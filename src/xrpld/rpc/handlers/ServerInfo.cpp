#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

Json::Value
doServerInfo(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);

    ret[jss::info] = context.netOps.getServerInfo(
        true,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) &&
            context.params[jss::counters].asBool());

    return ret;
}

}  // namespace xrpl
