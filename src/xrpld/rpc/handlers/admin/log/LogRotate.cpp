#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

json::Value
doLogRotate(RPC::JsonContext& context)
{
    context.app.getPerfLog().rotate();
    return RPC::makeObjectValue(context.app.getLogs().rotate());
}

}  // namespace xrpl
