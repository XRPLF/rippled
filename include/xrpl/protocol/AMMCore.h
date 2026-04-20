#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

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
};

inline constexpr CurveType protocolCurveTypes[] = {
    CtConstantProduct,
    CtConcentratedLiquidity,
    CtStableSwap,
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

// StableSwap limits
inline constexpr std::uint32_t minAmplification = 1;
inline constexpr std::uint32_t maxAmplification = 5000;
inline constexpr std::uint32_t maxAmpChangePct = 10;
inline constexpr std::uint32_t ampRampDuration = 86400;

// Newton's method convergence
inline constexpr int newtonMaxIterations = 256;

class STObject;
class STAmount;
class Rules;

/** Calculate Liquidity Provider Token (LPT) Currency.
 */
Currency
ammLPTCurrency(
    Asset const& asset1,
    Asset const& asset2,
    std::uint8_t curveType = CtConstantProduct);

/** Calculate LPT Issue from AMM asset pair.
 */
Issue
ammLPTIssue(
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccountID,
    std::uint8_t curveType = CtConstantProduct);

/** Validate the amount.
 * If validZero is false and amount is beast::zero then invalid amount.
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

/** Get time slot of the auction slot.
 */
std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot);

/** Return true if required AMM amendments are enabled
 */
bool
ammEnabled(Rules const&);

/** Convert to the fee from the basis points
 * @param tfee  trading fee in {0, 1000}
 * 1 = 1/10bps or 0.001%, 1000 = 1%
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / kAuctionSlotFeeScaleFactor;
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

}  // namespace xrpl
