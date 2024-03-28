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

#ifndef RIPPLE_TX_AMMBID_H_INCLUDED
#define RIPPLE_TX_AMMBID_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

/** AMMBid implements AMM bid Transactor.
 * This is a mechanism for an AMM instance to auction-off
 * the trading advantages to users (arbitrageurs) at a discounted
 * TradingFee for a 24 hour slot. Any account that owns corresponding
 * LPTokens can bid for the auction slot of that AMM instance.
 * Part of the proceeds from the auction, i.e. LPTokens are refunded
 * to the current slot-holder computed on a pro rata basis.
 * Remaining part of the proceeds - in the units of LPTokens- is burnt,
 * thus effectively increasing the LPs shares.
 * Total slot time of 24 hours is divided into 20 equal intervals.
 * The auction slot can be in any of the following states at any time:
 * - Empty - no account currently holds the slot.
 * - Occupied - an account owns the slot with at least 5% of the remaining
 *   slot time (in one of 1-19 intervals).
 * - Tailing - an account owns the slot with less than 5% of the remaining time.
 * The slot-holder owns the slot privileges when in state Occupied or Tailing.
 * If x is the fraction of used slot time for the current slot holder
 * and X is the price at which the slot can be bought specified in LPTokens
 * then: The minimum bid price for the slot in first interval is
 * f(x) = X * 1.05 + min_slot_price
 * The bid price of slot any time is
 * f(x) = X * 1.05 * (1 - x^60) + min_slot_price, where min_slot_price
 * is a constant fraction of the total LPTokens.
 * The revenue from a successful bid is split between the current slot-holder
 * and the pool. The current slot holder is always refunded the remaining slot
 * value f(x) = (1 - x) * X.
 * The remaining LPTokens are burnt.
 * The auction information is maintained in AuctionSlot of ltAMM object.
 * AuctionSlot contains:
 * Account - account id, which owns the slot.
 * Expiration - slot expiration time
 * DiscountedFee - trading fee charged to the account, default is 0.
 * Price - price paid for the slot in LPTokens.
 * AuthAccounts - up to four accounts authorized to trade at
 *     the discounted fee.
 * @see [XLS30d:Continuous Auction
 * Mechanism](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMBid : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMBid(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMBID_H_INCLUDED
