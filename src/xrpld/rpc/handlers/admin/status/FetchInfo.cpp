/** @file
 *  Handler for the `fetch_info` admin RPC command.
 *
 *  Exposes the internal state of the inbound ledger fetch subsystem and
 *  optionally clears recorded fetch failures, allowing operators to diagnose
 *  and recover from stalled ledger acquisition.
 */

#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

/** Handle the `fetch_info` admin RPC command.
 *
 *  Returns a snapshot of the inbound ledger fetch subsystem's current state.
 *  If the `"clear"` parameter is present and true, failed-fetch records are
 *  wiped first via `InboundLedgers::clearFailures()` so that previously
 *  abandoned ledgers become eligible for re-acquisition. The snapshot
 *  returned under `"info"` always reflects the post-clear state, making a
 *  single `{"clear": true}` call both an action and a confirmation.
 *
 *  Registered in `Handler.cpp` as `"fetch_info"` with `Role::ADMIN` and
 *  `Condition::NoCondition`.
 *
 *  @param context  RPC dispatch envelope; `context.params` may contain an
 *      optional boolean `"clear"` field. `context.netOps` is the
 *      `NetworkOPs` abstraction through which the fetch subsystem is reached.
 *  @return A JSON object with:
 *      - `"clear"` (boolean, present only when the clear action was taken)
 *        — echoes `true` to confirm that failed-fetch records were wiped.
 *      - `"info"` (object) — the fetch-state snapshot from
 *        `InboundLedgers::getInfo()`.
 *  @note The `"clear"` field is read with an `isMember` guard before
 *      `asBool()` to avoid a type-coercion exception when the field is
 *      absent; unexpected non-boolean values are handled by `Json::Value`'s
 *      built-in coercion, consistent with sibling handlers in this directory.
 */
json::Value
doFetchInfo(RPC::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    if (context.params.isMember(jss::clear) && context.params[jss::clear].asBool())
    {
        context.netOps.clearLedgerFetch();
        ret[jss::clear] = true;
    }

    ret[jss::info] = context.netOps.getLedgerFetchInfo();

    return ret;
}

}  // namespace xrpl
