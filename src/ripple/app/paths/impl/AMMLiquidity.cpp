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
#include <ripple/app/paths/AMMOffer.h>

namespace ripple {

template <typename TIn, typename TOut>
AMMLiquidity<TIn, TOut>::AMMLiquidity(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::uint32_t tradingFee,
    Issue const& in,
    Issue const& out,
    AMMContext& ammContext,
    beast::Journal j)
    : ammContext_(ammContext)
    , ammAccountID_(ammAccountID)
    , tradingFee_(tradingFee)
    , issueIn_(in)
    , issueOut_(out)
    , initialBalances_{fetchBalances(view)}
    , j_(j)
{
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMLiquidity<TIn, TOut>::fetchBalances(ReadView const& view) const
{
    auto const assetIn = ammAccountHolds(view, ammAccountID_, issueIn_);
    auto const assetOut = ammAccountHolds(view, ammAccountID_, issueOut_);
    // This should not happen.
    if (assetIn < beast::zero || assetOut < beast::zero)
        Throw<std::runtime_error>("AMMLiquidity: invalid balances");

    return TAmounts{get<TIn>(assetIn), get<TOut>(assetOut)};
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMLiquidity<TIn, TOut>::generateFibSeqOffer(
    TAmounts<TIn, TOut> const& balances) const
{
    auto const n = ammContext_.curIters();
    TAmounts<TIn, TOut> cur{};
    Number x{};
    Number y{};

    cur.in = toAmount<TIn>(
        getIssue(balances.in),
        InitialFibSeqPct * initialBalances_.in,
        Number::rounding_mode::upward);
    cur.out = swapAssetIn(initialBalances_, cur.in, tradingFee_);
    y = cur.out;
    if (n == 0)
        return cur;

    std::uint16_t i = 0;
    auto const total = [&]() {
        Number total{};
        do
        {
            total = x + y;
            x = y;
            y = total;
        } while (++i < n);
        return total;
    }();
    cur.out = toAmount<TOut>(
        getIssue(balances.out), total, Number::rounding_mode::downward);
    cur.in = swapAssetOut(balances, cur.out, tradingFee_);
    return cur;
}

template <typename T>
constexpr T
maxAmount()
{
    if constexpr (std::is_same_v<T, XRPAmount>)
        return XRPAmount(STAmount::cMaxNative);
    else if constexpr (std::is_same_v<T, IOUAmount>)
        return IOUAmount(STAmount::cMaxValue / 2, STAmount::cMaxOffset);
    else if constexpr (std::is_same_v<T, STAmount>)
        return STAmount(STAmount::cMaxValue / 2, STAmount::cMaxOffset);
}

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMLiquidity<TIn, TOut>::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    // Can't generate more offers if multi-path.
    if (ammContext_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    // Frozen accounts
    if (balances.in == beast::zero || balances.out == beast::zero)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, frozen accounts";
        return std::nullopt;
    }

    JLOG(j_.debug()) << "AMMLiquidity::getOffer balances "
                     << to_string(initialBalances_.in) << " "
                     << to_string(initialBalances_.out) << " new balances "
                     << to_string(balances.in) << " "
                     << to_string(balances.out);

    // Can't generate AMM with a better quality than CLOB's
    // quality if AMM's Spot Price quality is less than CLOB quality.
    if (clobQuality && Quality{balances} < *clobQuality)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, higher clob quality";
        return std::nullopt;
    }

    auto offer = [&]() -> std::optional<AMMOffer<TIn, TOut>> {
        if (ammContext_.multiPath())
        {
            auto const amounts = generateFibSeqOffer(balances);
            if (clobQuality && Quality{amounts} < clobQuality)
                return std::nullopt;
            return AMMOffer<TIn, TOut>(
                *this, amounts, std::nullopt, Quality{amounts});
        }
        else if (
            auto const amounts = clobQuality
                ? changeSpotPriceQuality(balances, *clobQuality, tradingFee_)
                : balances)
        {
            // If the offer size is equal to the balances then change the size
            // to the largest amount, which doesn't overflow.
            // The size is going to be changed in BookStep
            // per either deliver amount limit, or sendmax, or available
            // output or input funds.
            if (balances == amounts)
            {
                return AMMOffer<TIn, TOut>(
                    *this,
                    {maxAmount<TIn>(),
                     swapAssetIn(balances, maxAmount<TIn>(), tradingFee_)},
                    balances,
                    Quality{balances});
            }
            return AMMOffer<TIn, TOut>(
                *this, *amounts, balances, Quality{*amounts});
        }
        return std::nullopt;
    }();

    if (offer && offer->amount().in > beast::zero &&
        offer->amount().out > beast::zero)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, created "
                         << to_string(offer->amount().in) << "/" << issueIn_
                         << " " << to_string(offer->amount().out) << "/"
                         << issueOut_;
        return offer;
    }
    else
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, failed "
                         << offer.has_value();
    }

    return std::nullopt;
}

template class AMMLiquidity<STAmount, STAmount>;
template class AMMLiquidity<IOUAmount, IOUAmount>;
template class AMMLiquidity<XRPAmount, IOUAmount>;
template class AMMLiquidity<IOUAmount, XRPAmount>;

}  // namespace ripple
