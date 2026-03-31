#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>

namespace xrpl {

Json::Value
doValidatorListSites(RPC::JsonContext& context)
{
    return context.app.getValidatorSites().getJson();
}

}  // namespace xrpl
