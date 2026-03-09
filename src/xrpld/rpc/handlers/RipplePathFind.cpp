#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/protocol/RPCErr.h>
#include <xrpl/resource/Fees.h>

#include <condition_variable>
#include <mutex>

namespace xrpl {

// This interface is deprecated.
Json::Value
doRipplePathFind(RPC::JsonContext& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError(rpcNOT_SUPPORTED);

    context.loadType = Resource::feeHeavyBurdenRPC;

    std::shared_ptr<ReadView const> lpLedger;
    Json::Value jvResult;

    if (!context.app.config().standalone() && !context.params.isMember(jss::ledger) &&
        !context.params.isMember(jss::ledger_index) && !context.params.isMember(jss::ledger_hash))
    {
        // No ledger specified, use pathfinding defaults
        // and dispatch to pathfinding engine
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return rpcError(rpcNO_NETWORK);
            return rpcError(rpcNOT_SYNCED);
        }

        PathRequest::pointer request;
        lpLedger = context.ledgerMaster.getClosedLedger();

        // makeLegacyPathRequest enqueues a path-finding job that runs
        // asynchronously.  We block this thread with a condition_variable
        // until the path-finding continuation signals completion.
        // If makeLegacyPathRequest cannot schedule the job (e.g. during
        // shutdown), it returns an empty request and we skip the wait.
        // Replaces the old Coro yield/resume pattern with synchronous
        // blocking, eliminating shutdown race conditions.
        std::mutex mtx;
        std::condition_variable cv;
        bool pathDone = false;

        jvResult = context.app.getPathRequests().makeLegacyPathRequest(
            request,
            [&]() {
                {
                    std::lock_guard lk(mtx);
                    pathDone = true;
                }
                cv.notify_one();
            },
            context.consumer,
            lpLedger,
            context.params);
        if (request)
        {
            using namespace std::chrono_literals;
            std::unique_lock lk(mtx);
            if (!cv.wait_for(lk, 30s, [&] { return pathDone; }))
            {
                // Path-finding continuation never fired (e.g. shutdown
                // race or unexpected failure). Return an internal error
                // rather than blocking the RPC thread indefinitely.
                return rpcError(rpcINTERNAL);
            }
            jvResult = request->doStatus(context.params);
        }

        return jvResult;
    }

    // The caller specified a ledger
    jvResult = RPC::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;

    RPC::LegacyPathFind lpf(isUnlimited(context.role), context.app);
    if (!lpf.isOk())
        return rpcError(rpcTOO_BUSY);

    auto result = context.app.getPathRequests().doLegacyPathRequest(
        context.consumer, lpLedger, context.params);

    for (auto& fieldName : jvResult.getMemberNames())
        result[fieldName] = std::move(jvResult[fieldName]);

    return result;
}

}  // namespace xrpl
