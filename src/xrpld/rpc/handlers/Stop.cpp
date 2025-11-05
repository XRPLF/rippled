#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>

namespace ripple {

namespace RPC {
struct JsonContext;
}

Json::Value
doStop(RPC::JsonContext& context)
{
    context.app.signalStop("RPC");
    return RPC::makeObjectValue(systemName() + " server stopping");
}

}  // namespace ripple
