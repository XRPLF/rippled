#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   'ident' : <indent>,
// }
Json::Value
doOwnerInfo(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::account) &&
        !context.params.isMember(jss::ident))
    {
        return RPC::missing_field_error(jss::account);
    }

    std::string strIdent = context.params.isMember(jss::account)
        ? context.params[jss::account].asString()
        : context.params[jss::ident].asString();
    Json::Value ret;

    // Get info on account.
    auto const& closedLedger = context.ledgerMaster.getClosedLedger();
    std::optional<AccountID> const accountID = parseBase58<AccountID>(strIdent);
    ret[jss::accepted] = accountID.has_value()
        ? context.netOps.getOwnerInfo(closedLedger, accountID.value())
        : rpcError(rpcACT_MALFORMED);

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
    ret[jss::current] = accountID.has_value()
        ? context.netOps.getOwnerInfo(currentLedger, *accountID)
        : rpcError(rpcACT_MALFORMED);
    return ret;
}

}  // namespace ripple
