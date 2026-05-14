/** @file
 *  Implements `doLogLevel`, the handler for the `log_level` admin RPC command.
 *
 *  Operators use this command at runtime to query or change log verbosity
 *  thresholds across all logging partitions — or a single named one — without
 *  restarting the node.  The companion command `logrotate` (implemented in
 *  `LogRotate.cpp`) completes the set of runtime log-management primitives.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/Log.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/predicate.hpp>

#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Query or set runtime log-verbosity thresholds (ADMIN).
 *
 *  Dispatches across three modes based on the shape of the request:
 *
 *  - **Query** (no `severity` field): returns a `levels` JSON object whose
 *    `base` key holds the global threshold and whose remaining keys are the
 *    per-partition thresholds for every named subsystem (e.g. `Ledger`,
 *    `Consensus`).
 *  - **Global set** (`severity` only, no `partition`): sets the global base
 *    threshold; all partitions without an individual override are filtered at
 *    this level from that point forward.
 *  - **Partition set** (`severity` + `partition`): sets the threshold on a
 *    single named partition.  The name `"base"` (case-insensitive) is an alias
 *    for the global threshold and is handled identically to the global-set
 *    mode.
 *
 *  **Severity conversion:** the raw string is validated by `Logs::fromString()`
 *  and converted to `beast::severities::Severity` via `Logs::toSeverity()`.
 *  An unrecognised string causes an immediate `rpcINVALID_PARAMS` error rather
 *  than silently clamping to a default.
 *
 *  @param context  RPC dispatch context carrying `params` and `app` references.
 *  @return In query mode, a JSON object with a `levels` key.  In set modes, an
 *      empty JSON object on success, or an `rpcINVALID_PARAMS` error object for
 *      an unrecognised severity string.
 *
 *  @note Unknown partition names are silently accepted: `Logs::get()` creates a
 *      new sink on demand, so a misspelled partition name creates an isolated
 *      threshold entry rather than returning an error.
 *  @note The `"base"` partition alias comparison uses `boost::iequals`, matching
 *      the case-insensitive storage key comparison used internally by `Logs`.
 */
json::Value
doLogLevel(RPC::JsonContext& context)
{
    if (not context.params.isMember(jss::severity))
    {
        json::Value ret(json::ValueType::Object);
        json::Value lev(json::ValueType::Object);

        lev[jss::base] = Logs::toString(context.app.getLogs().threshold());
        std::vector<std::pair<std::string, std::string>> const logTable(
            context.app.getLogs().partitionSeverities());
        for (auto const& [k, v] : logTable)
            lev[k] = v;

        ret[jss::levels] = lev;
        return ret;
    }

    auto const severity = Logs::fromString(context.params[jss::severity].asString());

    if (not severity.has_value())
        return rpcError(RpcInvalidParams);

    if (not context.params.isMember(jss::partition))
    {
        context.app.getLogs().threshold(*severity);
        return json::ValueType::Object;
    }

    if (context.params.isMember(jss::partition))
    {
        std::string const partition(context.params[jss::partition].asString());

        if (boost::iequals(partition, "base"))
        {
            context.app.getLogs().threshold(*severity);
        }
        else
        {
            context.app.getLogs().get(partition).threshold(*severity);
        }

        return json::ValueType::Object;
    }

    return rpcError(RpcInvalidParams);
}

}  // namespace xrpl
