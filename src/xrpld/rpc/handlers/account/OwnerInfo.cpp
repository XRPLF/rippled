#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <optional>
#include <string>

namespace xrpl {
// {
//   'ident' : <ident>,
// }
json::Value
doOwnerInfo(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::account) && !context.params.isMember(jss::ident))
    {
        return RPC::missingFieldError(jss::account);
    }
    std::string strIdent;
    if (context.params.isMember(jss::account))
    {
        if (!context.params[jss::account].isString())
            return RPC::invalidFieldError(jss::account);

        strIdent = context.params[jss::account].asString();
    }
    else
    {
        if (!context.params[jss::ident].isString())
            return RPC::invalidFieldError(jss::ident);

        strIdent = context.params[jss::ident].asString();
    }
    json::Value ret;

    // Get info on account.
    auto const& closedLedger = context.ledgerMaster.getClosedLedger();
    std::optional<AccountID> const accountID = parseBase58<AccountID>(strIdent);
    ret[jss::accepted] = accountID.has_value()
        ? context.netOps.getOwnerInfo(closedLedger, accountID.value())
        : rpcError(RpcActMalformed);

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
    ret[jss::current] = accountID.has_value()
        ? context.netOps.getOwnerInfo(currentLedger, *accountID)
        : rpcError(RpcActMalformed);
    return ret;
}

}  // namespace xrpl
