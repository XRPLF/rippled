#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/InfoSub.h>

namespace xrpl {

json::Value
doPathFind(RPC::JsonContext& context)
{
    if (context.app.config().pathSearchMax == 0)
        return rpcError(RpcNotSupported);

    auto lpLedger = context.ledgerMaster.getClosedLedger();

    if (!context.params.isMember(jss::subcommand) || !context.params[jss::subcommand].isString())
    {
        return rpcError(RpcInvalidParams);
    }

    if (!context.infoSub)
        return rpcError(RpcNoEvents);

    context.infoSub->setApiVersion(context.apiVersion);

    auto sSubCommand = context.params[jss::subcommand].asString();

    if (sSubCommand == "create")
    {
        context.loadType = Resource::kFeeHeavyBurdenRpc;
        context.infoSub->clearRequest();
        return context.app.getPathRequestManager().makePathRequest(
            context.infoSub, lpLedger, context.params);
    }

    if (sSubCommand == "close")
    {
        InfoSubRequest::pointer const request = context.infoSub->getRequest();

        if (!request)
            return rpcError(RpcNoPfRequest);

        context.infoSub->clearRequest();
        return request->doClose();
    }

    if (sSubCommand == "status")
    {
        InfoSubRequest::pointer const request = context.infoSub->getRequest();

        if (!request)
            return rpcError(RpcNoPfRequest);

        return request->doStatus(context.params);
    }

    return rpcError(RpcInvalidParams);
}

}  // namespace xrpl
