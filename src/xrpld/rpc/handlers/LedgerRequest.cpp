#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <variant>

namespace ripple {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value
doLedgerRequest(RPC::JsonContext& context)
{
    auto res = getLedgerByContext(context);

    if (std::holds_alternative<Json::Value>(res))
        return std::get<Json::Value>(res);

    auto const& ledger = std::get<std::shared_ptr<Ledger const>>(res);

    Json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->info().seq;
    addJson(jvResult, {*ledger, &context, 0});
    return jvResult;
}

}  // namespace ripple
