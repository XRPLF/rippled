/** @file
 *  Implements the `server_state` JSON-RPC handler (`doServerState`).
 *
 *  This handler is the machine-readable counterpart to `ServerInfo.cpp`.
 *  Both handlers delegate all data assembly to `NetworkOPs::getServerInfo()`
 *  and differ only in the `human` flag passed to that function:
 *  - `doServerState` passes `human = false` — XRP amounts are returned as
 *    raw integer drop counts, fees as integer basis points, and timestamps as
 *    Unix epoch integers, suited for programmatic clients and monitoring tools.
 *  - `doServerInfo` passes `human = true` — the same fields are formatted with
 *    units and floating-point representations for human operators.
 *
 *  The response is wrapped under the `jss::state` key; the parallel sibling
 *  uses `jss::info`. Both keys are compile-time constants from the `jss`
 *  namespace, preventing typo-class bugs at compile time.
 *
 *  Role gating (`context.role == Role::ADMIN`) is resolved upstream by the
 *  RPC framework before this handler is invoked; passing the boolean directly
 *  means no trust decisions are made here. Admin callers receive additional
 *  fields such as peer counts, internal queue depths, and load factor details.
 *
 *  The `counters` parameter opt-in is guarded with `isMember` before
 *  `asBool()` to avoid a `Json::LogicError` on a missing field. A malformed
 *  (non-boolean) value will raise `Json::LogicError`; an absent or falsy
 *  value silently opts out via short-circuit `&&`.
 *
 *  @see doServerInfo  Sibling handler emitting human-readable (`human=true`)
 *      representation of the same data under `jss::info`.
 *  @see NetworkOPs::getServerInfo  The function that assembles the full response.
 */

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

/** Handle the `server_state` RPC command.
 *
 *  Returns machine-readable server status by delegating to
 *  `NetworkOPs::getServerInfo` with `human = false`. Numeric quantities
 *  (XRP amounts, fees, timestamps) are emitted as raw integers rather than
 *  human-friendly strings. The result is nested under the `jss::state` key
 *  in the returned JSON object.
 *
 *  @param context  RPC dispatch context carrying the JSON params, resolved
 *      role, and references to application subsystems.
 *  @return JSON object with a single `"state"` key whose value is the full
 *      server status payload from `NetworkOPs::getServerInfo`.
 */
json::Value
doServerState(RPC::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    ret[jss::state] = context.netOps.getServerInfo(
        false,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) && context.params[jss::counters].asBool());

    return ret;
}

}  // namespace xrpl
