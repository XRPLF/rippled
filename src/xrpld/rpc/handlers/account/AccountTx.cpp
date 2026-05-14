#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

/** @file
 *  Implements the `account_tx` JSON-RPC handler, which retrieves all
 *  transactions that affected a specific account over an optional ledger range,
 *  with cursor-based pagination for large result sets.
 *
 *  The implementation is split into five focused functions:
 *  `parseLedgerArgs`, `getLedgerRange`, `doAccountTxHelp`,
 *  `populateJsonResponse`, and the public entry point `doAccountTx`.
 */

namespace xrpl {

using TxnsData = RelationalDatabase::AccountTxs;
using TxnsDataBinary = RelationalDatabase::MetaTxsList;
using TxnDataBinary = RelationalDatabase::txnMetaLedgerType;
using AccountTxArgs = RelationalDatabase::AccountTxArgs;
using AccountTxResult = RelationalDatabase::AccountTxResult;
using LedgerSpecifier = RelationalDatabase::LedgerSpecifier;

/** Parse ledger selection fields from a request into a `LedgerSpecifier`.
 *
 *  Translates raw JSON `params` into a `LedgerSpecifier` variant that
 *  uniformly represents any supported ledger or ledger-range selector.
 *  Priority order is: `ledger_index_min`/`ledger_index_max` range →
 *  `ledger_hash` → `ledger_index` → absent (returns `std::nullopt`).
 *
 *  At API version 2+, combining a min/max range with a single-ledger
 *  identifier (`ledger_hash` or `ledger_index`) is an `rpcINVALID_PARAMS`
 *  error. In API v1, this combination was silently tolerated.
 *
 *  Negative values for `ledger_index_min` default to `0` (earliest),
 *  and negative `ledger_index_max` defaults to `UINT32_MAX` (latest),
 *  following the long-standing XRPL convention that `-1` means "unbounded".
 *
 *  @param context The RPC dispatch context; used for `apiVersion`.
 *  @param params  The raw JSON request parameters object.
 *  @return On success, an `optional<LedgerSpecifier>` — empty optional
 *      when no ledger field was supplied. On error, a `json::Value`
 *      containing an injected RPC error.
 */
std::variant<std::optional<LedgerSpecifier>, json::Value>
parseLedgerArgs(RPC::Context& context, json::Value const& params)
{
    json::Value response;
    if (context.apiVersion > 1u)
    {
        if ((params.isMember(jss::ledger_index_min) || params.isMember(jss::ledger_index_max)) &&
            (params.isMember(jss::ledger_hash) || params.isMember(jss::ledger_index)))
        {
            RPC::Status const status{RpcInvalidParams, "invalidParams"};
            status.inject(response);
            return response;
        }
    }
    if (params.isMember(jss::ledger_index_min) || params.isMember(jss::ledger_index_max))
    {
        uint32_t const min =
            params.isMember(jss::ledger_index_min) && params[jss::ledger_index_min].asInt() >= 0
            ? params[jss::ledger_index_min].asUInt()
            : 0;
        uint32_t const max =
            params.isMember(jss::ledger_index_max) && params[jss::ledger_index_max].asInt() >= 0
            ? params[jss::ledger_index_max].asUInt()
            : UINT32_MAX;

        return LedgerRange{.min = min, .max = max};
    }
    if (params.isMember(jss::ledger_hash))
    {
        auto& hashValue = params[jss::ledger_hash];
        if (!hashValue.isString())
        {
            RPC::Status const status{RpcInvalidParams, "ledgerHashNotString"};
            status.inject(response);
            return response;
        }

        LedgerHash hash;
        if (!hash.parseHex(hashValue.asString()))
        {
            RPC::Status const status{RpcInvalidParams, "ledgerHashMalformed"};
            status.inject(response);
            return response;
        }
        return hash;
    }
    if (params.isMember(jss::ledger_index))
    {
        LedgerSpecifier ledger;
        if (params[jss::ledger_index].isNumeric())
        {
            ledger = params[jss::ledger_index].asUInt();
        }
        else
        {
            std::string const ledgerStr = params[jss::ledger_index].asString();

            if (ledgerStr == "current" || ledgerStr.empty())
            {
                ledger = LedgerShortcut::Current;
            }
            else if (ledgerStr == "closed")
            {
                ledger = LedgerShortcut::Closed;
            }
            else if (ledgerStr == "validated")
            {
                ledger = LedgerShortcut::Validated;
            }
            else
            {
                RPC::Status const status{RpcInvalidParams, "ledger_index string malformed"};
                status.inject(response);
                return response;
            }
        }
        return ledger;
    }
    return std::optional<LedgerSpecifier>{};
}

/** Resolve an optional `LedgerSpecifier` to a concrete validated ledger range.
 *
 *  Queries the node's validated ledger window and intersects it with the
 *  caller-supplied specifier. When no specifier is given the full validated
 *  range is returned.
 *
 *  Error behaviour differs by API version:
 *  - If the node has no validated range: v1 returns `rpcLGR_IDXS_INVALID`;
 *    v2+ returns `rpcNOT_SYNCED`.
 *  - For a range specifier that extends beyond the validated window: v1
 *    silently clamps; v2+ returns `rpcLGR_IDX_MALFORMED` to prevent clients
 *    from unknowingly querying incomplete data.
 *  - For a single-ledger specifier (hash, sequence, or shortcut) that is
 *    unvalidated or outside the window: returns `rpcLGR_NOT_VALIDATED`.
 *
 *  @param context         The RPC dispatch context; used for `apiVersion`
 *      and `ledgerMaster`.
 *  @param ledgerSpecifier The parsed ledger selector, or `std::nullopt` to
 *      request the full validated range.
 *  @return Either the resolved `LedgerRange` or an `RPC::Status` error.
 */
std::variant<LedgerRange, RPC::Status>
getLedgerRange(RPC::Context& context, std::optional<LedgerSpecifier> const& ledgerSpecifier)
{
    std::uint32_t uValidatedMin = 0;
    std::uint32_t uValidatedMax = 0;
    bool const bValidated = context.ledgerMaster.getValidatedRange(uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        if (context.apiVersion == 1)
            return RpcLgrIdxsInvalid;
        return RpcNotSynced;
    }

    std::uint32_t uLedgerMin = uValidatedMin;
    std::uint32_t uLedgerMax = uValidatedMax;
    if (ledgerSpecifier)
    {
        auto status = std::visit(
            [&](auto const& ls) -> RPC::Status {
                using T = std::decay_t<decltype(ls)>;
                if constexpr (std::is_same_v<T, LedgerRange>)
                {
                    if (context.apiVersion > 1u)
                    {
                        if ((ls.max > uValidatedMax && ls.max != -1) ||
                            (ls.min < uValidatedMin && ls.min != 0))
                        {
                            return RpcLgrIdxMalformed;
                        }
                    }
                    if (ls.min > uValidatedMin)
                    {
                        uLedgerMin = ls.min;
                    }
                    if (ls.max < uValidatedMax)
                    {
                        uLedgerMax = ls.max;
                    }
                    if (uLedgerMax < uLedgerMin)
                    {
                        if (context.apiVersion == 1)
                            return RpcLgrIdxsInvalid;
                        return RpcInvalidLgrRange;
                    }
                }
                else
                {
                    std::shared_ptr<ReadView const> ledgerView;
                    auto status = getLedger(ledgerView, ls, context);
                    if (!ledgerView)
                    {
                        return status;
                    }

                    bool const validated = context.ledgerMaster.isValidated(*ledgerView);

                    if (!validated || ledgerView->header().seq > uValidatedMax ||
                        ledgerView->header().seq < uValidatedMin)
                    {
                        return RpcLgrNotValidated;
                    }
                    uLedgerMin = uLedgerMax = ledgerView->header().seq;
                }
                return RPC::Status::kOK;
            },
            *ledgerSpecifier);

        if (status)
            return status;
    }
    return LedgerRange{.min = uLedgerMin, .max = uLedgerMax};
}

/** Execute an `account_tx` database query from parsed arguments.
 *
 *  Sets the resource load class to `feeMediumBurdenRPC`, resolves the ledger
 *  range via `getLedgerRange`, then dispatches to one of four `RelationalDatabase`
 *  page methods depending on `(binary, forward)`:
 *
 *  | binary | forward | Method                    |
 *  |--------|---------|---------------------------|
 *  | false  | false   | `newestAccountTxPage()`   |
 *  | false  | true    | `oldestAccountTxPage()`   |
 *  | true   | false   | `newestAccountTxPageB()`  |
 *  | true   | true    | `oldestAccountTxPageB()`  |
 *
 *  The `B`-suffixed variants return raw `(tx_blob, meta_blob, ledger_seq)`
 *  tuples, avoiding object deserialization for binary callers such as indexers.
 *
 *  @param context The RPC dispatch context.
 *  @param args    Parsed request fields: account, optional ledger specifier,
 *      binary flag, forward flag, limit, and optional pagination marker.
 *  @return A pair of `AccountTxResult` (filled on success) and an
 *      `RPC::Status`. On ledger-range error the result is empty and the
 *      status carries the relevant error code.
 */
std::pair<AccountTxResult, RPC::Status>
doAccountTxHelp(RPC::Context& context, AccountTxArgs const& args)
{
    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;

    AccountTxResult result;

    auto lgrRange = getLedgerRange(context, args.ledger);
    if (auto stat = std::get_if<RPC::Status>(&lgrRange))
    {
        return {result, *stat};
    }

    result.ledgerRange = std::get<LedgerRange>(lgrRange);

    result.marker = args.marker;

    RelationalDatabase::AccountTxPageOptions const options = {
        .account = args.account,
        .ledgerRange = result.ledgerRange,
        .marker = result.marker,
        .limit = args.limit,
        .bAdmin = isUnlimited(context.role)};

    auto& db = context.app.getRelationalDatabase();

    if (args.binary)
    {
        if (args.forward)
        {
            auto [tx, marker] = db.oldestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db.newestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }
    else
    {
        if (args.forward)
        {
            auto [tx, marker] = db.oldestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db.newestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }

    result.limit = args.limit;
    JLOG(context.j.debug()) << __func__ << " : finished";

    return {result, RpcSuccess};
}

/** Serialize an `account_tx` query result into the final JSON response.
 *
 *  On error, injects the `RPC::Status` error code into the response and
 *  returns immediately. On success, serializes each transaction entry and
 *  applies four enrichment passes in order:
 *
 *  1. `RPC::insertDeliverMax()` — adds `DeliverMax` for payment transactions.
 *  2. `insertDeliveredAmount()` — adds `delivered_amount` to metadata for
 *     successful payments and check-cash transactions; falls back to the
 *     transaction `Amount` field, or `"unavailable"` for pre-metadata history.
 *  3. `RPC::insertNFTSyntheticInJson()` — synthesizes NFT fields from
 *     metadata added after the original NFT amendments.
 *  4. `RPC::insertMPTokenIssuanceID()` — injects `mpt_issuance_id` for
 *     successful `MPTokenIssuanceCreate` transactions.
 *
 *  API version differences in JSON output:
 *  - v1: transaction key is `tx`; metadata key is `meta`.
 *  - v2+: transaction key is `tx_json` with `JsonOptions::disable_API_prior_V2`;
 *    `hash`, `ledger_index`, `ledger_hash`, and `close_time_iso` are promoted
 *    to the top-level entry; metadata key is `meta_blob` for binary results.
 *
 *  @note The `UNREACHABLE` macro on the missing-metadata branch documents a
 *      developer-facing invariant: a valid transaction without metadata
 *      indicates database corruption and should never occur at runtime.
 *
 *  @param res     The result pair from `doAccountTxHelp`.
 *  @param args    The original parsed request arguments; used for the
 *      `binary` flag assertion.
 *  @param context The RPC JSON dispatch context; used for `apiVersion`,
 *      `ledgerMaster`, and logging.
 *  @return A fully-formed JSON response object suitable for returning to the
 *      client.
 */
json::Value
populateJsonResponse(
    std::pair<AccountTxResult, RPC::Status> const& res,
    AccountTxArgs const& args,
    RPC::JsonContext const& context)
{
    json::Value response;
    RPC::Status const& error = res.second;
    if (error.toErrorCode() != RpcSuccess)
    {
        error.inject(response);
    }
    else
    {
        AccountTxResult const& result = res.first;
        response[jss::validated] = true;
        response[jss::limit] = result.limit;
        response[jss::account] = context.params[jss::account].asString();
        response[jss::ledger_index_min] = result.ledgerRange.min;
        response[jss::ledger_index_max] = result.ledgerRange.max;

        json::Value& jvTxns = (response[jss::transactions] = json::ValueType::Array);

        if (auto txnsData = std::get_if<TxnsData>(&result.transactions))
        {
            XRPL_ASSERT(!args.binary, "xrpl::populateJsonResponse : binary is not set");

            for (auto const& [txn, txnMeta] : *txnsData)
            {
                if (txn)
                {
                    json::Value& jvObj = jvTxns.append(json::ValueType::Object);
                    jvObj[jss::validated] = true;

                    auto const jsonTx = (context.apiVersion > 1 ? jss::tx_json : jss::tx);
                    if (context.apiVersion > 1)
                    {
                        jvObj[jsonTx] = txn->getJson(
                            static_cast<JsonOptions::underlying_t>(
                                JsonOptions::Values::IncludeDate) |
                                static_cast<JsonOptions::underlying_t>(
                                    JsonOptions::Values::DisableApiPriorV2),
                            false);
                        jvObj[jss::hash] = to_string(txn->getID());
                        jvObj[jss::ledger_index] = txn->getLedger();
                        jvObj[jss::ledger_hash] =
                            to_string(context.ledgerMaster.getHashBySeq(txn->getLedger()));

                        if (auto closeTime =
                                context.ledgerMaster.getCloseTimeBySeq(txn->getLedger()))
                            jvObj[jss::close_time_iso] = toStringIso(*closeTime);
                    }
                    else
                    {
                        jvObj[jsonTx] = txn->getJson(JsonOptions::Values::IncludeDate);
                    }

                    auto const& sttx = txn->getSTransaction();
                    RPC::insertDeliverMax(jvObj[jsonTx], sttx->getTxnType(), context.apiVersion);
                    if (txnMeta)
                    {
                        jvObj[jss::meta] = txnMeta->getJson(JsonOptions::Values::IncludeDate);
                        insertDeliveredAmount(jvObj[jss::meta], context, txn, *txnMeta);
                        RPC::insertNFTSyntheticInJson(jvObj, sttx, *txnMeta);
                        RPC::insertMPTokenIssuanceID(jvObj[jss::meta], sttx, *txnMeta);
                    }
                    else
                    {
                        // LCOV_EXCL_START
                        UNREACHABLE(
                            "xrpl::populateJsonResponse : missing "
                            "transaction metadata");
                        // LCOV_EXCL_STOP
                    }
                }
            }
        }
        else
        {
            XRPL_ASSERT(args.binary, "xrpl::populateJsonResponse : binary is set");

            for (auto const& binaryData : std::get<TxnsDataBinary>(result.transactions))
            {
                json::Value& jvObj = jvTxns.append(json::ValueType::Object);

                jvObj[jss::tx_blob] = strHex(std::get<0>(binaryData));
                auto const jsonMeta = (context.apiVersion > 1 ? jss::meta_blob : jss::meta);
                jvObj[jsonMeta] = strHex(std::get<1>(binaryData));
                jvObj[jss::ledger_index] = std::get<2>(binaryData);
                jvObj[jss::validated] = true;
            }
        }

        if (result.marker)
        {
            response[jss::marker] = json::ValueType::Object;
            response[jss::marker][jss::ledger] = result.marker->ledgerSeq;
            response[jss::marker][jss::seq] = result.marker->txnSeq;
        }
    }

    JLOG(context.j.debug()) << __func__ << " : finished";
    return response;
}

/** Handle the `account_tx` RPC command.
 *
 *  Returns all transactions that affected the specified account, optionally
 *  constrained to a ledger range and resumable via a cursor marker.
 *
 *  Expected request fields:
 *  - `account` (string, required) — base-58 account address.
 *  - `ledger_index_min` / `ledger_index_max` (integer, optional) — inclusive
 *      ledger range; `-1` means "unbounded" in that direction.
 *  - `ledger_hash` (string, optional) — single ledger by hash.
 *  - `ledger_index` (integer or shortcut string, optional) — single ledger.
 *  - `binary` (boolean, optional, default false) — return raw blobs instead
 *      of decoded JSON objects.
 *  - `forward` (boolean, optional, default false) — iterate oldest-first
 *      rather than newest-first.
 *  - `limit` (integer, optional) — maximum transactions per page.
 *  - `marker` (object `{ledger, seq}`, optional) — resume pagination from a
 *      prior response's marker.
 *
 *  Returns `rpcNOT_ENABLED` immediately when the node is not maintaining a
 *  transaction index (`config().useTxTables()` is false).
 *
 *  At API v2+, `binary` and `forward` must be actual JSON booleans (not
 *  coerced strings); `marker.ledger` and `marker.seq` must be unsigned
 *  integers, with an explicit error message if either is missing or has the
 *  wrong type.
 *
 *  @param context The RPC JSON dispatch context carrying request params,
 *      application services, API version, and role.
 *  @return A JSON response object containing a `transactions` array and
 *      effective `ledger_index_min`/`ledger_index_max`, or an RPC error.
 */
json::Value
doAccountTx(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(RpcNotEnabled);

    auto& params = context.params;
    AccountTxArgs args;
    json::Value response;

    if (context.apiVersion > 1u && params.isMember(jss::binary) && !params[jss::binary].isBool())
    {
        return RPC::invalidFieldError(jss::binary);
    }
    if (context.apiVersion > 1u && params.isMember(jss::forward) && !params[jss::forward].isBool())
    {
        return RPC::invalidFieldError(jss::forward);
    }

    if (auto const err = RPC::readLimitField(args.limit, RPC::Tuning::kACCOUNT_TX, context))
        return *err;

    args.binary = params.isMember(jss::binary) && params[jss::binary].asBool();
    args.forward = params.isMember(jss::forward) && params[jss::forward].asBool();

    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    auto const account = parseBase58<AccountID>(params[jss::account].asString());
    if (!account)
        return rpcError(RpcActMalformed);

    args.account = *account;

    auto parseRes = parseLedgerArgs(context, params);
    if (auto jv = std::get_if<json::Value>(&parseRes))
    {
        return *jv;
    }

    args.ledger = std::get<std::optional<LedgerSpecifier>>(parseRes);

    if (params.isMember(jss::marker))
    {
        auto& token = params[jss::marker];
        if (!token.isMember(jss::ledger) || !token.isMember(jss::seq) ||
            !token[jss::ledger].isConvertibleTo(json::ValueType::UInt) ||
            !token[jss::seq].isConvertibleTo(json::ValueType::UInt))
        {
            RPC::Status const status{
                RpcInvalidParams,
                "invalid marker. Provide ledger index via ledger field, and "
                "transaction sequence number via seq field"};
            status.inject(response);
            return response;
        }
        args.marker = {
            .ledgerSeq = token[jss::ledger].asUInt(), .txnSeq = token[jss::seq].asUInt()};
    }

    auto res = doAccountTxHelp(context, args);
    JLOG(context.j.debug()) << __func__ << " populating response";
    return populateJsonResponse(res, args, context);
}

}  // namespace xrpl
