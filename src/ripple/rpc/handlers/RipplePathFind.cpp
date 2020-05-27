//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/net/RPCErr.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// This interface is deprecated.
Json::Value
doRipplePathFind(RPC::JsonContext& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError(rpcNOT_SUPPORTED);

    context.loadType = Resource::feeHighBurdenRPC;

    std::shared_ptr<ReadView const> lpLedger;
    Json::Value jvResult;

    if (!context.app.config().standalone() &&
        !context.params.isMember(jss::ledger) &&
        !context.params.isMember(jss::ledger_index) &&
        !context.params.isMember(jss::ledger_hash))
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

        // It doesn't look like there's much odd happening here, but you should
        // be aware this code runs in a JobQueue::Coro, which is a coroutine.
        // And we may be flipping around between threads.  Here's an overview:
        //
        // 1. We're running doRipplePathFind() due to a call to
        //    ripple_path_find.  doRipplePathFind() is currently running
        //    inside of a JobQueue::Coro using a JobQueue thread.
        //
        // 2. doRipplePathFind's call to makeLegacyPathRequest() enqueues the
        //    path-finding request.  That request will (probably) run at some
        //    indeterminate future time on a (probably different) JobQueue
        //    thread.
        //
        // 3. As a continuation from that path-finding JobQueue thread, the
        //    coroutine we're currently running in (!) is posted to the
        //    JobQueue.  Because it is a continuation, that post won't
        //    happen until the path-finding request completes.
        //
        // 4. Once the continuation is enqueued, and we have reason to think
        //    the path-finding job is likely to run, then the coroutine we're
        //    running in yield()s.  That means it surrenders its thread in
        //    the JobQueue.  The coroutine is suspended, but ready to run,
        //    because it is kept resident by a shared_ptr in the
        //    path-finding continuation.
        //
        // 5. If all goes well then path-finding runs on a JobQueue thread
        //    and executes its continuation.  The continuation posts this
        //    same coroutine (!) to the JobQueue.
        //
        // 6. When the JobQueue calls this coroutine, this coroutine resumes
        //    from the line below the coro->yield() and returns the
        //    path-finding result.
        //
        // With so many moving parts, what could go wrong?
        //
        // Just in terms of the JobQueue refusing to add jobs at shutdown
        // there are two specific things that can go wrong.
        //
        // 1. The path-finding Job queued by makeLegacyPathRequest() might be
        //    rejected (because we're shutting down).
        //
        //    Fortunately this problem can be addressed by looking at the
        //    return value of makeLegacyPathRequest().  If
        //    makeLegacyPathRequest() cannot get a thread to run the path-find
        //    on, then it returns an empty request.
        //
        // 2. The path-finding job might run, but the Coro::post() might be
        //    rejected by the JobQueue (because we're shutting down).
        //
        //    We handle this case by resuming (not posting) the Coro.
        //    By resuming the Coro, we allow the Coro to run to completion
        //    on the current thread instead of requiring that it run on a
        //    new thread from the JobQueue.
        //
        // Both of these failure modes are hard to recreate in a unit test
        // because they are so dependent on inter-thread timing.  However
        // the failure modes can be observed by synchronously (inside the
        // rippled source code) shutting down the application.  The code to
        // do so looks like this:
        //
        //   context.app.signalStop();
        //   while (! context.app.getJobQueue().jobCounter().joined()) { }
        //
        // The first line starts the process of shutting down the app.
        // The second line waits until no more jobs can be added to the
        // JobQueue before letting the thread continue.
        //
        // May 2017
        jvResult = context.app.getPathRequests().makeLegacyPathRequest(
            request,
            [&context]() {
                // Copying the shared_ptr keeps the coroutine alive up
                // through the return.  Otherwise the storage under the
                // captured reference could evaporate when we return from
                // coroCopy->resume().  This is not strictly necessary, but
                // will make maintenance easier.
                std::shared_ptr<JobQueue::Coro> coroCopy{context.coro};
                if (!coroCopy->post())
                {
                    // The post() failed, so we won't get a thread to let
                    // the Coro finish.  We'll call Coro::resume() so the
                    // Coro can finish on our thread.  Otherwise the
                    // application will hang on shutdown.
                    coroCopy->resume();
                }
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

}  // namespace ripple
