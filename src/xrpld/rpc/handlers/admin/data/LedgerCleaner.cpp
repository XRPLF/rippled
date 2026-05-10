#include <xrpld/app/ledger/LedgerCleaner.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doLedgerCleaner(RPC::JsonContext& context)
{
    TRACE_FUNC();
    context.app.getLedgerCleaner().clean(context.params);
    return RPC::makeObjectValue("Cleaner configured");
}

}  // namespace xrpl
