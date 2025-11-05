#ifndef XRPL_PROTOCOL_AMMCORE_H_INCLUDED
#define XRPL_PROTOCOL_AMMCORE_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

std::uint16_t constexpr TRADING_FEE_THRESHOLD = 1000;  // 1%

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

class STObject;
class STAmount;
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

}  // namespace ripple

#endif  // XRPL_PROTOCOL_AMMCORE_H_INCLUDED
