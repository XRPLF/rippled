#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doServerState(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);

    ret[jss::state] = context.netOps.getServerInfo(
        false,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) &&
            context.params[jss::counters].asBool());

    return ret;
}

}  // namespace ripple
