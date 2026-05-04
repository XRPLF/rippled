#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <mutex>

namespace xrpl {

json::Value
doLedgerAccept(RPC::JsonContext& context)
{
    json::Value jvResult;

    if (!context.app.config().standalone())
    {
        jvResult[jss::error] = "notStandAlone";
    }
    else
    {
        std::unique_lock const lock{context.app.getMasterMutex()};
        context.netOps.acceptLedger();
        jvResult[jss::ledger_current_index] = context.ledgerMaster.getCurrentLedgerIndex();
    }

    return jvResult;
}

}  // namespace xrpl
