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

#ifndef RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
#define RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED

#include "ripple/app/misc/AMM.h"
#include "ripple/app/misc/AMM_formulae.h"
#include "ripple/app/paths/AMMOfferCounter.h"
#include "ripple/basics/Log.h"
#include "ripple/ledger/ReadView.h"
#include "ripple/ledger/View.h"
#include "ripple/protocol/Quality.h"
#include "ripple/protocol/STLedgerEntry.h"

namespace ripple {

namespace detail {

/** Generate AMM offers with the offer size based on Fibonacci sequence.
 * The sequence corresponds to the payment engine iterations with AMM
 * liquidity. Iterations that don't consume AMM offers don't count.
 * We max out at four iterations with AMM offers.
 */
class FibSeqHelper
{
private:
    // Current sequence amounts.
    mutable Amounts curSeq_{};
    // Latest sequence number.
    mutable std::uint16_t lastNSeq_{0};
    mutable Number x_{0};
    mutable Number y_{0};

public:
    FibSeqHelper() = default;
    ~FibSeqHelper() = default;
    FibSeqHelper(FibSeqHelper const&) = delete;
    FibSeqHelper&
    operator=(FibSeqHelper const&) = delete;
    /** Generate first sequence.
     * @param balances current AMM pool balances.
     * @param tfee trading fee in basis points.
     * @return
     */
    Amounts const&
    firstSeq(Amounts const& balances, std::uint16_t tfee) const
    {
        curSeq_.in = toSTAmount(
            balances.in.issue(),
            (Number(5) / 10000) * balances.in / 2,
            Number::rounding_mode::upward);
        curSeq_.out = swapAssetIn(balances, curSeq_.in, tfee);
        y_ = curSeq_.out;
        return curSeq_;
    }
    /** Generate next sequence.
     * @param n sequence to generate
     * @param balances current AMM pool balances.
     * @param tfee trading fee in basis points.
     * @return
     */
    Amounts const&
    nextNthSeq(std::uint16_t n, Amounts const& balances, std::uint16_t tfee)
        const
    {
        // We are at the same payment engine iteration when executing
        // a limiting step. Have to generate the same sequence.
        if (n == lastNSeq_)
            return curSeq_;
        auto const total = [&]() {
            if (n < lastNSeq_)
                Throw<std::runtime_error>(
                    std::string("nextNthSeq: invalid sequence ") +
                    std::to_string(n) + " " + std::to_string(lastNSeq_));
            Number total{};
            do
            {
                total = x_ + y_;
                x_ = y_;
                y_ = total;
            } while (++lastNSeq_ < n);
            return total;
        }();
        curSeq_.out = toSTAmount(
            balances.out.issue(), total, Number::rounding_mode::downward);
        curSeq_.in = swapAssetOut(balances, curSeq_.out, tfee);
        return curSeq_;
    }
};

}  // namespace detail

/** AMMLiquidity class provides AMM offers to BookStep class.
 * The offers are generated in two ways. If there are multiple
 * paths specified to the payment transaction then the offers
 * are generated based on the Fibonacci sequence with
 * at most four payment engine iterations consuming AMM offers.
 * These offers behave the same way as CLOB offers in that if
 * there is a limiting step, then the offers are adjusted
 * based on their quality.
 * If there is only one path specified in the payment transaction
 * then the offers are generated based on the current step
 * remainingIn/remainingOut amounts and/or competing CLOB offer.
 * In the latter case, the offer's size is set in such a way
 * that the new AMM's pool spot price quality is equal to the CLOB's
 * offer quality.
 */
class AMMLiquidity
{
private:
    AMMOfferCounter const& offerCounter_;
    AccountID ammAccountID_;
    std::uint32_t tradingFee_;
    // Cached AMM pool balances as of last getOffer()
    mutable Amounts balances_;
    // Is seated in case of multi-path. Generates Fibonacci
    // sequence offer.
    mutable std::optional<detail::FibSeqHelper> fibSeqHelper_;
    // Indicates that the balances may have changed
    // since the last fetchBalances()
    mutable bool dirty_;
    beast::Journal const j_;

public:
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Issue const& in,
        Issue const& out,
        AMMOfferCounter const& offerCounter,
        beast::Journal j);
    ~AMMLiquidity() = default;
    AMMLiquidity(AMMLiquidity const&) = delete;
    AMMLiquidity&
    operator=(AMMLiquidity const&) = delete;

    /** Generate AMM offer. Returns nullopt if clobQuality is provided
     * and it is better than AMM offer quality. Otherwise returns AMM offer.
     * If clobQuality is provided then AMM offer size is set based on the
     * quality. If either remainingIn/remainingOut/cache is provided
     * then the offer size is adjusted based on those amounts.
     */
    template <typename TIn, typename TOut>
    std::optional<Amounts>
    getOffer(
        ReadView const& view,
        std::optional<Quality> const& clobQuality = std::nullopt,
        std::optional<TIn> const& remainingIn = std::nullopt,
        std::optional<TOut> const& remainingOut = std::nullopt,
        std::optional<TAmounts<TIn, TOut>> const& cache = std::nullopt) const;

    /** Called when AMM offer is consumed. Sets dirty flag
     * to indicate that the balances may have changed and
     * increments offer counter to indicate that AMM offer
     * is used in the strand.
     */
    void
    consumed()
    {
        dirty_ = true;
        offerCounter_.incrementCounter();
    }

    AccountID
    ammAccount() const
    {
        return ammAccountID_;
    }

private:
    /** Fetches AMM balances if dirty flag is set.
     */
    Amounts
    fetchBalances(ReadView const& view) const;

    /** Returns total amount held by AMM for the given token.
     */
    STAmount
    ammAccountHolds(
        ReadView const& view,
        AccountID const& ammAccountID,
        Issue const& issue) const;

    /** Generate offer based on Fibonacci sequence.
     * @param balances current AMM balances
     */
    Amounts
    generateFibSeqOffer(Amounts const& balances) const;
};

template <typename TIn, typename TOut>
std::optional<Amounts>
AMMLiquidity::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality,
    std::optional<TIn> const& remainingIn,
    std::optional<TOut> const& remainingOut,
    std::optional<TAmounts<TIn, TOut>> const& cache) const
{
    // Can't generate more offers. Only applies if generating
    // based on Fibonacci sequence.
    if (offerCounter_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    JLOG(j_.debug()) << "AMMLiquidity::getOffer balances " << balances_.in
                     << " " << balances_.out << " new balances " << balances.in
                     << " " << balances.out;

    // Can't generate AMM offer with a better quality than CLOB's offer quality
    // if AMM's Spot Price quality is less than CLOB offer quality.
    if (clobQuality && Quality{balances} < *clobQuality)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, higher clob quality";
        return std::nullopt;
    }

    std::optional<STAmount> const saRemIn = remainingIn
        ? std::optional<STAmount>(toSTAmount(*remainingIn, balances.in.issue()))
        : std::nullopt;
    std::optional<STAmount> const saRemOut = remainingOut
        ? std::optional<STAmount>(
              toSTAmount(*remainingOut, balances.out.issue()))
        : std::nullopt;
    std::optional<STAmount> const saCacheIn = cache
        ? std::optional<STAmount>(toSTAmount(cache->in, balances.in.issue()))
        : std::nullopt;
    std::optional<STAmount> const saCacheOut = cache
        ? std::optional<STAmount>(toSTAmount(cache->out, balances.out.issue()))
        : std::nullopt;

    auto const offer = [&]() -> std::optional<Amounts> {
        if (offerCounter_.multiPath())
        {
            auto const offer = generateFibSeqOffer(balances);
            auto const quality = Quality{offer};
            if (clobQuality && quality < clobQuality.value())
                return std::nullopt;
            // Change offer size proportionally to the quality
            // to retain the strands order by quality.
            if (saRemOut && offer.out > *saRemOut)
                return quality.ceil_out(offer, *saRemOut);
            if (saRemIn && offer.in > *saRemIn)
            {
                auto amounts = quality.ceil_in(offer, *saRemIn);
                // The step produced more output in the forward pass than the
                // reverse pass while consuming the same input (or less).
                if (saCacheOut && amounts.out > *saCacheOut &&
                    amounts.in <= saCacheIn)
                {
                    amounts = quality.ceil_out(offer, *saCacheOut);
                    if (amounts.in != *saCacheIn)
                        return std::nullopt;
                }
                return amounts;
            }
            return offer;
        }
        else if (
            auto const offer = clobQuality
                ? changeSpotPriceQuality(balances, *clobQuality, tradingFee_)
                : balances)
        {
            // Change offer size based on swap in/out formulas. The stand's
            // quality changes in this case for the better but since
            // there is only one strand it doesn't impact the strands order.
            if (saRemOut && offer->out > *saRemOut)
                return Amounts{
                    swapAssetOut(balances, *remainingOut, tradingFee_),
                    *saRemOut};
            if (saRemIn && offer->in > *saRemIn)
            {
                auto in = *saRemIn;
                auto out = swapAssetIn(balances, *remainingIn, tradingFee_);
                // The step produced more output in the forward pass than the
                // reverse pass while consuming the same input (or less).
                if (saCacheOut && out > *saCacheOut && in <= *saCacheIn)
                {
                    out = *saCacheOut;
                    in = swapAssetOut(balances, out, tradingFee_);
                    if (in != *saCacheIn)
                        return std::nullopt;
                }
                return Amounts{in, out};
            }
            return offer;
        }
        else
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

#endif  // RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
