#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

Json::Value
doLedgerCurrent(RPC::JsonContext& context)
{
    Json::Value jvResult;
    jvResult[jss::ledger_current_index] =
        context.ledgerMaster.getCurrentLedgerIndex();
    return jvResult;
}

}  // namespace xrpl
