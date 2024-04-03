//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
    TAmounts<TIn, TOut> cur{};

    cur.in = toAmount<TIn>(
        getIssue(balances.in),
        InitialFibSeqPct * initialBalances_.in,
        Number::rounding_mode::upward);
    cur.out = swapAssetIn(initialBalances_, cur.in, tradingFee_);

    if (ammContext_.curIters() == 0)
        return cur;

    // clang-format off
    constexpr std::uint32_t fib[AMMContext::MaxIterations] = {
        1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987,
        1597, 2584, 4181, 6765, 10946, 17711, 28657, 46368, 75025, 121393,
        196418, 317811, 514229, 832040, 1346269};
    // clang-format on

    assert(!ammContext_.maxItersReached());

    cur.out = toAmount<TOut>(
        getIssue(balances.out),
        cur.out * fib[ammContext_.curIters() - 1],
        Number::rounding_mode::downward);
    // swapAssetOut() returns negative in this case
    if (cur.out >= balances.out)
        Throw<std::overflow_error>(
            "AMMLiquidity: generateFibSeqOffer exceeds the balance");

    cur.in = swapAssetOut(balances, cur.out, tradingFee_);

    return cur;
}

namespace {
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

template <typename T>
T
maxOut(T const& out, Issue const& iss)
{
    Number const res = out * Number{99, -2};
    return toAmount<T>(iss, res, Number::rounding_mode::downward);
}
}  // namespace

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMLiquidity<TIn, TOut>::maxOffer(
    TAmounts<TIn, TOut> const& balances,
    Rules const& rules) const
{
    if (!rules.enabled(fixAMMOverflowOffer))
    {
        return AMMOffer<TIn, TOut>(
            *this,
            {maxAmount<TIn>(),
             swapAssetIn(balances, maxAmount<TIn>(), tradingFee_)},
            balances,
            Quality{balances});
    }
    else
    {
        auto const out = maxOut<TOut>(balances.out, issueOut());
        if (out <= TOut{0} || out >= balances.out)
            return std::nullopt;
        return AMMOffer<TIn, TOut>(
            *this,
            {swapAssetOut(balances, out, tradingFee_), out},
            balances,
            Quality{balances});
    }
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

    JLOG(j_.trace()) << "AMMLiquidity::getOffer balances "
                     << to_string(initialBalances_.in) << " "
                     << to_string(initialBalances_.out) << " new balances "
                     << to_string(balances.in) << " "
                     << to_string(balances.out);

    // Can't generate AMM with a better quality than CLOB's
    // quality if AMM's Spot Price quality is less than CLOB quality or is
    // within a threshold.
    // Spot price quality (SPQ) is calculated within some precision threshold.
    // On the next iteration, after SPQ is changed, the new SPQ might be close
    // to the requested clobQuality but not exactly and potentially SPQ may keep
    // on approaching clobQuality for many iterations. Checking for the quality
    // threshold prevents this scenario.
    if (auto const spotPriceQ = Quality{balances}; clobQuality &&
        (spotPriceQ <= clobQuality ||
         withinRelativeDistance(spotPriceQ, *clobQuality, Number(1, -7))))
    {
        JLOG(j_.trace()) << "AMMLiquidity::getOffer, higher clob quality";
        return std::nullopt;
    }

    auto offer = [&]() -> std::optional<AMMOffer<TIn, TOut>> {
        try
        {
            if (ammContext_.multiPath())
            {
                auto const amounts = generateFibSeqOffer(balances);
                if (clobQuality && Quality{amounts} < clobQuality)
                    return std::nullopt;
                return AMMOffer<TIn, TOut>(
                    *this, amounts, balances, Quality{amounts});
            }
            else if (!clobQuality)
            {
                // If there is no CLOB to compare against, return the largest
                // amount, which doesn't overflow. The size is going to be
                // changed in BookStep per either deliver amount limit, or
                // sendmax, or available output or input funds. Might return
                // nullopt if the pool is small.
                return maxOffer(balances, view.rules());
            }
            else if (
                auto const amounts =
                    changeSpotPriceQuality(balances, *clobQuality, tradingFee_))
            {
                return AMMOffer<TIn, TOut>(
                    *this, *amounts, balances, Quality{*amounts});
            }
        }
        catch (std::overflow_error const& e)
        {
            JLOG(j_.error()) << "AMMLiquidity::getOffer overflow " << e.what();
            if (!view.rules().enabled(fixAMMOverflowOffer))
                return maxOffer(balances, view.rules());
            else
                return std::nullopt;
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "AMMLiquidity::getOffer exception " << e.what();
        }
        return std::nullopt;
    }();

    if (offer)
    {
        if (offer->amount().in > beast::zero &&
            offer->amount().out > beast::zero)
        {
            JLOG(j_.trace())
                << "AMMLiquidity::getOffer, created "
                << to_string(offer->amount().in) << "/" << issueIn_ << " "
                << to_string(offer->amount().out) << "/" << issueOut_;
            return offer;
        }

        JLOG(j_.error()) << "AMMLiquidity::getOffer, failed";
    }

    return std::nullopt;
}

template class AMMLiquidity<STAmount, STAmount>;
template class AMMLiquidity<IOUAmount, IOUAmount>;
template class AMMLiquidity<XRPAmount, IOUAmount>;
template class AMMLiquidity<IOUAmount, XRPAmount>;

}  // namespace ripple
