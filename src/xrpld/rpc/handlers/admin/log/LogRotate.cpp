#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doLogRotate(RPC::JsonContext& context)
{
    TRACE_FUNC();
    context.app.getPerfLog().rotate();
    return RPC::makeObjectValue(context.app.getLogs().rotate());
}

}  // namespace xrpl
