#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doBlackList(RPC::JsonContext& context)
{
    TRACE_FUNC();
    auto& rm = context.app.getResourceManager();
    if (context.params.isMember(jss::threshold))
    {
        return rm.getJson(context.params[jss::threshold].asInt());
    }

    return rm.getJson();
}

}  // namespace xrpl
