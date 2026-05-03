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
doLogLevel(RPC::JsonContext& context)
{
    // log_level
    if (!context.params.isMember(jss::severity))
    {
        // get log severities
        json::Value ret(json::ObjectValue);
        json::Value lev(json::ObjectValue);

        lev[jss::base] = Logs::toString(Logs::fromSeverity(context.app.getLogs().threshold()));
        std::vector<std::pair<std::string, std::string>> const logTable(
            context.app.getLogs().partitionSeverities());
        for (auto const& [k, v] : logTable)
            lev[k] = v;

        ret[jss::levels] = lev;
        return ret;
    }

    LogSeverity const sv(Logs::fromString(context.params[jss::severity].asString()));

    if (sv == LSInvalid)
        return rpcError(RpcInvalidParams);

    auto severity = Logs::toSeverity(sv);
    // log_level severity
    if (!context.params.isMember(jss::partition))
    {
        // set base log threshold
        context.app.getLogs().threshold(severity);
        return json::ObjectValue;
    }

    // log_level partition severity base?
    if (context.params.isMember(jss::partition))
    {
        // set partition threshold
        std::string const partition(context.params[jss::partition].asString());

        if (boost::iequals(partition, "base"))
        {
            context.app.getLogs().threshold(severity);
        }
        else
        {
            context.app.getLogs().get(partition).threshold(severity);
        }

        return json::ObjectValue;
    }

    return rpcError(RpcInvalidParams);
}

}  // namespace xrpl
