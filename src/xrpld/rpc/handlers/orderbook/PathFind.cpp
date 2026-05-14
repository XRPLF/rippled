/** @file
 *  Implements `doPathFind`, the RPC entry point for the `path_find` command.
 *
 *  `path_find` is a subscription-oriented command: it registers a long-lived
 *  payment-path request on a WebSocket connection and pushes updated results
 *  after every ledger close. The three subcommands (`create`, `close`,
 *  `status`) map directly onto the `PathRequestManager` / `PathRequest` /
 *  `InfoSub` lifecycle.
 */

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

/** Dispatch a `path_find` subcommand for a persistent pathfinding subscription.
 *
 *  Handles the three subcommands of the `path_find` WebSocket API:
 *
 *  - **`create`** — registers a new path-find request on the current
 *    subscriber, replacing any existing one (`clearRequest` is called first).
 *    Charges `kFEE_HEAVY_BURDEN_RPC` because path search traverses order books
 *    and trust lines across the full ledger graph. The closed (validated)
 *    ledger is used for the initial computation to ensure a stable view.
 *    Subsequent updates are pushed asynchronously by `PathRequestManager`
 *    after each ledger close.
 *  - **`close`** — tears down the active request and clears it from the
 *    subscriber. No resource charge; cost was already borne at `create` time.
 *  - **`status`** — returns the most recently computed path result without
 *    triggering a new computation. No resource charge.
 *
 *  Validations are applied cheapest-first:
 *  1. `PATH_SEARCH_MAX == 0` → `rpcNOT_SUPPORTED` (admin-disabled pathfinding).
 *  2. Missing or non-string `subcommand` field → `rpcINVALID_PARAMS`.
 *  3. No active `infoSub` (non-WebSocket transport) → `rpcNO_EVENTS`.
 *  4. No live `InfoSubRequest` on `close`/`status` → `rpcNO_PF_REQUEST`.
 *  5. Unrecognised subcommand string → `rpcINVALID_PARAMS`.
 *
 *  The negotiated API version is propagated to the subscriber via
 *  `setApiVersion` before dispatch so that asynchronous push updates are
 *  formatted according to the client's negotiated version.
 *
 *  @note Only WebSocket transports carry an `infoSub`; HTTP callers always
 *      receive `rpcNO_EVENTS` because there is no persistent channel over
 *      which to push path updates.
 *  @note `InfoSub` holds at most one active `InfoSubRequest` at a time.
 *      Issuing a second `create` without a `close` silently replaces the
 *      prior request.
 *  @param context  RPC dispatch context, including the subscriber, ledger
 *      master, app handle, and parsed request parameters.
 *  @return JSON response for the subcommand: an initial path result for
 *      `create`, a close confirmation for `close`, the cached result for
 *      `status`, or an `rpcError` object on failure.
 */
json::Value
doPathFind(RPC::JsonContext& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
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
        context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;
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
