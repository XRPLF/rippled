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

class STObject;
class STAmount;
class Rules;

/** Calculate Liquidity Provider Token (LPT) Currency.
 */
Currency
ammLPTCurrency(Asset const& asset1, Asset const& asset2);

/** Calculate LPT Issue from AMM asset pair.
 */
Issue
ammLPTIssue(Asset const& asset1, Asset const& asset2, AccountID const& ammAccountID);

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
