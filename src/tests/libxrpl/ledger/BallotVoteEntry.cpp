#include <xrpl/ledger/entries/BallotVoteEntry.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(BallotVoteEntryTests, constructors)
{
    EntryTestEnv e;

    UInt256 const ballotID = keylet::ballot(e.alice.id(), 11).key;

    expectKeylet<BallotVoteEntry>(
        e,
        keylet::ballotVote(ballotID, e.bob.id()),
        "ballotVote(ballotID, voter)",
        ballotID,
        e.bob.id());
}

}  // namespace xrpl::test
