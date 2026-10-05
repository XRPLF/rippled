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

/**
 * Serves `path_find`: `create` opens a path request that pushes updates to the
 * connection, recording the API version this request named on it, `close` ends
 * the connection's open request and `status` reports it.
 *
 * @param context The request and the connection it arrived on.
 * @return The subcommand's result, or an error when pathfinding is disabled,
 *         the subcommand is missing or unknown, no connection can receive
 *         updates, or there is no open request to close or report.
 */
json::Value
doPathFind(rpc::JsonContext& context)
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

    auto sSubCommand = context.params[jss::subcommand].asString();

    if (sSubCommand == "create")
    {
        context.loadType = resource::kFeeHeavyBurdenRpc;
        context.infoSub->clearRequest();
        return context.app.getPathRequestManager().makePathRequest(
            context.infoSub, lpLedger, context.params, context.apiVersion);
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
