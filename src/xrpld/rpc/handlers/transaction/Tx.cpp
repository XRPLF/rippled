/** @file
 *  Implements `doTxJson`, the server-side handler for the XRPL `tx` RPC method.
 *
 *  A client sends a transaction hash or a Concise Transaction ID (CTID) and
 *  receives back the full transaction, its execution metadata, and validation
 *  status.  The file is organised into three tightly scoped phases:
 *
 *  1. **`doTxJson`** — argument parsing and network-ID validation.
 *  2. **`doTxHelp`** — ledger-range checks, database lookup via
 *     `TransactionMaster::fetch`, and CTID re-encoding.
 *  3. **`populateJsonResponse`** — pure serialization; reads only from
 *     `TxResult`, never touches the database or ledger state.
 *
 *  @see RPC::decodeCTID, RPC::encodeCTID, TransactionMaster::fetch
 */
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/Status.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/TxSearched.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/NetworkOPs.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace xrpl {

/** Determine whether a closed ledger identified by sequence and hash is fully validated.
 *
 *  Applies three sequential guards before concluding a ledger is validated:
 *  (1) the local store must actually hold the ledger, (2) its sequence must
 *  not exceed the current validated ledger's sequence (a closed-but-not-yet-
 *  validated ledger would fail here), and (3) the stored hash for that
 *  sequence must match the supplied hash — the final guard against ledgers
 *  that were present but later reorganised out of the canonical chain.
 *
 *  @param ledgerMaster  The ledger master instance used to query local state.
 *  @param seq           The ledger sequence number to check.
 *  @param hash          The expected ledger hash at @p seq.
 *  @return `true` only when all three guards pass; `false` otherwise.
 *
 *  @note This is intentionally stricter than merely checking `haveLedger`:
 *      a node can hold a closed ledger whose sequence is still above the
 *      validated frontier, in which case `"validated": true` would be wrong.
 */
static bool
isValidated(LedgerMaster& ledgerMaster, std::uint32_t seq, uint256 const& hash)
{
    if (!ledgerMaster.haveLedger(seq))
        return false;

    if (seq > ledgerMaster.getValidatedLedger()->header().seq)
        return false;

    return ledgerMaster.getHashBySeq(seq) == hash;
}

/** Carries the raw output of a `tx` lookup across the phase boundary into the
 *  response-serialization layer.
 *
 *  All fields except `txn` and `searchedAll` are populated only when the
 *  transaction was located in a closed ledger and the requisite data was
 *  available.
 *
 *  The `meta` field is a discriminated union: in binary mode `doTxHelp`
 *  serializes `TxMeta` to a raw `Blob` up front, so the response layer never
 *  needs to re-serialize.  In non-binary mode the `shared_ptr<TxMeta>` is
 *  stored directly.
 *
 *  `searchedAll` defaults to `TxSearched::Unknown` (field absent from
 *  response) and is only set to `All` or `Some` when a ledger-range search
 *  was requested and the transaction was not found.
 */
struct TxResult
{
    /** The located transaction object, or null if lookup failed. */
    Transaction::pointer txn;

    /** Execution metadata: `Blob` in binary mode, `shared_ptr<TxMeta>` otherwise.
     *  Empty (default-constructed variant) until `doTxHelp` populates it. */
    std::variant<std::shared_ptr<TxMeta>, Blob> meta;

    /** Whether the containing ledger has been validated by consensus. */
    bool validated = false;

    /** Re-encoded CTID for this transaction, absent if any component exceeds
     *  its bit budget (`txnIdx > 0xFFFF`, `netID >= 0xFFFF`, or
     *  `lgrSeq >= 0x0FFF'FFFF`). */
    std::optional<std::string> ctid;

    /** Close time of the validated ledger that included this transaction.
     *  Populated only when `validated` is true. */
    std::optional<NetClock::time_point> closeTime;

    /** Hash of the closed ledger that included this transaction.
     *  Absent for open ledgers. */
    std::optional<uint256> ledgerHash;

    /** Completeness of the range search.  `Unknown` suppresses the
     *  `searched_all` field in the response for backward compatibility. */
    TxSearched searchedAll = TxSearched::Unknown;
};

/** Parsed and validated inputs for a single `tx` RPC call.
 *
 *  Exactly one of `hash` or `ctid` will be non-empty after `doTxJson`
 *  finishes parsing; supplying both is rejected before this struct is
 *  populated.  If `ctid` is present, `doTxHelp` resolves it to a hash via
 *  `LedgerMaster::txnIdFromIndex` before dispatching to `TransactionMaster`.
 */
struct TxArgs
{
    /** Raw transaction hash supplied via `"transaction"` param, or the hash
     *  resolved from a CTID by `doTxHelp`. */
    std::optional<uint256> hash;

    /** Decoded CTID as `{ledgerSeq, txnIndex}`.  The network ID has already
     *  been validated against the local node before this struct is used. */
    std::optional<std::pair<uint32_t, uint16_t>> ctid;

    /** Whether to return transaction and metadata in binary (hex) form. */
    bool binary = false;

    /** Optional inclusive ledger-sequence range `{min_ledger, max_ledger}`.
     *  When present, the lookup is restricted to this range and `searched_all`
     *  is reported in the response.  Range span is capped at 1000 ledgers. */
    std::optional<std::pair<uint32_t, uint32_t>> ledgerRange;
};

/** Resolve and fetch a transaction from the local database.
 *
 *  Performs ledger-range validation, hash resolution, database lookup, and
 *  CTID re-encoding in sequence:
 *
 *  1. **Range validation.** If `args.ledgerRange` is set, checks that the
 *     range is non-inverted and spans no more than 1000 ledgers
 *     (`kMAX_RANGE`).  Returns `RpcInvalidLgrRange` or
 *     `RpcExcessiveLgrRange` on violation.
 *
 *  2. **Hash resolution.** If `args.ctid` is set, calls
 *     `LedgerMaster::txnIdFromIndex` to convert `{ledgerSeq, txnIndex}`
 *     into a transaction hash.  A successful resolution also collapses the
 *     search range to the single ledger named by the CTID.  If the index
 *     lookup returns no hash, `RpcTxnNotFound` is returned immediately.
 *
 *  3. **Database fetch.** Calls the appropriate overload of
 *     `TransactionMaster::fetch`: the range-bounded overload when a
 *     `ledgerRange` was supplied, the full-history overload otherwise.
 *     Both return `std::variant<TxPair, TxSearched>`.  A `TxSearched`
 *     result means the transaction was absent; the sentinel is forwarded
 *     into `TxResult::searchedAll` and `RpcTxnNotFound` is returned.
 *
 *  4. **Metadata serialization.** In binary mode, `TxMeta` is serialized
 *     to a raw `Blob` here — avoiding re-serialization in the response
 *     phase.  In non-binary mode the `shared_ptr<TxMeta>` is stored as-is.
 *
 *  5. **Validation check.** Calls `isValidated` with the ledger's sequence
 *     and hash.  Only if validated is the close time fetched and stored.
 *
 *  6. **CTID re-encoding.** Attempts to encode a CTID from the found
 *     transaction's `sfTransactionIndex`, the ledger sequence, and the
 *     local network ID.  Omits the field silently if any component exceeds
 *     its bit budget rather than returning a truncated value.
 *
 *  @param context  RPC dispatch context providing access to app services.
 *  @param args     Validated input arguments from `doTxJson`.
 *  @return A pair of `TxResult` (populated on success) and `RPC::Status`
 *      indicating success or the specific failure reason.
 *
 *  @note This function must not emit JSON directly; all output goes through
 *      `TxResult` fields consumed by `populateJsonResponse`.
 */
std::pair<TxResult, RPC::Status>
doTxHelp(RPC::Context& context, TxArgs args)
{
    TxResult result;

    ClosedInterval<uint32_t> range;

    if (args.ledgerRange)
    {
        constexpr uint16_t kMAX_RANGE = 1000;

        if (args.ledgerRange->second < args.ledgerRange->first)
            return {result, RpcInvalidLgrRange};

        if (args.ledgerRange->second - args.ledgerRange->first > kMAX_RANGE)
            return {result, RpcExcessiveLgrRange};

        range = ClosedInterval<uint32_t>(args.ledgerRange->first, args.ledgerRange->second);
    }

    auto ec{RpcSuccess};

    using TxPair = std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>;

    result.searchedAll = TxSearched::Unknown;
    std::variant<TxPair, TxSearched> v;

    if (args.ctid)
    {
        args.hash =
            context.app.getLedgerMaster().txnIdFromIndex(args.ctid->first, args.ctid->second);

        if (args.hash)
            range = ClosedInterval<uint32_t>(args.ctid->first, args.ctid->second);
    }

    if (!args.hash)
        return {result, RpcTxnNotFound};

    if (args.ledgerRange)
    {
        v = context.app.getMasterTransaction().fetch(*(args.hash), range, ec);
    }
    else
    {
        v = context.app.getMasterTransaction().fetch(*(args.hash), ec);
    }

    if (auto e = std::get_if<TxSearched>(&v))
    {
        result.searchedAll = *e;
        return {result, RpcTxnNotFound};
    }

    auto [txn, meta] = std::get<TxPair>(v);

    if (ec == RpcDbDeserialization)
    {
        return {result, ec};
    }
    if (!txn)
    {
        return {result, RpcTxnNotFound};
    }

    result.txn = txn;
    if (txn->getLedger() == 0)
    {
        // Transaction is known but not yet included in any closed ledger
        // (e.g., still in the transaction queue). Return what we have with
        // no metadata or validation status.
        return {result, RpcSuccess};
    }

    std::shared_ptr<Ledger const> const ledger =
        context.ledgerMaster.getLedgerBySeq(txn->getLedger());

    if (ledger && !ledger->open())
        result.ledgerHash = ledger->header().hash;

    if (ledger && meta)
    {
        if (args.binary)
        {
            result.meta = meta->getAsObject().getSerializer().getData();
        }
        else
        {
            result.meta = meta;
        }
        result.validated =
            isValidated(context.ledgerMaster, ledger->header().seq, ledger->header().hash);
        if (result.validated)
            result.closeTime = context.ledgerMaster.getCloseTimeBySeq(txn->getLedger());

        // Attempt to re-encode a CTID.  All three components must fit within
        // their respective bit budgets; if any exceeds the limit the field is
        // simply omitted rather than emitting a truncated or invalid value.
        if (meta->getAsObject().isFieldPresent(sfTransactionIndex))
        {
            uint32_t const lgrSeq = ledger->header().seq;
            uint32_t const txnIdx = meta->getAsObject().getFieldU32(sfTransactionIndex);
            uint32_t const netID = context.app.getNetworkIDService().getNetworkID();

            if (txnIdx <= 0xFFFFU && netID < 0xFFFFU && lgrSeq < 0x0FFF'FFFFUL)
                result.ctid = RPC::encodeCTID(lgrSeq, txnIdx, netID);
        }
    }

    return {result, RpcSuccess};
}

/** Build the final JSON response for a `tx` RPC request.
 *
 *  Pure serialization layer — must not access the database, ledger master,
 *  or any other application service.  All required data is already in `res`.
 *
 *  **Error path.** When `res.second` indicates failure, the error is injected
 *  via `RPC::Status::inject`.  The one special case is `RpcTxnNotFound` with
 *  a known `searchedAll` value (i.e., a range-bounded search that concluded):
 *  in that case a `searched_all` boolean is added before the error fields so
 *  the client can distinguish genuine absence from an incomplete local history.
 *
 *  **API version branching.** For API v2+, the response is restructured:
 *  - Transaction goes under `"tx_json"` (non-binary) or `"tx_blob"` (binary).
 *  - `"ledger_hash"`, `"hash"`, and `"close_time_iso"` become top-level fields.
 *  - `JsonOptions::disable_API_prior_V2` suppresses deprecated legacy fields.
 *  For API v1 the legacy flat layout from `Transaction::getJson` is used.
 *
 *  **Metadata enrichment.** Three post-processing calls augment the `"meta"`
 *  JSON object after core serialization:
 *  - `insertDeliveredAmount` — fills `delivered_amount` for payments and
 *    check-cash transactions.
 *  - `RPC::insertNFTSyntheticInJson` — adds NFT-specific synthetic fields.
 *  - `RPC::insertMPTokenIssuanceID` — injects `mpt_issuance_id` for
 *    `MPTokenIssuanceCreate` transactions.
 *  These are applied only in non-binary mode where `result.meta` holds a
 *  `shared_ptr<TxMeta>`.
 *
 *  @param res      The lookup result produced by `doTxHelp`.
 *  @param args     The original parsed arguments (used for `binary` flag and
 *      API-version-neutral field selection).
 *  @param context  RPC context supplying `apiVersion` for branching logic.
 *  @return A `Json::Value` object ready to be returned to the caller.
 *
 *  @note `result.ledgerHash` is only populated for closed or validated
 *      ledgers (never for open ones), so `"ledger_hash"` is correctly absent
 *      when the transaction is pending.
 */
json::Value
populateJsonResponse(
    std::pair<TxResult, RPC::Status> const& res,
    TxArgs const& args,
    RPC::JsonContext const& context)
{
    json::Value response;
    RPC::Status const& error = res.second;
    TxResult const& result = res.first;
    if (error.toErrorCode() != RpcSuccess)
    {
        if (error.toErrorCode() == RpcTxnNotFound && result.searchedAll != TxSearched::Unknown)
        {
            response = json::Value(json::ValueType::Object);
            response[jss::searched_all] = (result.searchedAll == TxSearched::All);
            error.inject(response);
        }
        else
        {
            error.inject(response);
        }
    }
    else if (result.txn)
    {
        auto const& sttx = result.txn->getSTransaction();
        if (context.apiVersion > 1)
        {
            constexpr auto kOPTIONS_JSON =
                static_cast<JsonOptions::underlying_t>(JsonOptions::Values::IncludeDate) |
                static_cast<JsonOptions::underlying_t>(JsonOptions::Values::DisableApiPriorV2);
            if (args.binary)
            {
                response[jss::tx_blob] = result.txn->getJson(kOPTIONS_JSON, true);
            }
            else
            {
                response[jss::tx_json] = result.txn->getJson(kOPTIONS_JSON);
                RPC::insertDeliverMax(
                    response[jss::tx_json], sttx->getTxnType(), context.apiVersion);
            }

            if (result.ledgerHash)
                response[jss::ledger_hash] = to_string(*result.ledgerHash);

            response[jss::hash] = to_string(result.txn->getID());
            if (result.validated)
            {
                response[jss::ledger_index] = result.txn->getLedger();
                if (result.closeTime)
                    response[jss::close_time_iso] = toStringIso(*result.closeTime);
            }
        }
        else
        {
            response = result.txn->getJson(JsonOptions::Values::IncludeDate, args.binary);
            if (!args.binary)
                RPC::insertDeliverMax(response, sttx->getTxnType(), context.apiVersion);
        }

        if (auto blob = std::get_if<Blob>(&result.meta))
        {
            XRPL_ASSERT(args.binary, "xrpl::populateJsonResponse : binary is set");
            auto jsonMeta = (context.apiVersion > 1 ? jss::meta_blob : jss::meta);
            response[jsonMeta] = strHex(makeSlice(*blob));
        }
        else if (auto m = std::get_if<std::shared_ptr<TxMeta>>(&result.meta))
        {
            auto& meta = *m;
            if (meta)
            {
                response[jss::meta] = meta->getJson(JsonOptions::Values::None);
                insertDeliveredAmount(response[jss::meta], context, result.txn, *meta);
                RPC::insertNFTSyntheticInJson(response, sttx, *meta);
                RPC::insertMPTokenIssuanceID(response[jss::meta], sttx, *meta);
            }
        }
        response[jss::validated] = result.validated;

        if (result.ctid)
            response[jss::ctid] = *(result.ctid);
    }
    return response;
}

/** Entry point for the `tx` RPC method — parses arguments and drives the lookup pipeline.
 *
 *  Validates that exactly one of `"transaction"` (a 64-hex-character hash) or
 *  `"ctid"` (a 16-hex-character Concise Transaction ID) is present.  Supplying
 *  both is rejected with `rpcINVALID_PARAMS` because the intent would be
 *  ambiguous.  Supplying neither is also rejected.
 *
 *  **CTID network-ID check.** A CTID encodes the network it was issued on.  If
 *  the decoded `networkID` does not match `NetworkIDService::getNetworkID()`, a
 *  human-readable error is returned naming the expected network rather than
 *  silently doing a hash lookup on a different chain.
 *
 *  **Ledger range.** When both `"min_ledger"` and `"max_ledger"` are present
 *  they are extracted via `Json::Value::asUInt`, which throws on type mismatch.
 *  The `try`/`catch` converts that exception into `rpcINVALID_LGR_RANGE`; the
 *  range span limit (1000 ledgers) is enforced later in `doTxHelp`.
 *
 *  **Transaction-table guard.** The handler returns `rpcNOT_ENABLED`
 *  immediately when `config().useTxTables()` is false — the relational
 *  database tables that back the lookup are not present.
 *
 *  After argument parsing, delegates to `doTxHelp` for the database lookup
 *  and then to `populateJsonResponse` for serialization.
 *
 *  @param context  RPC dispatch context carrying JSON params, app services,
 *      and the resolved API version.
 *  @return A `Json::Value` with the transaction, metadata, and validation
 *      status, or an error object if any validation or lookup step fails.
 */
json::Value
doTxJson(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(RpcNotEnabled);

    TxArgs args;

    if (context.params.isMember(jss::transaction) && context.params.isMember(jss::ctid))
        return rpcError(RpcInvalidParams);

    if (context.params.isMember(jss::transaction))
    {
        uint256 hash;
        if (!hash.parseHex(context.params[jss::transaction].asString()))
            return rpcError(RpcNotImpl);
        args.hash = hash;
    }
    else if (context.params.isMember(jss::ctid))
    {
        auto ctid = RPC::decodeCTID(context.params[jss::ctid].asString());
        if (!ctid)
            return rpcError(RpcInvalidParams);

        auto const [lgr_seq, txn_idx, net_id] = *ctid;
        if (net_id != context.app.getNetworkIDService().getNetworkID())
        {
            std::stringstream out;
            out << "Wrong network. You should submit this request to a node "
                   "running on NetworkID: "
                << net_id;
            return RPC::makeError(RpcWrongNetwork, out.str());
        }
        args.ctid = {lgr_seq, txn_idx};
    }
    else
    {
        return rpcError(RpcInvalidParams);
    }

    args.binary = context.params.isMember(jss::binary) && context.params[jss::binary].asBool();

    if (context.params.isMember(jss::min_ledger) && context.params.isMember(jss::max_ledger))
    {
        try
        {
            args.ledgerRange = std::make_pair(
                context.params[jss::min_ledger].asUInt(), context.params[jss::max_ledger].asUInt());
        }
        catch (...)
        {
            // asUInt() throws if the JSON value is not an unsigned integer.
            return rpcError(RpcInvalidLgrRange);
        }
    }

    std::pair<TxResult, RPC::Status> const res = doTxHelp(context, args);
    return populateJsonResponse(res, args, context);
}

}  // namespace xrpl
