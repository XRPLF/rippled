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

#ifndef RIPPLE_TX_AMMCREATE_H_INCLUDED
#define RIPPLE_TX_AMMCREATE_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

/** AMMCreate implements Automatic Market Maker(AMM) creation Transactor.
 *  It creates a new AMM instance with two tokens. Any trader, or Liquidity
 *  Provider (LP), can create the AMM instance and receive in return shares
 *  of the AMM pool in the form of LPTokens. The number of tokens that LP gets
 *  are determined by LPTokens = sqrt(A * B), where A and B is the current
 *  composition of the AMM pool. LP can add (AMMDeposit) or withdraw
 *  (AMMWithdraw) tokens from AMM and
 *  AMM can be used transparently in the payment or offer crossing transactions.
 *  Trading fee is charged to the traders for the trades executed against
 *  AMM instance. The fee is added to the AMM pool and distributed to the LPs
 *  in proportion to the LPTokens upon liquidity removal. The fee can be voted
 *  on by LP's (AMMVote). LP's can continuously bid (AMMBid) for the 24 hour
 *  auction slot, which enables LP's to trade at zero trading fee.
 *  AMM instance creates AccountRoot object with disabled master key
 *  for book-keeping of XRP balance if one of the tokens
 *  is XRP, a trustline for each IOU token, a trustline to keep track
 *  of LPTokens, and ltAMM ledger object. AccountRoot ID is generated
 *  internally from the parent's hash. ltAMM's object ID is
 * hash{token1.currency, token1.issuer, token2.currency, token2.issuer}, where
 * issue1 < issue2. ltAMM object provides mapping from the hash to AccountRoot
 * ID and contains: AMMAccount - AMM AccountRoot ID. TradingFee - AMM voted
 * TradingFee. VoteSlots - Array of VoteEntry, contains fee vote information.
 *  AuctionSlot - Auction slot, contains discounted fee bid information.
 *  LPTokenBalance - LPTokens outstanding balance.
 *  AMMToken - currency/issuer information for AMM tokens.
 *  AMMDeposit, AMMWithdraw, AMMVote, and AMMBid transactions use the hash
 *  to access AMM instance.
 *  @see [XLS30d:Creating AMM instance on
 * XRPL](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMCreate : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to create the AMM instance. */
    TER
    doApply() override;
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMCREATE_H_INCLUDED
