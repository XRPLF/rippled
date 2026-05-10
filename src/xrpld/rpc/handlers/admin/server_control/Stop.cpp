#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

namespace RPC {
struct JsonContext;
}  // namespace RPC

json::Value
doStop(RPC::JsonContext& context)
{
    TRACE_FUNC();
    context.app.signalStop("RPC");
    return RPC::makeObjectValue(systemName() + " server stopping");
}

}  // namespace xrpl
