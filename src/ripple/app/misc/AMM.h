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
#ifndef RIPPLE_APP_MISC_AMM_H_INLCUDED
#define RIPPLE_APP_MISC_AMM_H_INLCUDED

#include <ripple/basics/Expected.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>

namespace ripple {

class ReadView;
class ApplyView;
class Sandbox;
class NetClock;
class STObject;
class Rules;

std::uint16_t constexpr TradingFeeThreshold = 1000;  // 1%

/** Calculate AMM account ID.
 */
AccountID
ammAccountID(uint256 const& parentHash, uint256 const& ammID);

/** Calculate Liquidity Provider Token (LPT) Currency.
 */
Currency
lptCurrency(AccountID const& ammAccountID);

/** Calculate LPT Issue.
 */
Issue
lptIssue(AccountID const& ammAccountID);

/** Get AMM pool balances.
 */
std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    beast::Journal const j);

/** Get AMM pool and LP token balances. If both optIssue are
 * provided then they are used as the AMM token pair issues.
 * Otherwise the missing issues are fetched from ammSle.
 */
Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    beast::Journal const j);

/** Get the balance of LP tokens.
 */
STAmount
lpHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Validate the amount.
 * If zero is false and amount is beast::zero then invalid amount.
 * Return error code if invalid amount.
 */
NotTEC
invalidAMMAmount(std::optional<STAmount> const& a, bool nonNegative = false);

/** Check if the line is frozen from the issuer.
 */
bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a);

/** Check if the account requires authorization.
 *  Return terNO_AUTH or terNO_LINE if it does
 *  and tsSUCCESS otherwise.
 */
TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account);

/** Get AMM trading fee for the given account. The fee is discounted
 * if the account is the auction slot owner or one of the slot's authorized
 * accounts.
 */
std::uint16_t
getTradingFee(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& account);

/** Get Issue from sfToken1/sfToken2 fields.
 */
std::pair<Issue, Issue>
getTokensIssue(SLE const& ammSle);

/** Send w/o fees. Either from or to must be AMM account.
 */
TER
ammSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

/** Get time slot of the auction slot.
 */
std::uint16_t
timeSlot(NetClock::time_point const& clock, STObject const& auctionSlot);

/** Return true if required AMM amendments are enabled
 */
bool
ammEnabled(Rules const&);

/** Returns total amount held by AMM for the given token.
 */
STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    const Issue& issue);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_H_INLCUDED
