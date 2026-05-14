/** @file
 *  Implements the `consensus_info` admin RPC handler.
 *
 *  Registered in Handler.cpp as `Role::ADMIN` with `NO_CONDITION`, so the
 *  handler runs regardless of sync state and is unreachable by non-admin
 *  connections. The command accepts no parameters; any extra arguments are
 *  rejected by the RPC framework before dispatch.
 */

#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

/** Snapshot the live consensus engine state for an admin caller.
 *
 *  Delegates to `NetworkOPs::getConsensusInfo()`, which calls
 *  `Consensus::getJson(true)` — the verbose path that includes the current
 *  consensus phase, dispute sets, proposal counts, and timing fields in
 *  addition to the summary fields present in the non-verbose form.
 *
 *  @param context RPC dispatch envelope carrying the `NetworkOPs` reference.
 *  @return JSON object with a single `"info"` key whose value is the verbose
 *      consensus snapshot.
 */
json::Value
doConsensusInfo(RPC::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    ret[jss::info] = context.netOps.getConsensusInfo();

    return ret;
}

}  // namespace xrpl
