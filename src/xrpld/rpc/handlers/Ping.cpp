#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace RPC {
struct JsonContext;
}  // namespace RPC

Json::Value
doPing(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);
    switch (context.role)
    {
        case Role::ADMIN:
            ret[jss::role] = "admin";
            break;
        case Role::IDENTIFIED:
            ret[jss::role] = "identified";
            ret[jss::username] = std::string{context.headers.user};
            if (context.headers.forwardedFor.size())
                ret[jss::ip] = std::string{context.headers.forwardedFor};
            break;
        case Role::PROXY:
            ret[jss::role] = "proxied";
            ret[jss::ip] = std::string{context.headers.forwardedFor};
        default:;
    }

    // This is only accessible on ws sessions.
    if (context.infoSub)
    {
        if (context.infoSub->getConsumer().isUnlimited())
            ret[jss::unlimited] = true;
    }

    return ret;
}

}  // namespace ripple
