#include <xrpld/app/ledger/LedgerCleaner.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

json::Value
doLedgerCleaner(rpc::JsonContext& context)
{
    context.app.getLedgerCleaner().clean(context.params);
    return rpc::makeObjectValue("Cleaner configured");
}

}  // namespace xrpl
