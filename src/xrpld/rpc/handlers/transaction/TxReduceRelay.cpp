#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doTxReduceRelay(RPC::JsonContext& context)
{
    TRACE_FUNC();
    return context.app.getOverlay().txMetrics();
}

}  // namespace xrpl
