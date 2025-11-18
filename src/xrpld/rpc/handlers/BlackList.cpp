#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/jss.h>
#include <xrpl/resource/ResourceManager.h>

namespace ripple {

Json::Value
doBlackList(RPC::JsonContext& context)
{
    auto& rm = context.app.getResourceManager();
    if (context.params.isMember(jss::threshold))
        return rm.getJson(context.params[jss::threshold].asInt());
    else
        return rm.getJson();
}

}  // namespace ripple
