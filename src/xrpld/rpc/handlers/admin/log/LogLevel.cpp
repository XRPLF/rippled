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

    auto const severity = Logs::fromString(context.params[jss::severity].asString());

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
    if (context.params.isMember(jss::partition))
    {
        // set partition threshold
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
