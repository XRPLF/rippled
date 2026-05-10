#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
json::Value
doLedgerRequest(RPC::JsonContext& context)
{
    TRACE_FUNC();
    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
    auto res = RPC::getOrAcquireLedger(context);

    if (!res.has_value())
        return res.error();

    auto const& ledger = res.value();

    json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->header().seq;
    addJson(jvResult, {*ledger, &context, 0});
    return jvResult;
}

}  // namespace xrpl
