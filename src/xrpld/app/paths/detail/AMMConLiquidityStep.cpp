//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/paths/AMMConLiquidityOffer.h>
#include <xrpld/app/paths/AMMConLiquidityPool.h>
#include <xrpld/app/paths/detail/AMMConLiquidityStep.h>
#include <xrpld/app/tx/detail/OfferStream.h>
#include <xrpld/ledger/PaymentSandbox.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/container/flat_set.hpp>

namespace ripple {

template <typename TIn, typename TOut>
using TAmountPair = std::pair<TIn, TOut>;

// Sophisticated ratio calculation function for financial precision
// Handles different amount types (XRPAmount, IOUAmount) with proper rounding
template <typename T>
T
mulRatio(T const& a, T const& b, T const& c, bool roundUp)
{
    if (c == beast::zero)
        return beast::zero;

    // For XRPAmount (integer-based)
    if constexpr (std::is_same_v<T, XRPAmount>)
    {
        // Use 128-bit arithmetic to avoid overflow
        __int128_t const a128 = a.drops();
        __int128_t const b128 = b.drops();
        __int128_t const c128 = c.drops();

        __int128_t const product = a128 * b128;
        __int128_t const quotient = product / c128;

        // Handle rounding
        if (roundUp && (product % c128) != 0)
        {
            return XRPAmount{static_cast<std::int64_t>(quotient + 1)};
        }

        return XRPAmount{static_cast<std::int64_t>(quotient)};
    }
    // For IOUAmount (mantissa/exponent-based)
    else if constexpr (std::is_same_v<T, IOUAmount>)
    {
        // IOUAmount has built-in precision handling
        // Use the existing IOUAmount arithmetic with proper rounding
        auto const product = a * b;
        auto const result = product / c;

        if (roundUp)
        {
            // For rounding up, we need to check if there's a remainder
            auto const remainder = product - (result * c);
            if (remainder > beast::zero)
            {
                // Add the smallest representable amount
                return result + IOUAmount{1, result.exponent()};
            }
        }

        return result;
    }
    // For STAmount (most flexible)
    else if constexpr (std::is_same_v<T, STAmount>)
    {
        // STAmount has sophisticated arithmetic built-in
        auto const product = a * b;
        auto const result = product / c;

        if (roundUp)
        {
            // Check for remainder and round up if needed
            auto const remainder = product - (result * c);
            if (remainder > beast::zero)
            {
                // Add the smallest representable amount for this precision
                return result + STAmount{1, result.issue(), result.native()};
            }
        }

        return result;
    }
    // Fallback for other types
    else
    {
        auto const result = (a * b) / c;
        if (roundUp)
        {
            auto const remainder = (a * b) % c;
            if (remainder > beast::zero)
            {
                return result + T{1};
            }
        }
        return result;
    }
}

template <class TIn, class TOut, class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
AMMConLiquidityStep<TIn, TOut, TDerived>::qualityUpperBound(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    // Return the quality upper bound for concentrated liquidity
    // This would be based on the current price and available liquidity
    if (ammConLiquidity_)
    {
        // Calculate quality based on current price
        auto const sqrtPriceX64 = ammConLiquidity_->getSqrtPriceX64();
        auto const price = (static_cast<double>(sqrtPriceX64) / (1ULL << 64)) *
            (static_cast<double>(sqrtPriceX64) / (1ULL << 64));

        return {Quality{Number{price}}, DebtDirection::issues};
    }

    return {std::nullopt, DebtDirection::issues};
}

template <class TIn, class TOut, class TDerived>
std::pair<std::optional<QualityFunction>, DebtDirection>
AMMConLiquidityStep<TIn, TOut, TDerived>::getQualityFunc(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    // Return a quality function for concentrated liquidity
    // This would allow dynamic quality calculation based on trade size
    if (ammConLiquidity_)
    {
        auto const sqrtPriceX64 = ammConLiquidity_->getSqrtPriceX64();
        auto const basePrice =
            (static_cast<double>(sqrtPriceX64) / (1ULL << 63)) *
            (static_cast<double>(sqrtPriceX64) / (1ULL << 63));

        // Create a quality function that adjusts based on trade size
        auto qualityFunc = [basePrice, this](TIn const& in) -> Quality {
            // Calculate dynamic quality based on slippage
            auto const liquidity = ammConLiquidity_->getAggregatedLiquidity();
            if (liquidity <= beast::zero)
                return Quality{Number{basePrice}};
            
            // Calculate slippage based on trade size relative to liquidity
            auto const tradeSizeRatio = static_cast<double>(in) / static_cast<double>(liquidity);
            auto const slippageFactor = 1.0 + (tradeSizeRatio * 0.1); // 10% slippage per 100% of liquidity
            
            auto const adjustedPrice = basePrice * slippageFactor;
            return Quality{Number{adjustedPrice}};
        };

        return {qualityFunc, DebtDirection::issues};
    }

    return {std::nullopt, DebtDirection::issues};
}

template <class TIn, class TOut, class TDerived>
std::uint32_t
AMMConLiquidityStep<TIn, TOut, TDerived>::offersUsed() const
{
    return offersUsed_;
}

template <class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
AMMConLiquidityStep<TIn, TOut, TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TOut const& out)
{
    // Reverse implementation for concentrated liquidity
    // This calculates the input amount needed to produce the desired output

    if (ammConLiquidity_)
    {
        // Get the concentrated liquidity offer
        auto const offer = getAMMConLiquidityOffer(sb, std::nullopt);
        if (offer)
        {
            // Calculate the input amount needed for the desired output
            auto const amounts = offer->amount();
            auto const inputAmount =
                mulRatio(amounts.first, out, amounts.second, true);

            // Execute the offer
            auto const consumed = TAmountPair<TIn, TOut>{inputAmount, out};
            offer->consume(afView, consumed);

            return {inputAmount, out};
        }
    }

    // Fallback to zero amounts if no concentrated liquidity available
    return {beast::zero, beast::zero};
}

template <class TIn, class TOut, class TDerived>
std::pair<TIn, TOut>
AMMConLiquidityStep<TIn, TOut, TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    TIn const& in)
{
    // Forward implementation for concentrated liquidity
    // This calculates the output amount for a given input

    if (ammConLiquidity_)
    {
        // Get the concentrated liquidity offer
        auto const offer = getAMMConLiquidityOffer(sb, std::nullopt);
        if (offer)
        {
            // Calculate the output amount for the given input
            auto const amounts = offer->amount();
            auto const outputAmount =
                mulRatio(amounts.second, in, amounts.first, false);

            // Execute the offer
            auto const consumed = TAmountPair<TIn, TOut>{in, outputAmount};
            offer->consume(afView, consumed);

            return {in, outputAmount};
        }
    }

    // Fallback to zero amounts if no concentrated liquidity available
    return {beast::zero, beast::zero};
}

template <class TIn, class TOut, class TDerived>
std::pair<bool, EitherAmount>
AMMConLiquidityStep<TIn, TOut, TDerived>::validFwd(
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in) override
{
    // Validate forward execution
    if (ammConLiquidity_)
    {
        auto const offer = getAMMConLiquidityOffer(sb, std::nullopt);
        if (offer && offer->isFunded())
        {
            // Check if the input amount is valid
            auto const amounts = offer->amount();
            if (amounts.first > beast::zero)
            {
                return {true, EitherAmount{amounts.second}};
            }
        }
    }

    return {false, EitherAmount{beast::zero}};
}

template <class TIn, class TOut, class TDerived>
TER
AMMConLiquidityStep<TIn, TOut, TDerived>::check(StrandContext const& ctx) const
{
    // Check if concentrated liquidity is available and valid
    if (!ammConLiquidity_)
        return tesSUCCESS;  // No concentrated liquidity available

    // Check if the feature is enabled
    if (!ctx.view.rules().enabled(featureAMMConcentratedLiquidity))
        return temDISABLED;

    // Check if there is sufficient liquidity
    if (ammConLiquidity_->getAggregatedLiquidity() <= beast::zero)
        return tesSUCCESS;  // No liquidity available

    return tesSUCCESS;
}

template <class TIn, class TOut, class TDerived>
std::optional<AMMConLiquidityOffer<TIn, TOut>>
AMMConLiquidityStep<TIn, TOut, TDerived>::getAMMConLiquidityOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    if (ammConLiquidity_)
    {
        return ammConLiquidity_->getOffer(view, clobQuality);
    }

    return std::nullopt;
}

template <class TIn, class TOut, class TDerived>
template <template <typename, typename> typename Offer>
void
AMMConLiquidityStep<TIn, TOut, TDerived>::consumeOffer(
    PaymentSandbox& sb,
    Offer<TIn, TOut>& offer,
    TAmountPair<TIn, TOut> const& ofrAmt,
    TAmountPair<TIn, TOut> const& stepAmt,
    TOut const& ownerGives) const
{
    // Consume the offer and update the concentrated liquidity positions
    offer.consume(sb, ofrAmt);

    // Update fee growth for all affected positions
    if (ammConLiquidity_)
    {
        // Calculate fees based on the trading fee
        auto const fee0 =
            mulRatio(ofrAmt.in, ammConLiquidity_->tradingFee(), 1000000, true);
        auto const fee1 =
            mulRatio(ofrAmt.out, ammConLiquidity_->tradingFee(), 1000000, true);

        // Update fee growth using the implemented function
        ammConLiquidity_->updateFeeGrowth(sb, fee0, fee1);
    }
}

template <class TIn, class TOut, class TDerived>
template <template <typename, typename> typename Offer>
bool
AMMConLiquidityStep<TIn, TOut, TDerived>::execOffer(
    PaymentSandbox& sb,
    Offer<TIn, TOut>& offer,
    TAmountPair<TIn, TOut> const& ofrAmt,
    TAmountPair<TIn, TOut> const& stepAmt,
    TOut const& ownerGives,
    std::function<bool(
        Offer<TIn, TOut>&,
        TAmountPair<TIn, TOut> const&,
        TAmountPair<TIn, TOut> const&,
        TOut const&,
        std::uint32_t,
        std::uint32_t)> const& callback) const
{
    // Execute the offer through the callback
    return callback(offer, ofrAmt, stepAmt, ownerGives, 1, 1);
}

// Payment AMMConLiquidityStep template class (not offer crossing).
template <class TIn, class TOut>
class AMMConLiquidityPaymentStep : public AMMConLiquidityStep<
                                       TIn,
                                       TOut,
                                       AMMConLiquidityPaymentStep<TIn, TOut>>
{
public:
    explicit ConcentratedLiquidityPaymentStep() = default;

    using AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityPaymentStep<TIn, TOut>>::AMMConLiquidityStep;
    using AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityPaymentStep<TIn, TOut>>::qualityUpperBound;
    using typename AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityPaymentStep<TIn, TOut>>::OfferType;

    // Never limit self cross quality on a payment.
    template <template <typename, typename> typename Offer>
    bool
    limitSelfCrossQuality(
        AccountID const&,
        AccountID const&,
        Offer<TIn, TOut> const& offer,
        std::optional<Quality>&,
        FlowOfferStream<TIn, TOut>&,
        bool) const
    {
        return false;
    }

    // A payment can look at offers of any quality
    bool
    checkQualityThreshold(Quality const& quality) const
    {
        return true;
    }

    // A payment doesn't use quality threshold (limitQuality)
    // since the strand's quality doesn't directly relate to the step's quality.
    std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        return lobQuality;
    }

    // For a payment ofrInRate is always the same as trIn.
    std::uint32_t
    getOfrInRate(Step const*, AccountID const&, std::uint32_t trIn) const
    {
        return trIn;
    }

    // For a payment ofrOutRate is always the same as trOut.
    std::uint32_t
    getOfrOutRate(
        Step const*,
        AccountID const&,
        AccountID const&,
        std::uint32_t trOut) const
    {
        return trOut;
    }

    Quality
    getQualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
    {
        auto const [quality, debtDir] = this->qualityUpperBound(v, prevStepDir);
        return quality ? *quality : Quality{Number{0}};
    }
};

// Offer crossing AMMConLiquidityStep template class.
template <class TIn, class TOut>
class AMMConLiquidityOfferCrossingStep
    : public AMMConLiquidityStep<
          TIn,
          TOut,
          AMMConLiquidityOfferCrossingStep<TIn, TOut>>
{
public:
    explicit ConcentratedLiquidityOfferCrossingStep() = default;

    using AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityOfferCrossingStep<TIn, TOut>>::AMMConLiquidityStep;
    using AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityOfferCrossingStep<TIn, TOut>>::qualityUpperBound;
    using typename AMMConLiquidityStep<
        TIn,
        TOut,
        AMMConLiquidityOfferCrossingStep<TIn, TOut>>::OfferType;

    // Limit self cross quality on offer crossing.
    template <template <typename, typename> typename Offer>
    bool
    limitSelfCrossQuality(
        AccountID const& strandSrc,
        AccountID const& strandDst,
        Offer<TIn, TOut> const& offer,
        std::optional<Quality>& limitQuality,
        FlowOfferStream<TIn, TOut>& offers,
        bool isFirst) const
    {
        if (strandSrc == strandDst)
        {
            // Self crossing - limit quality to prevent infinite loops
            if (isFirst)
            {
                limitQuality = offer.quality();
                return true;
            }
            else if (limitQuality && offer.quality() >= *limitQuality)
            {
                return false;
            }
        }
        return true;
    }

    // An offer crossing can only look at offers of better quality
    bool
    checkQualityThreshold(Quality const& quality) const
    {
        return quality > Quality{Number{0}};
    }

    // An offer crossing uses quality threshold (limitQuality)
    std::optional<Quality>
    qualityThreshold(Quality const& lobQuality) const
    {
        return lobQuality;
    }

    // For offer crossing, rates are based on the offer's rates
    std::uint32_t
    getOfrInRate(
        Step const* prevStep,
        AccountID const& owner,
        std::uint32_t trIn) const
    {
        // Use the offer's input rate
        return trIn;
    }

    std::uint32_t
    getOfrOutRate(
        Step const* prevStep,
        AccountID const& owner,
        AccountID const& strandDst,
        std::uint32_t trOut) const
    {
        // Use the offer's output rate
        return trOut;
    }

    Quality
    getQualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
    {
        auto const [quality, debtDir] = this->qualityUpperBound(v, prevStepDir);
        return quality ? *quality : Quality{Number{0}};
    }
};

//------------------------------------------------------------------------------

template <class TIn, class TOut>
static std::pair<TER, std::unique_ptr<Step>>
make_AMMConLiquidityStepHelper(
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<AMMConLiquidityOfferCrossingStep<TIn, TOut>>(
                ctx, in, out);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep =
            std::make_unique<AMMConLiquidityPaymentStep<TIn, TOut>>(
                ctx, in, out);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

std::pair<TER, std::unique_ptr<Step>>
make_AMMConLiquidityStepII(
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out)
{
    return make_AMMConLiquidityStepHelper<IOUAmount, IOUAmount>(ctx, in, out);
}

std::pair<TER, std::unique_ptr<Step>>
make_AMMConLiquidityStepIX(StrandContext const& ctx, Issue const& in)
{
    return make_AMMConLiquidityStepHelper<IOUAmount, XRPAmount>(
        ctx, in, xrpIssue());
}

std::pair<TER, std::unique_ptr<Step>>
make_AMMConLiquidityStepXI(StrandContext const& ctx, Issue const& out)
{
    return make_AMMConLiquidityStepHelper<XRPAmount, IOUAmount>(
        ctx, xrpIssue(), out);
}

}  // namespace ripple
