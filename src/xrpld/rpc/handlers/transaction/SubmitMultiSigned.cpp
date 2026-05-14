#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

/** Entry point for the `submit_multisigned` RPC command.
 *
 *  Classifies the request as `kFEE_HEAVY_BURDEN_RPC` (weight 3000) before
 *  doing any other work, so the resource manager can rate-limit the call even
 *  if it fails early.  The handler itself is intentionally thin: it extracts
 *  the optional `fail_hard` boolean (defaults to `false` when absent), converts
 *  it to `NetworkOPs::FailHard` via `NetworkOPs::doFailHard`, then delegates
 *  all structural validation, cryptographic signature checking, ledger lookups,
 *  and network submission to `RPC::transactionSubmitMultiSigned`.
 *
 *  The validated-ledger age is forwarded from `LedgerMaster` so the callee can
 *  refuse fee auto-fill when the node's view of the network is stale (>2 min).
 *  The `ProcessTransactionFn` closure produced by `getProcessTxnFn` captures
 *  `NetworkOPs` by reference, decoupling the business-logic layer from the full
 *  RPC context and enabling injection of a mock in unit tests.
 *
 *  @param context  RPC dispatch context carrying `params`, `role`, `apiVersion`,
 *      `ledgerMaster`, `netOps`, and the resource load tracker.
 *  @return JSON object with `tx_json`, `tx_blob`, and engine result fields on
 *      success, or a JSON error object on validation or submission failure.
 *  @note `fail_hard: true` rejects the transaction if it cannot be immediately
 *      applied to the open ledger; the default (`false`) allows it to be queued.
 *  @see RPC::transactionSubmitMultiSigned, RPC::getProcessTxnFn
 */
json::Value
doSubmitMultiSigned(RPC::JsonContext& context)
{
    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard(failHard);

    return RPC::transactionSubmitMultiSigned(
        context.params,
        context.apiVersion,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        RPC::getProcessTxnFn(context.netOps));
}

}  // namespace xrpl
