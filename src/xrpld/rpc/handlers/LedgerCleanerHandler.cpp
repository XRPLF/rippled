#include <xrpld/app/ledger/LedgerCleaner.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>

namespace ripple {

Json::Value
doLedgerCleaner(RPC::JsonContext& context)
{
    context.app.getLedgerCleaner().clean(context.params);
    return RPC::makeObjectValue("Cleaner configured");
}

}  // namespace ripple
