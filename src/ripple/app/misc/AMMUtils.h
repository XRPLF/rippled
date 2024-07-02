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
#ifndef RIPPLE_APP_MISC_AMMUTILS_H_INLCUDED
#define RIPPLE_APP_MISC_AMMUTILS_H_INLCUDED

#include <ripple/basics/Expected.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>

namespace ripple {

class ReadView;
class ApplyView;
class Sandbox;
class NetClock;

/** Get AMM pool balances.
 */
std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    FreezeHandling freezeHandling,
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
    FreezeHandling freezeHandling,
    beast::Journal const j);

/** Get the balance of LP tokens.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j);

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Get AMM trading fee for the given account. The fee is discounted
 * if the account is the auction slot owner or one of the slot's authorized
 * accounts.
 */
std::uint16_t
getTradingFee(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& account);

/** Returns total amount held by AMM for the given token.
 */
STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue);

/** Delete trustlines to AMM. If all trustlines are deleted then
 * AMM object and account are deleted. Otherwise tecIMPCOMPLETE is returned.
 */
TER
deleteAMMAccount(
    Sandbox& view,
    Issue const& asset,
    Issue const& asset2,
    beast::Journal j);

/** Initialize Auction and Voting slots and set the trading/discounted fee.
 */
void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Issue const& lptIssue,
    std::uint16_t tfee);

/** Return true if the Liquidity Provider is the only AMM provider, false
 * otherwise. Return tecINTERNAL if encountered an unexpected condition,
 * for instance Liquidity Provider has more than one LPToken trustline.
 */
Expected<bool, TER>
isOnlyLiquidityProvider(
    ReadView const& view,
    Issue const& ammIssue,
    AccountID const& lpAccount);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMMUTILS_H_INLCUDED
