#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>

namespace ripple {

Json::Value
doValidatorListSites(RPC::JsonContext& context)
{
    return context.app.validatorSites().getJson();
}

}  // namespace ripple
