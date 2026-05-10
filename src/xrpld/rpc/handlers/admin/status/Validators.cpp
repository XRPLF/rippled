#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doValidators(RPC::JsonContext& context)
{
    TRACE_FUNC();
    return context.app.getValidators().getJson();
}

}  // namespace xrpl
