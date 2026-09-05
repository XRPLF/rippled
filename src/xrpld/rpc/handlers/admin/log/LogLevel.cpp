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

json::Value
doLogLevel(rpc::JsonContext& context)
{
    // log_level
    if (not context.params.isMember(jss::severity))
    {
        // get log severities
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

    // Guard the conversion: json::Value::asString() throws for arrays and objects,
    // which would surface as a generic internal error instead of invalid parameters,
    // and silently stringifies scalars, so a number or a bool would be matched
    // against the severity names as its printed form.
    auto const& jvSeverity = context.params[jss::severity];
    if (!jvSeverity.isString())
        return rpcError(RpcInvalidParams);

    auto const severity = Logs::fromString(jvSeverity.asString());

    if (not severity.has_value())
        return rpcError(RpcInvalidParams);

    // log_level severity
    if (not context.params.isMember(jss::partition))
    {
        // set base log threshold
        context.app.getLogs().threshold(*severity);
        return json::ValueType::Object;
    }

    // log_level partition severity base?
    // Guard the conversion for the same reason, and reject the empty name: Logs::get
    // creates a partition on demand, so any value that is not a real partition name
    // adds a junk sink that this command then reports forever.
    auto const& jvPartition = context.params[jss::partition];
    if (!jvPartition.isString() || jvPartition.asString().empty())
        return rpcError(RpcInvalidParams);

    // set partition threshold
    std::string const partition(jvPartition.asString());

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

}  // namespace xrpl
