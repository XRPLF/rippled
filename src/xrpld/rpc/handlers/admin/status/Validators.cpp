#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

Json::Value
doValidators(RPC::JsonContext& context)
{
    return context.app.getValidators().getJson();
}

}  // namespace xrpl
