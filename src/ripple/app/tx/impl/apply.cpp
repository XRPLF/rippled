//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/applySteps.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>

namespace ripple {

std::pair<TER, bool>
apply(
    Application& app,
    OpenView& view,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    STAmountSO stAmountSO{view.rules().enabled(fixSTAmountCanonicalize)};
    NumberSO stNumberSO{view.rules().enabled(fixUniversalNumber)};

    auto pfresult = preflight(app, view.rules(), tx, flags, j);
    auto pcresult = preclaim(pfresult, app, view);
    return doApply(pcresult, app, view);
}

ApplyResult
applyTransaction(
    Application& app,
    OpenView& view,
    STTx const& txn,
    bool retryAssured,
    ApplyFlags flags,
    beast::Journal j)
{
    // Returns false if the transaction has need not be retried.
    if (retryAssured)
        flags = flags | tapRETRY;

    JLOG(j.trace()) << "TXN " << txn.getTransactionID()
                    << (retryAssured ? "/retry" : "/final");

    try
    {
        auto const result = apply(app, view, txn, flags, j);
        if (result.second)
        {
            JLOG(j.trace())
                << "Transaction applied: " << transHuman(result.first);
            return ApplyResult::Success;
        }

        if (isTefFailure(result.first) || isTemMalformed(result.first) ||
            isTelLocal(result.first))
        {
            // failure
            JLOG(j.trace())
                << "Transaction failure: " << transHuman(result.first);
            return ApplyResult::Fail;
        }

        JLOG(j.trace()) << "Transaction retry: " << transHuman(result.first);
        return ApplyResult::Retry;
    }
    catch (std::exception const& ex)
    {
        JLOG(j.trace()) << "Throws: " << ex.what();
        return ApplyResult::Fail;
    }
}

}  // namespace ripple
