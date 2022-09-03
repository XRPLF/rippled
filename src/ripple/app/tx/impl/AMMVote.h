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

#ifndef RIPPLE_TX_AMMVOTE_H_INCLUDED
#define RIPPLE_TX_AMMVOTE_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class Sandbox;

/** AMMVote implements AMM vote Transactor.
 * This transactor allows for the TradingFee of the AMM instance be a votable
 * parameter. Any account (LP) that holds the corresponding LPTokens can cast
 * a vote using the new AMMVote transaction. VoteSlots array in ltAMM object
 * keeps track of upto eight active votes (VoteEntry) for the instance.
 * VoteEntry contains:
 * Account - account id that cast the vote.
 * FeeVal - proposed fee in basis points.
 * VoteWeight - LPTokens owned by the account in basis points.
 * TradingFee is calculated as sum(VoteWeight_i * fee_i)/sum(VoteWeight_i).
 * Every time AMMVote transaction is submitted, the transactor
 * - Fails the transaction is the account doesn't hold LPTokens
 * - Removes VoteEntry for accounts that don't hold LPTokens
 * - If there are fewer than eight VoteEntry objects then add new VoteEntry
 *     object for the account.
 * - If all eight VoteEntry slots are full, then remove VoteEntry that
 *     holds less LPTokens than the account. If all accounts hold more
 *     LPTokens then fail transaction.
 * - If the account already holds a vote, then update VoteEntry.
 * - Calculate and update TradingFee.
 */
class AMMVote : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit AMMVote(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Gather information beyond what the Transactor base class gathers. */
    void
    preCompute() override;

    /** Attempt to create the AMM instance. */
    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMVOTE_H_INCLUDED
