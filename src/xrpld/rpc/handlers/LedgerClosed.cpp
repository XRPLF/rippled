#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doLedgerClosed(RPC::JsonContext& context)
{
    auto ledger = context.ledgerMaster.getClosedLedger();
    XRPL_ASSERT(ledger, "ripple::doLedgerClosed : non-null closed ledger");

    Json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->info().seq;
    jvResult[jss::ledger_hash] = to_string(ledger->info().hash);

    return jvResult;
}

}  // namespace ripple
