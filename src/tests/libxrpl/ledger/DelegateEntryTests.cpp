#include <xrpl/ledger/helpers/DelegateEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(DelegateEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<DelegateEntry>(
        e,
        keylet::delegate(e.alice.id(), e.bob.id()),
        "delegate(account, authorizedAccount)",
        e.alice.id(),
        e.bob.id());

    // Both arguments are AccountIDs, so the assertion above only has teeth
    // if their order matters.
    EXPECT_NE(
        keylet::delegate(e.alice.id(), e.bob.id()).key,
        keylet::delegate(e.bob.id(), e.alice.id()).key);
}

}  // namespace xrpl::test
