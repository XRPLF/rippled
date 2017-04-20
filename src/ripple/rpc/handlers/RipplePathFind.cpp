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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/core/JobQueue.h>
#include <ripple/net/RPCErr.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// This interface is deprecated.
Json::Value doRipplePathFind (RPC::Context& context)
{
    if (context.app.config().PATH_SEARCH_MAX == 0)
        return rpcError (rpcNOT_SUPPORTED);

    context.loadType = Resource::feeHighBurdenRPC;

    std::shared_ptr <ReadView const> lpLedger;
    Json::Value jvResult;

    if (! context.app.config().standalone() &&
        ! context.params.isMember(jss::ledger) &&
        ! context.params.isMember(jss::ledger_index) &&
        ! context.params.isMember(jss::ledger_hash))
    {
        // No ledger specified, use pathfinding defaults
        // and dispatch to pathfinding engine
        if (context.app.getLedgerMaster().getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            return rpcError (rpcNO_NETWORK);
        }

        PathRequest::pointer request;
        lpLedger = context.ledgerMaster.getClosedLedger();

        jvResult = context.app.getPathRequests().makeLegacyPathRequest (
            request, std::bind(&JobQueue::Coro::post, context.coro),
                context.consumer, lpLedger, context.params);
        if (request)
        {
            context.coro->yield();
            jvResult = request->doStatus (context.params);
        }

        return jvResult;
    }

    // The caller specified a ledger
    jvResult = RPC::lookupLedger (lpLedger, context);
    if (! lpLedger)
        return jvResult;

    RPC::LegacyPathFind lpf (isUnlimited (context.role), context.app);
    if (! lpf.isOk ())
        return rpcError (rpcTOO_BUSY);

    auto result = context.app.getPathRequests().doLegacyPathRequest (
        context.consumer, lpLedger, context.params);

    for (auto &fieldName : jvResult.getMemberNames ())
        result[fieldName] = std::move (jvResult[fieldName]);

    return result;
}

} // ripple
