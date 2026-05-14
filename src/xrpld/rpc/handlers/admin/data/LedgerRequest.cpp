#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

/** @file
 *  Admin RPC handler that locates a historical ledger by hash or sequence,
 *  acquiring it from the network if it is not already cached locally.
 */

/** Implement the `ledger_request` admin RPC command.
 *
 *  Declares a heavy resource cost immediately, then delegates all ledger
 *  resolution and acquisition logic to `RPC::getOrAcquireLedger()`. On
 *  success the response contains a compact header-level JSON representation
 *  of the ledger (no transactions, no state entries) together with the
 *  `ledger_index` field.
 *
 *  Exactly one of `ledger_hash` (64-char hex string) or `ledger_index`
 *  (positive integer) must be present in `params`; providing both or neither
 *  is a `rpcBAD_PARAM` error. Shortcut strings (`current`, `closed`,
 *  `validated`) are not accepted — this command targets specific historical
 *  ledgers rather than live chain state.
 *
 *  When the requested ledger is not locally cached, `getOrAcquireLedger`
 *  calls `InboundLedgers::acquire()` to initiate a peer-to-peer fetch. While
 *  that fetch is in progress the returned error JSON contains an `"acquiring"`
 *  field; callers should poll until the ledger becomes available.
 *
 *  @param context JSON-RPC context; `params` must contain exactly one of
 *      `ledger_hash` or `ledger_index`.
 *  @return On success, a JSON object with `ledger_index` and a nested
 *      `ledger` block containing the ledger header fields. On failure, an
 *      error JSON object; may include an `"acquiring"` field when a network
 *      fetch is pending.
 *  @note `context.loadType` is set to `kFEE_HEAVY_BURDEN_RPC` (weight 3000)
 *      before any work is done so the resource manager can rate-limit or
 *      deprioritize the caller regardless of whether the call ultimately
 *      triggers a network fetch.
 *  @note For the index-based path, the node's validated ledger must not be
 *      stale (older than `Tuning::kMAX_VALIDATED_LEDGER_AGE`); API v1 returns
 *      `rpcNO_CURRENT` on staleness, API v2+ returns `rpcNOT_SYNCED`.
 *  @note Non-admin callers receive an HTTP 403 response rather than a JSON
 *      error; the result object will be null in that case.
 */
json::Value
doLedgerRequest(RPC::JsonContext& context)
{
    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
    auto res = RPC::getOrAcquireLedger(context);

    if (!res.has_value())
        return res.error();

    auto const& ledger = res.value();

    json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->header().seq;
    addJson(jvResult, {*ledger, &context, 0});
    return jvResult;
}

}  // namespace xrpl
