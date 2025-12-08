#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>

namespace ripple {

Json::Value
doTxReduceRelay(RPC::JsonContext& context)
{
    return context.app.overlay().txMetrics();
}

}  // namespace ripple
