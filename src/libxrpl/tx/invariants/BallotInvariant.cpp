#include <xrpl/tx/invariants/BallotInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>

#include <cstdint>

namespace xrpl {

void
ValidBallot::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (!isDelete && after && after->getType() == ltBALLOT)
    {
        auto& change = ballots_[after->key()];
        change.before = before;
        change.after = after;
    }

    if (!isDelete && !before && after && after->getType() == ltBALLOT_VOTE)
        createdVotes_[(*after)[sfBallotID]] += 1;
}

bool
ValidBallot::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    for (auto const& [key, change] : ballots_)
    {
        auto const& b = change.after;
        std::uint32_t const optionCount = (*b)[sfOptionCount];

        if (!b->isFieldPresent(sfEncryptedTally) ||
            b->getFieldArray(sfEncryptedTally).size() != optionCount)
        {
            JLOG(j.fatal()) << "Invariant failed: ballot tally length != OptionCount";
            return false;
        }

        if (b->isFieldPresent(sfResults))
        {
            if (!b->isFlag(lsfBallotFinalized))
            {
                JLOG(j.fatal()) << "Invariant failed: ballot results present without finalize flag";
                return false;
            }
            if (b->getFieldArray(sfResults).size() != optionCount)
            {
                JLOG(j.fatal()) << "Invariant failed: ballot results length != OptionCount";
                return false;
            }
        }

        std::uint32_t const afterVotes = (*b)[sfVoteCount];
        std::uint32_t const beforeVotes = change.before ? (*change.before)[sfVoteCount] : 0u;
        if (afterVotes < beforeVotes)
        {
            JLOG(j.fatal()) << "Invariant failed: ballot VoteCount decreased";
            return false;
        }

        auto const it = createdVotes_.find(key);
        std::uint32_t const created = it == createdVotes_.end() ? 0u : it->second;
        if (afterVotes - beforeVotes != created)
        {
            JLOG(j.fatal()) << "Invariant failed: ballot VoteCount delta != BallotVotes created";
            return false;
        }
    }

    // Every created BallotVote must correspond to a VoteCount increment on a
    // ballot modified in the same transaction.
    for (auto const& [ballotID, count] : createdVotes_)
    {
        if (!ballots_.contains(ballotID))
        {
            JLOG(j.fatal()) << "Invariant failed: BallotVote created without ballot update";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
