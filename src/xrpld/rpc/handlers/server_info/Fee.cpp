/** @file
 *  Handler for the `fee` RPC command.
 *
 *  Exposes the node's current transaction fee environment to any connected
 *  client (`Role::USER`). The response is assembled entirely by
 *  `TxQ::doRPC`; this translation unit contributes only the dispatch point
 *  and a defensive type check on the return value.
 *
 *  Registration in `Handler.cpp`:
 *  @code
 *  {"fee", byRef(&doFee), Role::USER, NEEDS_CURRENT_LEDGER}
 *  @endcode
 *  The `NEEDS_CURRENT_LEDGER` condition means the framework guarantees that
 *  the open ledger exists before `doFee` is ever called, making the
 *  null-view guard inside `TxQ::doRPC` unreachable from this handler.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>

namespace xrpl {

/** Return the node's current transaction fee schedule.
 *
 *  Delegates entirely to `TxQ::doRPC`, which queries the open ledger via
 *  `TxQ::getMetrics` and returns a JSON object with two sub-objects:
 *
 *  - `levels` — fee values in normalized fee-level units:
 *    `reference_level`, `minimum_level`, `median_level`, `open_ledger_level`.
 *  - `drops` — absolute amounts in XRP drops:
 *    `base_fee`, `median_fee`, `minimum_fee`, `open_ledger_fee`.
 *
 *  Top-level fields include `ledger_current_index`, `current_ledger_size`,
 *  `expected_ledger_size`, `current_queue_size`, and (when a queue size cap
 *  is configured) `max_queue_size`.
 *
 *  @param context  RPC dispatch context; must have a current open ledger
 *      (enforced by the `NEEDS_CURRENT_LEDGER` handler condition).
 *  @return JSON object with `levels` and `drops` sub-objects on success.
 *      The failure branch — triggered only when `TxQ::doRPC` returns a
 *      non-object value, which cannot happen via this handler — is excluded
 *      from coverage (`LCOV_EXCL`) and marked `UNREACHABLE`.
 */
json::Value
doFee(RPC::JsonContext& context)
{
    auto result = context.app.getTxQ().doRPC(context.app);
    if (result.type() == json::ValueType::Object)
        return result;

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::doFee : invalid result type");
    RPC::injectError(RpcInternal, context.params);
    return context.params;
    // LCOV_EXCL_STOP
}

}  // namespace xrpl
