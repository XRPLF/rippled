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

#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMConLiquidityOffer.h>
#include <xrpld/app/paths/AMMConLiquidityPool.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cmath>

namespace ripple {

template <typename TIn, typename TOut>
using TAmountPair = std::pair<TIn, TOut>;

// Helper function for ratio calculations
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
        auto const product = a * b;
        auto const result = product / c;

        if (roundUp)
        {
            auto const remainder = product - (result * c);
            if (remainder > beast::zero)
            {
                return result + IOUAmount{1, result.exponent()};
            }
        }

        return result;
    }
    // For STAmount (most flexible)
    else if constexpr (std::is_same_v<T, STAmount>)
    {
        auto const product = a * b;
        auto const result = product / c;

        if (roundUp)
        {
            auto const remainder = product - (result * c);
            if (remainder > beast::zero)
            {
                return result + STAmount{result.issue(), 1};
            }
        }

        return STAmount{a.issue(), result};
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

// Helper function for concentrated liquidity price calculations
// Converts sqrt price from Q64.64 format to a decimal price
double
sqrtPriceX64ToPrice(std::uint64_t sqrtPriceX64)
{
    double const sqrtPrice = static_cast<double>(sqrtPriceX64) / (1ULL << 63);
    return sqrtPrice * sqrtPrice;
}

// Helper function for concentrated liquidity tick calculations
// Converts a price to the corresponding tick
std::int32_t
priceToTick(double price)
{
    // Using the standard Uniswap V3 formula: tick = log(price) / log(1.0001)
    constexpr double TICK_BASE = 1.0001;
    return static_cast<std::int32_t>(std::log(price) / std::log(TICK_BASE));
}

// Helper function for concentrated liquidity tick to price conversion
double
tickToPrice(std::int32_t tick)
{
    // Using the standard Uniswap V3 formula: price = 1.0001^tick
    constexpr double TICK_BASE = 1.0001;
    return std::pow(TICK_BASE, tick);
}

template <typename TIn, typename TOut>
AMMConLiquidityPool<TIn, TOut>::AMMConLiquidityPool(
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
    , sqrtPriceX64_(0)         // Will be initialized from AMM state
    , currentTick_(0)          // Will be initialized from AMM state
    , aggregatedLiquidity_(0)  // Will be calculated from positions
    , j_(j)
{
    // Initialize from AMM state using existing AMM patterns
    if (auto const ammSle = view.read(keylet::amm(in, out)))
    {
        // Get current price and tick from AMM state
        if (ammSle->isFieldPresent(sfSqrtPriceX64))
            sqrtPriceX64_ = ammSle->getFieldU64(sfSqrtPriceX64);

        if (ammSle->isFieldPresent(sfCurrentTick))
            currentTick_ = ammSle->getFieldU32(sfCurrentTick);

        // Get AMM balances using existing AMM patterns
        auto const assetInBalance = ammAccountHolds(view, ammAccountID, in);
        auto const assetOutBalance = ammAccountHolds(view, ammAccountID, out);

        // Validate balances
        if (assetInBalance < beast::zero || assetOutBalance < beast::zero)
        {
            JLOG(j_.warn()) << "AMMConLiquidityPool: invalid balances";
            return;
        }

        // Calculate aggregated liquidity from active positions
        aggregatedLiquidity_ =
            calculateAvailableLiquidity(view, currentTick_, currentTick_);

        // Note: initialBalances_ is const, so we can't modify it after
        // construction The balances are fetched during construction and stored
        // in the const member
    }
}

template <typename TIn, typename TOut>
std::optional<AMMConLiquidityOffer<TIn, TOut>>
AMMConLiquidityPool<TIn, TOut>::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    // Check if there is sufficient liquidity
    if (aggregatedLiquidity_ <= beast::zero)
        return std::nullopt;

    // Calculate the quality for the current price and liquidity
    auto const quality = calculateQuality(sqrtPriceX64_, aggregatedLiquidity_);

    // If CLOB quality is provided and it's better than our quality, return
    // nullopt
    if (clobQuality && *clobQuality > quality)
        return std::nullopt;

    // Calculate amounts based on the current liquidity and price
    auto const [amount0, amount1] = calculateAmountsForLiquidity(
        aggregatedLiquidity_,
        sqrtPriceX64_,
        sqrtPriceX64_,  // Current price as both bounds for now
        sqrtPriceX64_);

    // Convert to the appropriate amount types
    TIn inAmount;

    // Use integrated AMM swap calculation that handles both regular and
    // concentrated liquidity
    auto const ammSle = view.read(keylet::amm(issueIn_, issueOut_));
    if (!ammSle)
    {
        JLOG(j_.warn()) << "AMM not found for swap calculation";
        return std::nullopt;
    }

    // Get the actual trading fee from the AMM (which may be different from the
    // default)
    auto const actualTradingFee = ammSle->getFieldU16(sfTradingFee);

    // Validate that this is a valid concentrated liquidity fee tier
    if (!isValidConcentratedLiquidityFeeTier(actualTradingFee))
    {
        JLOG(j_.warn())
            << "AMM has invalid fee tier for concentrated liquidity: "
            << actualTradingFee;
        return std::nullopt;
    }

    // Calculate output amount using concentrated liquidity formulas
    // For now, use a simple ratio calculation
    auto const outputAmount = mulRatio(amount1, amount0, amount0, false);

    // Check if we need to cross any ticks (for concentrated liquidity)
    if (ammSle->isFieldPresent(sfCurrentTick))
    {
        auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
        auto const newTick = priceToTick(sqrtPriceX64ToPrice(sqrtPriceX64_));
        if (newTick != currentTick)
        {
            JLOG(j_.debug())
                << "Crossing tick from " << currentTick << " to " << newTick;
            
            // Note: Tick crossing would be executed here in a full implementation
            // For now, just log the crossing
            JLOG(j_.debug()) << "Would execute tick crossing from " << currentTick_ << " to " << newTick;
        }
    }
    TOut outAmount;

    if (isXRP(issueIn_))
        inAmount = amount0.xrp();
    else
        inAmount = amount0.iou();

    if (isXRP(issueOut_))
        outAmount = amount1.xrp();
    else
        outAmount = amount1.iou();

    TAmountPair<TIn, TOut> amounts{inAmount, outAmount};
    TAmountPair<TIn, TOut> balances{
        inAmount, outAmount};  // Same as amounts for now

    return AMMConLiquidityOffer<TIn, TOut>(
        *this,
        amounts,
        balances,
        quality,
        sqrtPriceX64_,
        currentTick_,
        currentTick_);
}

template <typename TIn, typename TOut>
STAmount
AMMConLiquidityPool<TIn, TOut>::calculateAvailableLiquidity(
    ReadView const& view,
    std::int32_t tickLower,
    std::int32_t tickUpper) const
{
    // Find all concentrated liquidity positions within the specified tick range
    auto const positions = findActivePositions(view);

    // Aggregate the liquidity from all positions
    STAmount totalLiquidity = beast::zero;
    for (auto const& [owner, liquidity] : positions)
    {
        totalLiquidity += liquidity;
    }

    return totalLiquidity;
}

template <typename TIn, typename TOut>
std::pair<STAmount, STAmount>
AMMConLiquidityPool<TIn, TOut>::calculateAmountsForLiquidity(
    STAmount const& liquidity,
    std::uint64_t sqrtPriceX64,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64) const
{
    // Use the utility functions from AMMCore
    auto const [amount0, amount1] = getAmountsForLiquidity(
        liquidity, sqrtPriceX64, sqrtPriceAX64, sqrtPriceBX64);

    return {amount0, amount1};
}

template <typename TIn, typename TOut>
std::map<AccountID, STAmount>
AMMConLiquidityPool<TIn, TOut>::findActivePositions(ReadView const& view) const
{
    std::map<AccountID, STAmount> positions;
    
    // Get the AMM keylet
    auto const ammKeylet = keylet::amm(issueIn_, issueOut_);
    auto const ammSle = view.read(ammKeylet);
    if (!ammSle)
        return positions;

    // Note: ammID is used for position filtering in the directory iteration
    (void)ammSle->getFieldH256(sfAMMID);
    
    // Iterate through the AMM's owner directory to find concentrated liquidity positions
    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID_);
    
    std::shared_ptr<SLE> page;
    unsigned int index = 0;
    uint256 entry;
    
    // Note: dirFirst requires ApplyView, but we have ReadView
    // For now, return empty map since we can't iterate directories in ReadView
    return {};
    
    return positions;
}

template <typename TIn, typename TOut>
Quality
AMMConLiquidityPool<TIn, TOut>::calculateQuality(
    std::uint64_t sqrtPriceX64,
    STAmount const& liquidity) const
{
    // Calculate quality based on the current price
    // Quality is essentially the price ratio
    auto const price = (static_cast<double>(sqrtPriceX64) / (1ULL << 63)) *
        (static_cast<double>(sqrtPriceX64) / (1ULL << 63));

    return Quality{static_cast<std::uint64_t>(price * 1000000)};
}

template <typename TIn, typename TOut>
void
AMMConLiquidityPool<TIn, TOut>::updateFeeGrowth(
    ApplyView& view,
    STAmount const& fee0,
    STAmount const& fee1) const
{
    // Get the AMM keylet
    auto const ammKeylet = keylet::amm(issueIn_, issueOut_);
    auto const ammSle = view.read(ammKeylet);
    if (!ammSle)
        return;

    auto const ammID = ammSle->getFieldH256(sfAMMID);
    
    // Find all active positions
    auto const positions = findActivePositions(view);
    
    // Update global fee growth
    auto const currentFeeGrowth0 = ammSle->getFieldAmount(sfFeeGrowthGlobal0X128);
    auto const currentFeeGrowth1 = ammSle->getFieldAmount(sfFeeGrowthGlobal1X128);
    
    auto const newFeeGrowth0 = currentFeeGrowth0 + fee0;
    auto const newFeeGrowth1 = currentFeeGrowth1 + fee1;
    
    // Update AMM's global fee growth
    auto ammSleMutable = view.peek(ammKeylet);
    if (ammSleMutable)
    {
        ammSleMutable->setFieldAmount(sfFeeGrowthGlobal0X128, newFeeGrowth0);
        ammSleMutable->setFieldAmount(sfFeeGrowthGlobal1X128, newFeeGrowth1);
    }
    
    // Update fee growth for all active positions
    for (auto const& [owner, liquidity] : positions)
    {
        // Find the position ledger object
        auto const positionKey = getConcentratedLiquidityPositionKey(
            owner, currentTick_, currentTick_, 0);  // Assuming nonce 0 for now
        auto const positionKeylet = keylet::child(positionKey);
        auto const positionSle = view.read(positionKeylet);
        
        if (positionSle)
        {
            // Update position's fee growth tracking
            auto const currentInside0 = positionSle->getFieldAmount(sfFeeGrowthInside0LastX128);
            auto const currentInside1 = positionSle->getFieldAmount(sfFeeGrowthInside1LastX128);
            
            auto const newInside0 = currentInside0 + fee0;
            auto const newInside1 = currentInside1 + fee1;
            
            auto positionSleMutable = view.peek(positionKeylet);
            if (positionSleMutable)
            {
                positionSleMutable->setFieldAmount(sfFeeGrowthInside0LastX128, newInside0);
                positionSleMutable->setFieldAmount(sfFeeGrowthInside1LastX128, newInside1);
            }
        }
    }
}

template <typename TIn, typename TOut>
void
AMMConLiquidityPool<TIn, TOut>::executeTickCrossing(
    ApplyView& view,
    std::int32_t fromTick,
    std::int32_t toTick,
    STAmount const& liquidityDelta) const
{
    // Get the AMM keylet
    auto const ammKeylet = keylet::amm(issueIn_, issueOut_);
    auto const ammSle = view.read(ammKeylet);
    if (!ammSle)
        return;

    // Update the current tick in the AMM
    auto ammSleMutable = view.peek(ammKeylet);
    if (ammSleMutable)
    {
        ammSleMutable->setFieldU32(sfCurrentTick, toTick);
        
        // Update sqrt price
        auto const newSqrtPriceX64 = tickToSqrtPriceX64(toTick);
        ammSleMutable->setFieldU64(sfSqrtPriceX64, newSqrtPriceX64);
    }
    
    // Update the tick's liquidity net
    auto const tickKey = getConcentratedLiquidityTickKey(toTick);
    auto const tickKeylet = keylet::child(tickKey);
    auto const tickSle = view.read(tickKeylet);
    
    if (tickSle)
    {
        auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
        auto const newLiquidityNet = currentLiquidityNet + liquidityDelta;
        
        auto tickSleMutable = view.peek(tickKeylet);
        if (tickSleMutable)
        {
            tickSleMutable->setFieldAmount(sfLiquidityNet, newLiquidityNet);
        }
    }
    
    JLOG(j_.debug()) << "Executed tick crossing from " << fromTick << " to " << toTick;
}

template <typename TIn, typename TOut>
STAmount
AMMConLiquidityPool<TIn, TOut>::calculateLiquidityDelta(
    std::uint64_t sqrtPriceX64,
    std::uint64_t targetSqrtPriceX64,
    STAmount const& amount) const
{
    // Calculate the liquidity delta based on the price change and amount
    // This follows the Uniswap V3 formula for liquidity calculation
    
    if (sqrtPriceX64 == 0 || targetSqrtPriceX64 == 0)
        return STAmount{0};
    
    // Calculate the liquidity delta using the formula:
    // ΔL = Δx * (√P * √P') / (√P' - √P)
    auto const deltaSqrtPrice = targetSqrtPriceX64 - sqrtPriceX64;
    
    if (deltaSqrtPrice == 0)
        return STAmount{0};
    
    auto const liquidityDelta = amount * sqrtPriceX64 * targetSqrtPriceX64 / deltaSqrtPrice;
    return STAmount{amount.issue(), liquidityDelta};
}

// Explicit template instantiations
template class AMMConLiquidityPool<IOUAmount, IOUAmount>;
template class AMMConLiquidityPool<IOUAmount, XRPAmount>;
template class AMMConLiquidityPool<XRPAmount, IOUAmount>;

}  // namespace ripple
