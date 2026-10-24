#include <xrpl/ledger/entries/BallotEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(BallotEntryTests, constructors)
{
    EntryTestEnv e;

    std::uint32_t const seq = 11;

    expectKeylet<BallotEntry>(
        e, keylet::ballot(e.alice.id(), seq), "ballot(owner, seq)", e.alice.id(), seq);
}

}  // namespace xrpl::test
