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

#include <ripple/basics/Number.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/UintTypes.h>

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

/** Calculate AMM account ID.
 */
AccountID
ammAccountID(
    std::uint16_t prefix,
    uint256 const& parentHash,
    uint256 const& ammID);

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

#endif  // RIPPLE_PROTOCOL_AMMCORE_H_INCLUDED
