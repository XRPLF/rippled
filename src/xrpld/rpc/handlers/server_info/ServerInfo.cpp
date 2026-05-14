/** @file
 *  Implements the `server_info` JSON-RPC handler (`doServerInfo`).
 *
 *  The handler is intentionally thin: it constructs an empty JSON object,
 *  delegates all data assembly to `NetworkOPs::getServerInfo()`, and wraps
 *  the result under the `jss::info` key. All substantive logic — gathering
 *  warnings, ledger positions, peer counts, consensus state, and optional
 *  performance counters — lives in `NetworkOPsImp::getServerInfo()`.
 *
 *  The `human = true` flag passed here is the sole behavioral difference
 *  between this handler and its sibling `ServerState.cpp` (`human = false`).
 *  When `human` is true, `getServerInfo` includes human-readable fields such
 *  as `hostid` and formats numeric quantities for readability; when false, it
 *  emits compact machine-friendly representations. Both handlers use `jss::info`
 *  vs. `jss::state` as their respective response keys over the same data source.
 *
 *  Role gating (`context.role == Role::ADMIN`) is forwarded directly to
 *  `getServerInfo`, which uses it to suppress sensitive fields (amendment-majority
 *  warnings, node configuration) for non-admin callers. Authentication and role
 *  resolution are performed upstream by the RPC framework before this handler
 *  is ever invoked.
 *
 *  The `counters` parameter is guarded with `isMember` before `asBool()` to
 *  avoid undefined behavior on a missing JSON field. If the field is present
 *  but not boolean, `asBool()` raises `Json::LogicError` — acceptable since
 *  such a request is malformed.
 *
 *  @see doServerState  Sibling handler emitting machine-friendly (`human=false`)
 *      representation of the same data under `jss::state`.
 *  @see NetworkOPs::getServerInfo  The function that assembles the full response.
 */

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

json::Value
doServerInfo(RPC::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    ret[jss::info] = context.netOps.getServerInfo(
        true,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) && context.params[jss::counters].asBool());

    return ret;
}

}  // namespace xrpl
