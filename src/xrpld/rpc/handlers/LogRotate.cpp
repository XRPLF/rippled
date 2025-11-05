#include <xrpld/app/main/Application.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/Log.h>

namespace ripple {

Json::Value
doLogRotate(RPC::JsonContext& context)
{
    context.app.getPerfLog().rotate();
    return RPC::makeObjectValue(context.app.logs().rotate());
}

}  // namespace ripple
