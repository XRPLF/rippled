/** @file
 *  Implements `doRipplePathFind`, the handler for the deprecated
 *  `ripple_path_find` JSON-RPC command.
 *
 *  The command provides a single synchronous-looking call that finds payment
 *  paths and returns them in the HTTP response. Its successor, `path_find`
 *  (`PathFind.cpp`), uses a WebSocket subscription model instead. This file
 *  is retained only for backwards compatibility.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <memory>
#include <utility>

namespace xrpl {

/** Handle the deprecated `ripple_path_find` one-shot pathfinding RPC command.
 *
 *  Finds one or more payment paths between two parties and returns the result
 *  synchronously in the HTTP response. The successor API is `path_find`, which
 *  uses a WebSocket subscription to push continuous updates; this handler
 *  remains only for backwards compatibility.
 *
 *  **Gate checks (cheapest first):**
 *  1. `PATH_SEARCH_MAX == 0` → `rpcNOT_SUPPORTED` (pathfinding administratively
 *     disabled).
 *  2. `getValidatedLedgerAge()` > `kMAX_VALIDATED_LEDGER_AGE` (2 min) →
 *     `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+). Only checked in
 *     the no-ledger (live-network) code path.
 *  3. `LegacyPathFind` admission control → `rpcTOO_BUSY` when the concurrent
 *     path-find ceiling (`kMAX_PATHFINDS_IN_PROGRESS = 2`) or job-queue depth
 *     (`kMAX_PATHFIND_JOB_COUNT = 50`) is exceeded. Only checked in the
 *     ledger-specified (synchronous) code path.
 *
 *  **Execution paths:**
 *
 *  - *No ledger specified* (networked mode only): Dispatches to the live
 *    `PathRequestManager` via `makeLegacyPathRequest()`, then cooperatively
 *    yields the `JobQueue::Coro` so the path-finding job can run on another
 *    thread. The handler resumes after the completion callback re-posts the
 *    coroutine, then calls `request->doStatus()` for the result. If
 *    `makeLegacyPathRequest()` returns a null request (job queue full at
 *    enqueue time), the error JSON it produced is returned directly without
 *    yielding.
 *
 *  - *Ledger specified*: Fully synchronous. Resolves the ledger with
 *    `RPC::lookupLedger()`, acquires a `LegacyPathFind` concurrency slot, and
 *    calls `PathRequestManager::doLegacyPathRequest()` inline. Ledger metadata
 *    already placed in `jvResult` by `lookupLedger()` is merged into the final
 *    result.
 *
 *  `loadType` is set to `kFEE_HEAVY_BURDEN_RPC` unconditionally; path-finding
 *  is one of the most compute-intensive operations on the RPC surface.
 *
 *  @note This function runs inside a `JobQueue::Coro` for the no-ledger path.
 *      Two shutdown failure modes exist: (1) `makeLegacyPathRequest()` returns
 *      null if the job queue rejects the path-find job; (2) the completion
 *      callback's `Coro::post()` fails if the queue stops accepting work after
 *      the path-find runs, in which case `Coro::resume()` is called instead so
 *      the coroutine drains on the path-finding thread rather than deadlocking.
 *  @note API v1 reports stale-ledger errors as `rpcNO_NETWORK`; API v2+
 *      consolidates to `rpcNOT_SYNCED`.
 *  @see `PathRequestManager::makeLegacyPathRequest()`,
 *      `PathRequestManager::doLegacyPathRequest()`, `RPC::LegacyPathFind`
 *  @deprecated Prefer `path_find` (WebSocket subscription) for new clients.
 *  @param context  RPC dispatch context carrying the parsed request parameters,
 *      resource consumer, coroutine handle, ledger master, and app handle.
 *  @return JSON result containing the discovered paths, or an `rpcError` object
 *      on any failure.
 */
json::Value
doRipplePathFind(RPC::JsonContext& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError(RpcNotSupported);

    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;

    std::shared_ptr<ReadView const> lpLedger;
    json::Value jvResult;

    if (!context.app.config().standalone() && !context.params.isMember(jss::ledger) &&
        !context.params.isMember(jss::ledger_index) && !context.params.isMember(jss::ledger_hash))
    {
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::kMAX_VALIDATED_LEDGER_AGE)
        {
            if (context.apiVersion == 1)
                return rpcError(RpcNoNetwork);
            return rpcError(RpcNotSynced);
        }

        PathRequest::pointer request;
        lpLedger = context.ledgerMaster.getClosedLedger();

        // Shutdown failure modes for the coroutine/JobQueue interaction:
        //
        // 1. makeLegacyPathRequest() returns null if the JobQueue rejected
        //    the path-find job (shutting down). The error JSON it produced
        //    is returned directly; we do not yield.
        //
        // 2. The path-find job runs but Coro::post() is rejected (JobQueue
        //    stopped accepting work). The completion lambda falls back to
        //    Coro::resume() so the coroutine drains on the path-finding
        //    thread rather than hanging the application on shutdown.
        jvResult = context.app.getPathRequestManager().makeLegacyPathRequest(
            request,
            [&context]() {
                // Copy the shared_ptr so the coroutine object stays alive
                // through the return from resume(); the captured reference
                // could otherwise evaporate beneath us.
                std::shared_ptr<JobQueue::Coro> const coroCopy{context.coro};
                if (!coroCopy->post())
                    coroCopy->resume();
            },
            context.consumer,
            lpLedger,
            context.params);
        if (request)
        {
            context.coro->yield();
            jvResult = request->doStatus(context.params);
        }

        return jvResult;
    }

    jvResult = RPC::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;

    RPC::LegacyPathFind const lpf(isUnlimited(context.role), context.app);
    if (!lpf.isOk())
        return rpcError(RpcTooBusy);

    auto result = context.app.getPathRequestManager().doLegacyPathRequest(
        context.consumer, lpLedger, context.params);

    for (auto& fieldName : jvResult.getMemberNames())
        result[fieldName] = std::move(jvResult[fieldName]);

    return result;
}

}  // namespace xrpl
