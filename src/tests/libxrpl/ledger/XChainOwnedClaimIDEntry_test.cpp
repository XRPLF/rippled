#include <xrpl/ledger/helpers/XChainOwnedClaimIDEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(XChainOwnedClaimIDEntryTests, Constructors)
{
    EntryTestEnv e;

    STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), IOU("USD", e.bob).issue()};

    expectKeylet<XChainOwnedClaimIDEntry>(
        e,
        keylet::xChainClaimID(bridge, 5u),
        "xChainClaimID(bridge, seq)",
        bridge,
        std::uint64_t{5});
}

}  // namespace xrpl::test
