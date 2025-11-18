#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <mutex>

namespace ripple {

Json::Value
doLedgerAccept(RPC::JsonContext& context)
{
    Json::Value jvResult;

    if (!context.app.config().standalone())
    {
        jvResult[jss::error] = "notStandAlone";
    }
    else
    {
        std::unique_lock lock{context.app.getMasterMutex()};
        context.netOps.acceptLedger();
        jvResult[jss::ledger_current_index] =
            context.ledgerMaster.getCurrentLedgerIndex();
    }

    return jvResult;
}

}  // namespace ripple
