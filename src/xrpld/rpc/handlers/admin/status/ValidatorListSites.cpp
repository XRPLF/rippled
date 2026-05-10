#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doValidatorListSites(RPC::JsonContext& context)
{
    TRACE_FUNC();
    return context.app.getValidatorSites().getJson();
}

}  // namespace xrpl
