#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/serialize.h>

#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger_data.pb.h>

#include <memory>
#include <utility>

namespace xrpl {

/** Handle the `ledger_data` JSON-RPC command.
 *
 *  Returns a paginated forward scan of the raw state-node map for a specified
 *  closed ledger, enabling bulk retrieval of the complete ledger state. The
 *  canonical use case is ledger synchronization and archival: callers walk
 *  the full state by following the opaque `marker` token across successive
 *  calls.
 *
 *  Recognized request fields:
 *  - `ledger_hash` / `ledger_index` — ledger selector (resolved via
 *      `RPC::lookupLedger`; defaults to current).
 *  - `marker` — hex-encoded 256-bit resume key from a prior response.
 *      Absent on the first call; iteration starts from key zero.
 *  - `limit` — maximum entries to return. Non-admin callers are silently
 *      clamped to `RPC::Tuning::pageLength(isBinary)` (2048 binary,
 *      256 JSON). Admin / unlimited-role clients may request larger pages.
 *  - `binary` — boolean; when true, each entry is a hex-serialized blob
 *      (`serializeHex`). When false (default), each entry is expanded JSON.
 *  - `type` — optional string naming a `LedgerEntryType` to filter results
 *      (e.g. `"offer"`, `"account"`). Omit or pass `ltANY` for all types.
 *      An unrecognised type string returns `rpcINVALID_PARAMS`.
 *
 *  Response fields:
 *  - `ledger_hash`, `ledger_index` — identity of the chosen ledger.
 *  - `ledger` — full ledger header (JSON or binary). Present only on the
 *      first page (no `marker` in the request) to avoid redundant data on
 *      continuation calls.
 *  - `state` — array of ledger entries for this page.
 *  - `marker` — opaque resume token; absent when the scan is complete.
 *
 *  @note Pagination uses `upper_bound(key - 1)` fence-post arithmetic: when
 *      the page limit is exhausted, `marker` is set to `sle->key() - 1` so
 *      that the next `upper_bound` resumes on exactly that entry — no entry
 *      is skipped or duplicated across page boundaries.
 *  @note State entries are read via `keylet::unchecked`, which bypasses
 *      keylet validation; this is safe because the keys originate from the
 *      ledger's own state map rather than untrusted client input.
 *  @param context  The dispatch envelope carrying request parameters,
 *      application context, and role information.
 *  @return JSON object containing the response or an error.
 */
json::Value
doLedgerData(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto const& params = context.params;

    auto jvResult = RPC::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;

    bool const isMarker = params.isMember(jss::marker);
    ReadView::key_type key = ReadView::key_type();
    if (isMarker)
    {
        json::Value const& jMarker = params[jss::marker];
        if (!(jMarker.isString() && key.parseHex(jMarker.asString())))
            return RPC::expectedFieldError(jss::marker, "valid");
    }

    bool isBinary = false;
    if (params.isMember(jss::binary))
    {
        if (!params[jss::binary].isBool())
            return RPC::expectedFieldError(jss::binary, "boolean");
        isBinary = params[jss::binary].asBool();
    }

    int limit = -1;
    if (params.isMember(jss::limit))
    {
        json::Value const& jLimit = params[jss::limit];
        if (!jLimit.isIntegral())
            return RPC::expectedFieldError(jss::limit, "integer");

        limit = jLimit.asInt();
    }

    auto maxLimit = RPC::Tuning::pageLength(isBinary);
    if ((limit < 0) || ((limit > maxLimit) && (!isUnlimited(context.role))))
        limit = maxLimit;

    jvResult[jss::ledger_hash] = to_string(lpLedger->header().hash);
    jvResult[jss::ledger_index] = lpLedger->header().seq;

    if (!isMarker)
    {
        jvResult[jss::ledger] = getJson(LedgerFill(
            *lpLedger, &context, isBinary ? static_cast<int>(LedgerFill::Options::Binary) : 0));
    }

    auto [rpcStatus, type] = RPC::chooseLedgerEntryType(params);
    if (rpcStatus)
    {
        jvResult.clear();
        rpcStatus.inject(jvResult);
        return jvResult;
    }
    json::Value& nodes = jvResult[jss::state];
    if (nodes.type() == json::ValueType::Null)
    {
        nodes = json::Value(json::ValueType::Array);
    }

    auto e = lpLedger->sles.end();
    for (auto i = lpLedger->sles.upperBound(key); i != e; ++i)
    {
        auto sle = lpLedger->read(keylet::unchecked((*i)->key()));
        if (limit-- <= 0)
        {
            auto k = sle->key();
            jvResult[jss::marker] = to_string(--k);
            break;
        }

        if (type == ltANY || sle->getType() == type)
        {
            if (isBinary)
            {
                json::Value& entry = nodes.append(json::ValueType::Object);
                entry[jss::data] = serializeHex(*sle);
                entry[jss::index] = to_string(sle->key());
            }
            else
            {
                json::Value& entry = nodes.append(sle->getJson(JsonOptions::Values::None));
                entry[jss::index] = to_string(sle->key());
            }
        }
    }

    return jvResult;
}

/** Handle the `GetLedgerData` gRPC request.
 *
 *  Binary-only equivalent of `doLedgerData` for the gRPC transport. Every
 *  SLE is serialized raw into the protobuf response via `Serializer::add`;
 *  there is no JSON-format option.
 *
 *  Differences from the JSON path:
 *  - Page size is always `RPC::Tuning::pageLength(true)` (2048); role-based
 *    limit expansion is not available.
 *  - Both a start key (`marker`) and an end key (`end_marker`) may be
 *    supplied, bounding the scan from both sides. This allows callers to
 *    partition the keyspace into disjoint ranges and parallelise state
 *    downloads — a pattern absent from the JSON API.
 *  - The resume marker is written as raw bytes into the response protobuf
 *    rather than a JSON hex string.
 *
 *  Error mapping: `rpcINVALID_PARAMS` → `INVALID_ARGUMENT`; all other
 *  failures → `NOT_FOUND`. A non-OK status discards the response object.
 *
 *  @note Like `doLedgerData`, entries are read via `keylet::unchecked` and
 *      the fence-post `key - 1` marker arithmetic applies identically.
 *  @param context  The gRPC dispatch envelope carrying the
 *      `GetLedgerDataRequest` and application context.
 *  @return A pair of `{GetLedgerDataResponse, grpc::Status}`. On error the
 *      response is empty and the status carries the error detail.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerDataResponse, grpc::Status>
doLedgerDataGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerDataRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerDataResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == RpcInvalidParams)
        {
            errorStatus = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    uint256 startKey;
    if (auto key = uint256::fromVoidChecked(request.marker()))
    {
        startKey = *key;
    }
    else if (!request.marker().empty())
    {
        grpc::Status const errorStatus{grpc::StatusCode::INVALID_ARGUMENT, "marker malformed"};
        return {response, errorStatus};
    }

    auto e = ledger->sles.end();
    if (!request.end_marker().empty())
    {
        auto const key = uint256::fromVoidChecked(request.end_marker());

        if (!key)
            return {response, {grpc::StatusCode::INVALID_ARGUMENT, "end marker malformed"}};

        if (*key < startKey)
            return {response, {grpc::StatusCode::INVALID_ARGUMENT, "end marker out of range"}};

        e = ledger->sles.upperBound(*key);
    }

    int maxLimit = RPC::Tuning::pageLength(true);

    for (auto i = ledger->sles.upperBound(startKey); i != e; ++i)
    {
        auto sle = ledger->read(keylet::unchecked((*i)->key()));
        if (maxLimit-- <= 0)
        {
            auto k = sle->key();
            --k;
            response.set_marker(k.data(), k.size());
            break;
        }
        auto stateObject = response.mutable_ledger_objects()->add_objects();
        Serializer s;
        sle->add(s);
        stateObject->set_data(s.peekData().data(), s.getLength());
        stateObject->set_key(sle->key().data(), sle->key().size());
    }
    return {response, status};
}

}  // namespace xrpl
