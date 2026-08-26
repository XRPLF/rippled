#include <xrpl/ledger/helpers/XChainOwnedCreateAccountClaimIDEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(XChainOwnedCreateAccountClaimIDEntryTests, Constructors)
{
    EntryTestEnv e;

    STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), IOU("USD", e.bob).issue()};

    expectKeylet<XChainOwnedCreateAccountClaimIDEntry>(
        e,
        keylet::xChainCreateAccountClaimID(bridge, 5u),
        "xChainCreateAccountClaimID(bridge, seq)",
        bridge,
        std::uint64_t{5});

    // Must not collide with the plain claim-ID keylet, which takes the same
    // arguments.
    EXPECT_NE(
        keylet::xChainCreateAccountClaimID(bridge, 5u).key, keylet::xChainClaimID(bridge, 5u).key);
}

}  // namespace xrpl::test
