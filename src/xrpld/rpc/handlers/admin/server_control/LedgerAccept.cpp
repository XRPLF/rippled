#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <mutex>

namespace xrpl {

/**
 * Closes the open ledger on a stand-alone server.
 *
 * @param context The request. Its `params` are not read.
 * @return `ledger_current_index` of the ledger the close opens, or
 *         `notStandAlone` when the server is on a network.
 */
json::Value
doLedgerAccept(rpc::JsonContext& context)
{
    json::Value jvResult;

    if (!context.app.config().standalone())
    {
        rpc::injectError(RpcNotStandAlone, jvResult);
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
