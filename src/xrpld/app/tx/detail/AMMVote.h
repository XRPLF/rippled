#ifndef XRPL_TX_AMMVOTE_H_INCLUDED
#define XRPL_TX_AMMVOTE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

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
 * - Fails the transaction if the account doesn't hold LPTokens
 * - Removes VoteEntry for accounts that don't hold LPTokens
 * - If there are fewer than eight VoteEntry objects then add new VoteEntry
 *     object for the account.
 * - If all eight VoteEntry slots are full, then remove VoteEntry that
 *     holds less LPTokens than the account. If all accounts hold more
 *     LPTokens then fail transaction.
 * - If the account already holds a vote, then update VoteEntry.
 * - Calculate and update TradingFee.
 * @see [XLS30d:Governance: Trading Fee Voting
 * Mechanism](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMVote : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMVote(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace ripple

#endif  // XRPL_TX_AMMVOTE_H_INCLUDED
