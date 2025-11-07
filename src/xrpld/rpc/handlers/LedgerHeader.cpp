#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value
doLedgerHeader(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    Serializer s;
    addRaw(lpLedger->info(), s);
    jvResult[jss::ledger_data] = strHex(s.peekData());

    // This information isn't verified: they should only use it if they trust
    // us.
    addJson(jvResult, {*lpLedger, &context, 0});

    return jvResult;
}

}  // namespace ripple
