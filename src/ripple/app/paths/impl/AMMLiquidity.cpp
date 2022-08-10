//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/paths/AMMLiquidity.h>

namespace ripple {

AMMLiquidity::AMMLiquidity(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::uint32_t tradingFee,
    Issue const& in,
    Issue const& out,
    AMMOfferCounter const& offerCounter,
    beast::Journal j)
    : offerCounter_(offerCounter)
    , ammAccountID_(ammAccountID)
    , tradingFee_(tradingFee)
    , balances_{STAmount{in}, STAmount{out}}
    , fibSeqHelper_{std::nullopt}
    , dirty_(true)
    , j_(j)
{
    balances_ = fetchBalances(view);
}

STAmount
AMMLiquidity::ammAccountHolds(
    const ReadView& view,
    AccountID const& ammAccountID,
    const Issue& issue) const
{
    if (isXRP(issue))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return sle->getFieldAmount(sfBalance);
    }
    else if (auto const sle = view.read(
                 keylet::line(ammAccountID, issue.account, issue.currency));
             !isFrozen(view, ammAccountID, issue.currency, issue.account))
    {
        auto amount = sle->getFieldAmount(sfBalance);
        if (amount.negative())
            amount.negate();
        amount.setIssuer(issue.account);
        return amount;
    }

    return STAmount{issue};
}

Amounts
AMMLiquidity::fetchBalances(const ReadView& view) const
{
    if (dirty_)
    {
        auto const assetIn =
            ammAccountHolds(view, ammAccountID_, balances_.in.issue());
        auto const assetOut =
            ammAccountHolds(view, ammAccountID_, balances_.out.issue());
        // This should not happen since AMMLiquidity is created only
        // if AMM exists for the given token pair.
        if (assetIn <= beast::zero || assetOut <= beast::zero)
            Throw<std::runtime_error>("AMMLiquidity: unexpected 0 balances");

        dirty_ = false;

        return Amounts(assetIn, assetOut);
    }

    return balances_;
}

Amounts
AMMLiquidity::generateFibSeqOffer(const Amounts& balances) const
{
    // first sequence
    if (!fibSeqHelper_.has_value())
    {
        fibSeqHelper_.emplace();
        return fibSeqHelper_->firstSeq(balances, tradingFee_);
    }
    // advance to next sequence
    return fibSeqHelper_->nextNthSeq(
        offerCounter_.curIters(), balances, tradingFee_);
}

}  // namespace ripple