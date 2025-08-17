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

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace ripple {

Currency
ammLPTCurrency(Currency const& cur1, Currency const& cur2)
{
    // AMM LPToken is 0x03 plus 19 bytes of the hash
    std::int32_t constexpr AMMCurrencyCode = 0x03;
    auto const [minC, maxC] = std::minmax(cur1, cur2);
    auto const hash = sha512Half(minC, maxC);
    Currency currency;
    *currency.begin() = AMMCurrencyCode;
    std::copy(
        hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

Issue
ammLPTIssue(
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(cur1, cur2), ammAccountID);
}

NotTEC
invalidAMMAsset(
    Issue const& issue,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (badCurrency() == issue.currency)
        return temBAD_CURRENCY;
    if (isXRP(issue) && issue.account.isNonZero())
        return temBAD_ISSUER;
    if (pair && issue != pair->first && issue != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (issue1 == issue2)
        return temBAD_AMM_TOKENS;
    if (auto const res = invalidAMMAsset(issue1, pair))
        return res;
    if (auto const res = invalidAMMAsset(issue2, pair))
        return res;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Issue, Issue>> const& pair,
    bool validZero)
{
    if (auto const res = invalidAMMAsset(amount.issue(), pair))
        return res;
    if (amount < beast::zero || (!validZero && amount == beast::zero))
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    // It should be impossible for expiration to be < TOTAL_TIME_SLOT_SECS,
    // but check just to be safe
    auto const expiration = auctionSlot[sfExpiration];
    XRPL_ASSERT(
        expiration >= TOTAL_TIME_SLOT_SECS,
        "ripple::ammAuctionTimeSlot : minimum expiration");
    if (expiration >= TOTAL_TIME_SLOT_SECS)
    {
        if (auto const start = expiration - TOTAL_TIME_SLOT_SECS;
            current >= start)
        {
            if (auto const diff = current - start; diff < TOTAL_TIME_SLOT_SECS)
                return diff / AUCTION_SLOT_INTERVAL_DURATION;
        }
    }
    return std::nullopt;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

// Concentrated Liquidity Fee Tier Functions

bool
isValidConcentratedLiquidityFeeTier(std::uint16_t fee)
{
    return fee == CONCENTRATED_LIQUIDITY_FEE_TIER_0_01 ||
           fee == CONCENTRATED_LIQUIDITY_FEE_TIER_0_05 ||
           fee == CONCENTRATED_LIQUIDITY_FEE_TIER_0_3 ||
           fee == CONCENTRATED_LIQUIDITY_FEE_TIER_1_0;
}

std::uint16_t
getConcentratedLiquidityTickSpacing(std::uint16_t fee)
{
    switch (fee)
    {
        case CONCENTRATED_LIQUIDITY_FEE_TIER_0_01:
            return CONCENTRATED_LIQUIDITY_TICK_SPACING_0_01;
        case CONCENTRATED_LIQUIDITY_FEE_TIER_0_05:
            return CONCENTRATED_LIQUIDITY_TICK_SPACING_0_05;
        case CONCENTRATED_LIQUIDITY_FEE_TIER_0_3:
            return CONCENTRATED_LIQUIDITY_TICK_SPACING_0_3;
        case CONCENTRATED_LIQUIDITY_FEE_TIER_1_0:
            return CONCENTRATED_LIQUIDITY_TICK_SPACING_1_0;
        default:
            return CONCENTRATED_LIQUIDITY_TICK_SPACING_0_3; // Default to 0.3%
    }
}

std::uint16_t
getConcentratedLiquidityFeeTier(std::uint16_t tickSpacing)
{
    switch (tickSpacing)
    {
        case CONCENTRATED_LIQUIDITY_TICK_SPACING_0_01:
            return CONCENTRATED_LIQUIDITY_FEE_TIER_0_01;
        case CONCENTRATED_LIQUIDITY_TICK_SPACING_0_05:
            return CONCENTRATED_LIQUIDITY_FEE_TIER_0_05;
        case CONCENTRATED_LIQUIDITY_TICK_SPACING_0_3:
            return CONCENTRATED_LIQUIDITY_FEE_TIER_0_3;
        case CONCENTRATED_LIQUIDITY_TICK_SPACING_1_0:
            return CONCENTRATED_LIQUIDITY_FEE_TIER_1_0;
        default:
            return CONCENTRATED_LIQUIDITY_FEE_TIER_0_3; // Default to 0.3%
    }
}

bool
isValidTickForFeeTier(std::int32_t tick, std::uint16_t fee)
{
    auto const tickSpacing = getConcentratedLiquidityTickSpacing(fee);
    return tick % tickSpacing == 0;
}

// Concentrated Liquidity Utility Functions Implementation

std::uint64_t
tickToSqrtPriceX64(std::int32_t tick)
{
    if (tick >= 0)
    {
        // For positive ticks: sqrt(1.0001^tick)
        // Using Q64.64 fixed point arithmetic
        std::uint64_t constexpr Q64 = 1ULL << 64;
        double const price = std::pow(1.0001, tick);
        double const sqrtPrice = std::sqrt(price);
        return static_cast<std::uint64_t>(sqrtPrice * Q64);
    }
    else
    {
        // For negative ticks: sqrt(1/1.0001^|tick|)
        double const price = std::pow(1.0001, -tick);
        double const sqrtPrice = std::sqrt(1.0 / price);
        std::uint64_t constexpr Q64 = 1ULL << 64;
        return static_cast<std::uint64_t>(sqrtPrice * Q64);
    }
}

std::int32_t
sqrtPriceX64ToTick(std::uint64_t sqrtPriceX64)
{
    std::uint64_t constexpr Q64 = 1ULL << 64;
    double const sqrtPrice = static_cast<double>(sqrtPriceX64) / Q64;
    double const price = sqrtPrice * sqrtPrice;
    double const tick = std::log(price) / std::log(1.0001);
    return static_cast<std::int32_t>(std::round(tick));
}

STAmount
getLiquidityForAmounts(
    STAmount const& amount0,
    STAmount const& amount1,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64)
{
    std::uint64_t constexpr Q64 = 1ULL << 64;
    
    // Ensure sqrtPriceA < sqrtPriceB
    if (sqrtPriceAX64 > sqrtPriceBX64)
        std::swap(sqrtPriceAX64, sqrtPriceBX64);
    
    std::uint64_t sqrtPriceX64 = (sqrtPriceAX64 + sqrtPriceBX64) / 2;
    
    if (sqrtPriceX64 <= sqrtPriceAX64)
    {
        // Current price is below range
        return amount0;
    }
    else if (sqrtPriceX64 >= sqrtPriceBX64)
    {
        // Current price is above range
        return amount1;
    }
    else
    {
        // Current price is within range
        STAmount liquidity0 = amount0 * (sqrtPriceBX64 - sqrtPriceX64) / (sqrtPriceX64 * (sqrtPriceBX64 - sqrtPriceAX64));
        STAmount liquidity1 = amount1 * sqrtPriceX64 / (sqrtPriceBX64 - sqrtPriceAX64);
        return std::min(liquidity0, liquidity1);
    }
}

std::pair<STAmount, STAmount>
getAmountsForLiquidity(
    STAmount const& liquidity,
    std::uint64_t sqrtPriceX64,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64)
{
    // SECURITY: Validate input parameters
    if (liquidity <= STAmount{0})
    {
        return {STAmount{0}, STAmount{0}};
    }
    
    if (sqrtPriceAX64 == 0 || sqrtPriceBX64 == 0)
    {
        return {STAmount{0}, STAmount{0}};
    }
    
    // Ensure sqrtPriceA < sqrtPriceB
    if (sqrtPriceAX64 > sqrtPriceBX64)
        std::swap(sqrtPriceAX64, sqrtPriceBX64);
    
    STAmount amount0, amount1;
    
    if (sqrtPriceX64 <= sqrtPriceAX64)
    {
        // Current price is below range
        // SECURITY: Check for division by zero and overflow
        auto const denominator = sqrtPriceAX64 * sqrtPriceBX64;
        if (denominator == 0)
        {
            return {STAmount{0}, STAmount{0}};
        }
        
        auto const numerator = liquidity * (sqrtPriceBX64 - sqrtPriceAX64);
        amount0 = numerator / denominator;
        amount1 = STAmount{0};
    }
    else if (sqrtPriceX64 >= sqrtPriceBX64)
    {
        // Current price is above range
        amount0 = STAmount{0};
        amount1 = liquidity * (sqrtPriceBX64 - sqrtPriceAX64);
    }
    else
    {
        // Current price is within range
        // SECURITY: Check for division by zero and overflow
        auto const denominator = sqrtPriceX64 * sqrtPriceBX64;
        if (denominator == 0)
        {
            return {STAmount{0}, STAmount{0}};
        }
        
        auto const numerator0 = liquidity * (sqrtPriceBX64 - sqrtPriceX64);
        auto const numerator1 = liquidity * (sqrtPriceX64 - sqrtPriceAX64);
        
        amount0 = numerator0 / denominator;
        amount1 = numerator1;
    }
    
    return {amount0, amount1};
}

bool
isValidTickRange(std::int32_t tickLower, std::int32_t tickUpper, std::uint32_t tickSpacing)
{
    if (tickLower >= tickUpper)
        return false;
    
    if (tickLower < CONCENTRATED_LIQUIDITY_MIN_TICK || tickUpper > CONCENTRATED_LIQUIDITY_MAX_TICK)
        return false;
    
    if (tickLower % tickSpacing != 0 || tickUpper % tickSpacing != 0)
        return false;
    
    return true;
}

uint256
getConcentratedLiquidityPositionKey(
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce)
{
    // Create a unique key for the position
    auto const data = std::make_tuple(owner, tickLower, tickUpper, nonce);
    return sha512Half(data);
}

uint256
getConcentratedLiquidityTickKey(std::int32_t tick)
{
    // Create a unique key for the tick
    return sha512Half(std::make_tuple("tick", tick));
}

}  // namespace ripple
