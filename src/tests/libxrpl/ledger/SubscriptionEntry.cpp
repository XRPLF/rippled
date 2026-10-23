#include <xrpl/ledger/entries/SubscriptionEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(SubscriptionEntryTests, constructors)
{
    EntryTestEnv e;

    std::uint32_t const seq = 4;

    expectKeylet<SubscriptionEntry>(
        e,
        keylet::subscription(e.alice.id(), e.bob.id(), seq),
        "subscription(account, dest, seq)",
        e.alice.id(),
        e.bob.id(),
        seq);

    // Account and destination are both AccountIDs, so the assertion above
    // only has teeth if their order matters.
    EXPECT_NE(
        keylet::subscription(e.alice.id(), e.bob.id(), seq).key,
        keylet::subscription(e.bob.id(), e.alice.id(), seq).key);
}

}  // namespace xrpl::test
