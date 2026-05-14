#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

/** @file
 *  Implements the `tx_history` RPC command, a paginated raw transaction log
 *  browser backed by the node's relational database.
 *
 *  This command is available only in API version 1; API v2+ returns
 *  `unknownCmd` via the handler table's version range restriction.
 */

/** Return a page of up to 20 historical transactions starting at a given
 *  offset, sorted in descending ledger-sequence order.
 *
 *  The handler is a thin bridge between the RPC dispatch layer and
 *  `RelationalDatabase::getTxHistory()`. It performs no filtering by account
 *  or transaction type; it is a raw log browser, not a query engine.
 *
 *  Validation sequence (short-circuits on first failure):
 *  1. `useTxTables()` config flag — returns `rpcNOT_ENABLED` if absent.
 *  2. Presence of the `start` field — returns `rpcINVALID_PARAMS` if missing.
 *  3. Deep-pagination cap: `start > 10000` is rejected with `rpcNO_PERMISSION`
 *     unless `isUnlimited(context.role)` is true (ADMIN / IDENTIFIED roles).
 *
 *  After validation, `getRelationalDatabase().getTxHistory(startIndex)` is
 *  called. The database layer enforces the 20-transaction page size. Each
 *  transaction is serialized via `getJson(JsonOptions::None)` and enriched
 *  with `RPC::insertDeliverMax()` to inject the `DeliverMax` field (and, for
 *  API version > 1, remove the legacy `Amount` field) on Payment transactions,
 *  consistent with the enrichment applied in `Tx.cpp` and
 *  `TransactionEntry.cpp`.
 *
 *  The response echoes the requested `index` value so callers can track their
 *  pagination position across calls.
 *
 *  @note `context.params[jss::start].asUInt()` silently converts non-integer
 *      JSON values to `0` rather than returning a type error. A `start` of
 *      `0` is valid, so this is harmless in practice and is consistent with
 *      how other XRPL RPC handlers treat loosely-typed JSON integers.
 *  @note This command is retired in API v2 and later. Clients on v2+ receive
 *      `unknownCmd` from the handler table before this function is called.
 *  @note The resource load type is set to `kFEE_MEDIUM_BURDEN_RPC` because
 *      the backing call is a database scan rather than a point lookup.
 *  @param context The RPC dispatch context carrying the request parameters,
 *      application state, caller role, and API version.
 *  @return A JSON object with `"index"` (the requested start offset) and
 *      `"txs"` (array of transaction JSON objects), or an RPC error object.
 */
json::Value
doTxHistory(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(RpcNotEnabled);

    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;

    if (!context.params.isMember(jss::start))
        return rpcError(RpcInvalidParams);

    unsigned int const startIndex = context.params[jss::start].asUInt();

    if ((startIndex > 10000) && (!isUnlimited(context.role)))
        return rpcError(RpcNoPermission);

    auto trans = context.app.getRelationalDatabase().getTxHistory(startIndex);

    json::Value obj;
    json::Value& txs = obj[jss::txs];
    obj[jss::index] = startIndex;

    for (auto const& t : trans)
    {
        json::Value txJson = t->getJson(JsonOptions::Values::None);
        RPC::insertDeliverMax(txJson, t->getSTransaction()->getTxnType(), context.apiVersion);
        txs.append(txJson);
    }

    return obj;
}

}  // namespace xrpl
