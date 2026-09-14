#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SystemParameters.h>

namespace xrpl {

namespace rpc {
struct JsonContext;
}  // namespace rpc

json::Value
doStop(rpc::JsonContext& context)
{
    context.app.signalStop("RPC");
    return rpc::makeObjectValue(systemName() + " server stopping");
}

}  // namespace xrpl
