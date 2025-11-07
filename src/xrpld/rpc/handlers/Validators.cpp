#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>

namespace ripple {

Json::Value
doValidators(RPC::JsonContext& context)
{
    return context.app.validators().getJson();
}

}  // namespace ripple
