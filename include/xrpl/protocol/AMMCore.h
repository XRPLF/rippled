#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

constexpr std::uint16_t kTradingFeeThreshold = 1000;  // 1%

// Auction slot
constexpr std::uint32_t kTotalTimeSlotSecs = 24 * 3600;
constexpr std::uint16_t kAuctionSlotTimeIntervals = 20;
constexpr std::uint16_t kAuctionSlotMaxAuthAccounts = 4;
constexpr std::uint32_t kAuctionSlotFeeScaleFactor = 100000;
constexpr std::uint32_t kAuctionSlotDiscountedFeeFraction = 10;
constexpr std::uint32_t kAuctionSlotMinFeeFraction = 25;
constexpr std::uint32_t kAuctionSlotIntervalDuration =
    kTotalTimeSlotSecs / kAuctionSlotTimeIntervals;

// Votes
constexpr std::uint16_t kVoteMaxSlots = 8;
constexpr std::uint32_t kVoteWeightScaleFactor = 100000;

// Curve type identifiers
enum CurveType : std::uint8_t {
    CtConstantProduct = 0,
    CtConcentratedLiquidity = 1,
    CtStableSwap = 2,
    CtBinned = 3,
};

inline constexpr CurveType protocolCurveTypes[] = {
    CtConstantProduct,
    CtConcentratedLiquidity,
    CtStableSwap,
    CtBinned,
};

// Fee tier definitions for concentrated liquidity
enum FeeTier : std::uint8_t {
    FtStable = 0,  // 1 bp,   tick spacing 1
    FtLow = 1,     // 5 bp,   tick spacing 10
    FtMedium = 2,  // 30 bp,  tick spacing 60
    FtHigh = 3,    // 100 bp, tick spacing 200
};

inline constexpr std::uint16_t feeTierToFee[] = {1, 5, 30, 100};
inline constexpr std::int32_t feeTierToTickSpacing[] = {1, 10, 60, 200};
inline constexpr std::uint8_t feeTierCount = 4;

// Tick bounds
inline constexpr std::int32_t minTick = -887272;
inline constexpr std::int32_t maxTick = 887272;

// Offset-binary scaling applied wherever a tick is hashed or bit-packed
// to keep arithmetic in unsigned domain (avoids signed-division surprises
// near zero). Used by:
//   - keylet::ammTick (offset-encoded into the low 64 keylet bits)
//   - keylet::ammTickBitmapWord (wordIndex = (tick + offset) >> 8)
//   - Validthe AMM bitmap-consistency invariant
// One constant, one source of fragility — DO NOT duplicate inline.
inline constexpr std::uint32_t kTickBitmapOffset =
    static_cast<std::uint32_t>(-static_cast<std::int64_t>(minTick));

// StableSwap limits
inline constexpr std::uint32_t minAmplification = 1;
inline constexpr std::uint32_t maxAmplification = 5000;
inline constexpr std::uint32_t maxAmpChangePct = 10;
inline constexpr std::uint32_t ampRampDuration = 86400;

// Binned-curve limits. Bin step in basis points; bin price grows as
// (1 + binStep/10000)^binID. Range bound keeps state size finite and
// price range close to v3's effective range at default tick spacing.
inline constexpr std::int32_t minBinID = -221818;
inline constexpr std::int32_t maxBinID = 221818;
inline constexpr std::uint16_t validBinSteps[] = {1, 5, 10, 25, 100};
inline constexpr std::uint8_t binStepCount = 5;

// Newton's method convergence
inline constexpr int newtonMaxIterations = 256;

// Concentrated-liquidity per-swap tick-crossing cap. Bounds the
// per-swap work done by the curve's iterative tick traversal (each
// crossing does one SHAMap lookup for the next initialised tick + one
// SLE read). With FtStable's tickSpacing=1, 1000 crossings = ~10%
// price range; with FtMedium's tickSpacing=60, 1000 crossings spans
// the equivalent of a ~4x price move — both comfortably larger than
// any reasonable swap requires. Hitting the cap produces a silent
// partial fill: the curve returns the output realized over the first
// `maxTickCrossings` boundaries, and the caller infers the cap from
// the (smaller-than-requested) result. Uniswap v3 has no protocol
// cap (only block gas); we cap here because XRPL has no metered
// execution and the cap is the only fairness bound on per-swap work.
inline constexpr int maxTickCrossings = 1000;

class STObject;
class STAmount;
class Rules;

/**
 * Calculate Liquidity Provider Token (LPT) Currency.
 */
Currency
ammLPTCurrency(
    Asset const& asset1,
    Asset const& asset2,
    std::uint8_t curveType = CtConstantProduct);

/**
 * Calculate LPT Issue from AMM asset pair.
 */
Issue
ammLPTIssue(
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccountID,
    std::uint8_t curveType = CtConstantProduct);

/**
 * Validate the amount.
 * If validZero is false and amount is beast::kZero then invalid amount.
 * Return error code if invalid amount.
 * If pair then validate amount's issue matches one of the pair's issue.
 */
NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt,
    bool validZero = false);

NotTEC
invalidAMMAsset(
    Asset const& asset,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt);

NotTEC
invalidAMMAssetPair(
    Asset const& asset1,
    Asset const& asset2,
    std::optional<std::pair<Asset, Asset>> const& pair = std::nullopt);

/**
 * Get time slot of the auction slot.
 */
std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot);

/**
 * Return true if required AMM amendment is enabled
 */
bool
ammEnabled(Rules const&);

/**
 * Convert to the fee from the basis points
 * @param tfee  trading fee in {0, 1000}
 * 1 = 1/10bps or 0.001%, 1000 = 1%
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / kAuctionSlotFeeScaleFactor;
}

/**
 * Minimum auction slot price: LPTokens * TradingFee / kAuctionSlotMinFeeFraction
 * @param lptAMMBalance  AMM LP token balance
 * @param tradingFee     trading fee in {0, 1000}
 */
inline Number
ammAuctionMinSlotPrice(Number const& lptAMMBalance, std::uint16_t tradingFee)
{
    return lptAMMBalance * getFee(tradingFee) / kAuctionSlotMinFeeFraction;
}

/**
 * Get fee multiplier (1 - tfee)
 * @tfee trading fee in basis points
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return 1 - getFee(tfee);
}

/**
 * Get fee multiplier (1 - tfee / 2)
 * @tfee trading fee in basis points
 */
inline Number
feeMultHalf(std::uint16_t tfee)
{
    return 1 - getFee(tfee) / 2;
}

}  // namespace xrpl
