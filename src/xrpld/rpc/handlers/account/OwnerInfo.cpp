#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

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
doOwnerInfo(rpc::JsonContext& context)
{
    if (!context.params.isMember(jss::account) && !context.params.isMember(jss::ident))
    {
        return rpc::missingFieldError(jss::account);
    }
    std::string strIdent;
    if (context.params.isMember(jss::account))
    {
        if (!context.params[jss::account].isString())
            return rpc::invalidFieldError(jss::account);

        strIdent = context.params[jss::account].asString();
    }
    else
    {
        if (!context.params[jss::ident].isString())
            return rpc::invalidFieldError(jss::ident);

        strIdent = context.params[jss::ident].asString();
    }

    json::Value ret;

    std::optional<AccountID> const accountID = parseBase58<AccountID>(strIdent);

    if (!accountID)
    {
        ret[jss::accepted] = rpcError(RpcActMalformed);
        ret[jss::current] = rpcError(RpcActMalformed);
        return ret;
    }

    bool const hasLedgerSelector = context.params.isMember(jss::ledger) ||
        context.params.isMember(jss::ledger_hash) || context.params.isMember(jss::ledger_index);

    if (hasLedgerSelector)
    {
        std::shared_ptr<ReadView const> ledger;
        auto result = rpc::lookupLedger(ledger, context);

        if (!ledger)
            return result;

        ret[jss::accepted] = context.netOps.getOwnerInfo(*ledger, *accountID);
    }
    else
    {
        auto const& closedLedger = context.ledgerMaster.getClosedLedger();
        ret[jss::accepted] = context.netOps.getOwnerInfo(closedLedger, *accountID);

        auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
        ret[jss::current] = context.netOps.getOwnerInfo(currentLedger, *accountID);
    }

    return ret;
}

}  // namespace xrpl
