#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

Json::Value
doPathFind(RPC::JsonContext& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError(rpcNOT_SUPPORTED);

    auto lpLedger = context.ledgerMaster.getClosedLedger();

    if (!context.params.isMember(jss::subcommand) ||
        !context.params[jss::subcommand].isString())
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (!context.infoSub)
        return rpcError(rpcNO_EVENTS);

    context.infoSub->setApiVersion(context.apiVersion);

    auto sSubCommand = context.params[jss::subcommand].asString();

    if (sSubCommand == "create")
    {
        context.loadType = Resource::feeHeavyBurdenRPC;
        context.infoSub->clearRequest();
        return context.app.getPathRequests().makePathRequest(
            context.infoSub, lpLedger, context.params);
    }

    if (sSubCommand == "close")
    {
        InfoSubRequest::pointer request = context.infoSub->getRequest();

        if (!request)
            return rpcError(rpcNO_PF_REQUEST);

        context.infoSub->clearRequest();
        return request->doClose();
    }

    if (sSubCommand == "status")
    {
        InfoSubRequest::pointer request = context.infoSub->getRequest();

        if (!request)
            return rpcError(rpcNO_PF_REQUEST);

        return request->doStatus(context.params);
    }

    return rpcError(rpcINVALID_PARAMS);
}

}  // namespace ripple
