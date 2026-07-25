#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace xrpl {

// This interface is deprecated.
json::Value
doRipplePathFind(RPC::JsonContext& context)
{
    if (context.app.config().pathSearchMax == 0)
        return rpcError(RpcNotSupported);

    context.loadType = Resource::kFeeHeavyBurdenRpc;

    std::shared_ptr<ReadView const> lpLedger;
    json::Value jvResult;

    if (!context.app.config().standalone() && !context.params.isMember(jss::ledger) &&
        !context.params.isMember(jss::ledger_index) && !context.params.isMember(jss::ledger_hash))
    {
        // No ledger specified, use pathfinding defaults
        // and dispatch to pathfinding engine
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::kMaxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return rpcError(RpcNoNetwork);
            return rpcError(RpcNotSynced);
        }

        PathRequest::pointer request;
        lpLedger = context.ledgerMaster.getClosedLedger();

        // The wait below parks this JobQueue worker thread until the
        // path-finding continuation fires. The continuation is fired by a
        // JtUpdatePf job, which itself needs a free worker to run. Bound
        // the number of concurrently parked workers (LegacyPathFind admits
        // at most kMaxPathfindsInProgress non-admin requests) so that
        // concurrent ripple_path_find calls cannot occupy every worker and
        // stall the whole JobQueue. The guard must stay in scope until the
        // wait completes. (The old Boost.Coroutine implementation did not
        // need this: it suspended and released the worker instead of
        // blocking it.)
        RPC::LegacyPathFind const lpf(isUnlimited(context.role), context.app);
        if (!lpf.isOk())
            return rpcError(RpcTooBusy);

        // makeLegacyPathRequest enqueues a path-finding job that runs
        // asynchronously.  We block this thread with a condition_variable
        // until the path-finding continuation signals completion.
        // If makeLegacyPathRequest cannot schedule the job (e.g. during
        // shutdown), it returns an empty request and we skip the wait.
        // Replaces the old Coro yield/resume pattern with synchronous
        // blocking, eliminating shutdown race conditions.
        //
        // The state is shared with the continuation so that it stays alive
        // even if we stop waiting before the continuation runs.
        struct PathDone
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
        };
        auto const state = std::make_shared<PathDone>();

        jvResult = context.app.getPathRequestManager().makeLegacyPathRequest(
            request,
            [state]() {
                {
                    std::lock_guard const lk(state->mtx);
                    state->done = true;
                }
                state->cv.notify_one();
            },
            context.consumer,
            lpLedger,
            context.params);
        if (request)
        {
            using namespace std::chrono_literals;
            std::unique_lock lk(state->mtx);
            if (!state->cv.wait_for(lk, 30s, [&state] { return state->done; }))
            {
                // Path-finding continuation never fired (e.g. shutdown
                // race or unexpected failure). Return an internal error
                // rather than blocking the RPC thread indefinitely.
                return rpcError(RpcInternal);
            }
            lk.unlock();
            jvResult = request->doStatus(context.params);
        }

        return jvResult;
    }

    // The caller specified a ledger
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
