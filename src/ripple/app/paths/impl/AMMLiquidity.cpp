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
    AMMOfferCounter& offerCounter,
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
             sle &&
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
AMMLiquidity::fetchBalances(const ReadView& view)
{
    if (dirty_)
    {
        auto const assetIn =
            ammAccountHolds(view, ammAccountID_, balances_.in.issue());
        auto const assetOut =
            ammAccountHolds(view, ammAccountID_, balances_.out.issue());
        // This should not happen.
        if (assetIn < beast::zero || assetOut < beast::zero)
            Throw<std::runtime_error>("AMMLiquidity: invalid balances");

        dirty_ = false;

        return Amounts(assetIn, assetOut);
    }

    return balances_;
}

Amounts
AMMLiquidity::generateFibSeqOffer(const Amounts& balances)
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

std::optional<Amounts>
AMMLiquidity::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality)
{
    // Can't generate more offers. Only applies if generating
    // based on Fibonacci sequence.
    if (offerCounter_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    JLOG(j_.debug()) << "AMMLiquidity::getOffer balances " << balances_.in
                     << " " << balances_.out << " new balances " << balances.in
                     << " " << balances.out;

    // Can't generate AMM ammOffer with a better quality than CLOB's ammOffer
    // quality if AMM's Spot Price quality is less than CLOB ammOffer quality.
    if (clobQuality && Quality{balances} < *clobQuality)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, higher clob quality";
        return std::nullopt;
    }

    auto offer = [&]() -> std::optional<Amounts> {
        if (offerCounter_.multiPath())
        {
            auto const offer = generateFibSeqOffer(balances);
            auto const quality = Quality{offer};
            if (clobQuality && quality < clobQuality.value())
                return std::nullopt;
            return offer;
        }
        else if (
            auto const offer = clobQuality
                ? changeSpotPriceQuality(balances, *clobQuality, tradingFee_)
                : balances)
        {
            return offer;
        }
        return std::nullopt;
    }();

    balances_ = balances;

    if (offer && offer->in > beast::zero && offer->out > beast::zero)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, created " << offer->in
                         << " " << offer->out;
        // The new pool product must be greater or equal to the original pool
        // product. Swap in/out formulas are used in case of one-path, which by
        // design maintain the product invariant. The FibSeq is also generated
        // with the swap in/out formulas except when the offer has to
        // be reduced, in which case it is changed proportionally to
        // the original offer quality. It can be shown that in this case
        // the new pool product is greater than the original pool product.
        // Since the result for XRP is fractional, round downward
        // out amount and round upward in amount to maintain the invariant.
        // This is done in Number/STAmount conversion.
        return offer;
    }
    else
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, failed "
                         << offer.has_value();
    }

    return std::nullopt;
}

}  // namespace ripple