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

#ifndef RIPPLE_PROTOCOL_AMMCORE_H_INCLUDED
#define RIPPLE_PROTOCOL_AMMCORE_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

std::uint16_t constexpr TRADING_FEE_THRESHOLD = 1000;  // 1%

// Concentrated Liquidity Fee Tiers
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_FEE_TIER_0_01 = 10;   // 0.01%
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_FEE_TIER_0_05 = 50;   // 0.05%
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_FEE_TIER_0_3 = 300;   // 0.3%
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_FEE_TIER_1_0 = 1000;  // 1.0%

// Concentrated Liquidity Tick Spacing by Fee Tier
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_TICK_SPACING_0_01 =
    1;  // 0.01% fee tier
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_TICK_SPACING_0_05 =
    10;  // 0.05% fee tier
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_TICK_SPACING_0_3 =
    60;  // 0.3% fee tier
std::uint16_t constexpr CONCENTRATED_LIQUIDITY_TICK_SPACING_1_0 =
    200;  // 1.0% fee tier

// Auction slot
std::uint32_t constexpr TOTAL_TIME_SLOT_SECS = 24 * 3600;
std::uint16_t constexpr AUCTION_SLOT_TIME_INTERVALS = 20;
std::uint16_t constexpr AUCTION_SLOT_MAX_AUTH_ACCOUNTS = 4;
std::uint32_t constexpr AUCTION_SLOT_FEE_SCALE_FACTOR = 100000;
std::uint32_t constexpr AUCTION_SLOT_DISCOUNTED_FEE_FRACTION = 10;
std::uint32_t constexpr AUCTION_SLOT_MIN_FEE_FRACTION = 25;
std::uint32_t constexpr AUCTION_SLOT_INTERVAL_DURATION =
    TOTAL_TIME_SLOT_SECS / AUCTION_SLOT_TIME_INTERVALS;

// Votes
std::uint16_t constexpr VOTE_MAX_SLOTS = 8;
std::uint32_t constexpr VOTE_WEIGHT_SCALE_FACTOR = 100000;

// Concentrated Liquidity Parameters
std::uint32_t constexpr CONCENTRATED_LIQUIDITY_MIN_TICK =
    -887272;  // Minimum tick index
std::uint32_t constexpr CONCENTRATED_LIQUIDITY_MAX_TICK =
    887272;  // Maximum tick index
std::uint32_t constexpr CONCENTRATED_LIQUIDITY_TICK_SPACING =
    1;  // Default tick spacing
std::uint32_t constexpr CONCENTRATED_LIQUIDITY_MAX_POSITIONS =
    100;  // Maximum positions per AMM
std::uint64_t constexpr CONCENTRATED_LIQUIDITY_MIN_LIQUIDITY =
    1000;  // Minimum liquidity per position

// Concentrated Liquidity Position Structure
struct ConcentratedLiquidityPosition
{
    AccountID owner;                    // Position owner
    std::int32_t tickLower;             // Lower tick boundary
    std::int32_t tickUpper;             // Upper tick boundary
    STAmount liquidity;                 // Liquidity amount
    STAmount feeGrowthInside0LastX128;  // Fee growth tracking
    STAmount feeGrowthInside1LastX128;  // Fee growth tracking
    STAmount tokensOwed0;               // Unclaimed fees
    STAmount tokensOwed1;               // Unclaimed fees
    std::uint32_t nonce;                // Position nonce for uniqueness
};

// Concentrated Liquidity Tick Structure
struct ConcentratedLiquidityTick
{
    std::int32_t tick;               // Tick index
    STAmount liquidityGross;         // Total liquidity at this tick
    STAmount liquidityNet;           // Net liquidity change
    STAmount feeGrowthOutside0X128;  // Fee growth outside
    STAmount feeGrowthOutside1X128;  // Fee growth outside
    bool initialized;                // Whether tick is initialized
};

class STObject;
class Rules;

/** Calculate Liquidity Provider Token (LPT) Currency.
 */
Currency
ammLPTCurrency(Currency const& cur1, Currency const& cur2);

/** Calculate LPT Issue from AMM asset pair.
 */
Issue
ammLPTIssue(
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccountID);

/** Validate the amount.
 * If validZero is false and amount is beast::zero then invalid amount.
 * Return error code if invalid amount.
 * If pair then validate amount's issue matches one of the pair's issue.
 */
NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Issue, Issue>> const& pair = std::nullopt,
    bool validZero = false);

NotTEC
invalidAMMAsset(
    Issue const& issue,
    std::optional<std::pair<Issue, Issue>> const& pair = std::nullopt);

NotTEC
invalidAMMAssetPair(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<std::pair<Issue, Issue>> const& pair = std::nullopt);

/** Get time slot of the auction slot.
 */
std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot);

/** Return true if required AMM amendments are enabled
 */
bool
ammEnabled(Rules const&);

// Concentrated Liquidity Fee Tier Functions

/** Check if a fee tier is valid for concentrated liquidity
 */
bool
isValidConcentratedLiquidityFeeTier(std::uint16_t fee);

/** Get the tick spacing for a given fee tier
 */
std::uint16_t
getConcentratedLiquidityTickSpacing(std::uint16_t fee);

/** Get the fee tier for a given tick spacing
 */
std::uint16_t
getConcentratedLiquidityFeeTier(std::uint16_t tickSpacing);

/** Validate that a tick is valid for a given fee tier
 */
bool
isValidTickForFeeTier(std::int32_t tick, std::uint16_t fee);

/** Convert to the fee from the basis points
 * @param tfee  trading fee in {0, 1000}
 * 1 = 1/10bps or 0.001%, 1000 = 1%
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / AUCTION_SLOT_FEE_SCALE_FACTOR;
}

/** Get fee multiplier (1 - tfee)
 * @tfee trading fee in basis points
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return 1 - getFee(tfee);
}

/** Get fee multiplier (1 - tfee / 2)
 * @tfee trading fee in basis points
 */
inline Number
feeMultHalf(std::uint16_t tfee)
{
    return 1 - getFee(tfee) / 2;
}

/** Concentrated Liquidity Utility Functions */

/** Calculate sqrt price from tick index
 * @param tick The tick index
 * @return sqrt price as Q64.64 fixed point
 */
std::uint64_t
tickToSqrtPriceX64(std::int32_t tick);

/** Calculate tick index from sqrt price
 * @param sqrtPriceX64 sqrt price as Q64.64 fixed point
 * @return tick index
 */
std::int32_t
sqrtPriceX64ToTick(std::uint64_t sqrtPriceX64);

/** Calculate liquidity from amounts and price range
 * @param amount0 Amount of token0
 * @param amount1 Amount of token1
 * @param sqrtPriceAX64 Lower sqrt price
 * @param sqrtPriceBX64 Upper sqrt price
 * @return liquidity amount
 */
STAmount
getLiquidityForAmounts(
    STAmount const& amount0,
    STAmount const& amount1,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64);

/** Calculate amounts from liquidity and price range
 * @param liquidity Liquidity amount
 * @param sqrtPriceX64 Current sqrt price
 * @param sqrtPriceAX64 Lower sqrt price
 * @param sqrtPriceBX64 Upper sqrt price
 * @return pair of amounts (amount0, amount1)
 */
std::pair<STAmount, STAmount>
getAmountsForLiquidity(
    STAmount const& liquidity,
    std::uint64_t sqrtPriceX64,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64);

/** Validate tick range for concentrated liquidity
 * @param tickLower Lower tick
 * @param tickUpper Upper tick
 * @param tickSpacing Tick spacing
 * @return true if valid
 */
bool
isValidTickRange(
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t tickSpacing);

/** Calculate position key for concentrated liquidity
 * @param owner Position owner
 * @param tickLower Lower tick
 * @param tickUpper Upper tick
 * @param nonce Position nonce
 * @return position key
 */
uint256
getConcentratedLiquidityPositionKey(
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce);

/** Calculate tick key for concentrated liquidity
 * @param tick Tick index
 * @return tick key
 */
uint256
getConcentratedLiquidityTickKey(std::int32_t tick);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_AMMCORE_H_INCLUDED
