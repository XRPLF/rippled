#include <xrpl/ledger/helpers/SponsorshipEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(SponsorshipEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<SponsorshipEntry>(
        e,
        keylet::sponsorship(e.alice.id(), e.bob.id()),
        "sponsorship(sponsor, sponsee)",
        e.alice.id(),
        e.bob.id());

    // Sponsor and sponsee are both AccountIDs, so the assertion above only
    // has teeth if their order matters.
    EXPECT_NE(
        keylet::sponsorship(e.alice.id(), e.bob.id()).key,
        keylet::sponsorship(e.bob.id(), e.alice.id()).key);
}

}  // namespace xrpl::test
