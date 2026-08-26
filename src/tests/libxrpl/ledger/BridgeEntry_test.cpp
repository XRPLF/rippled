#include <xrpl/ledger/helpers/BridgeEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(BridgeEntryTests, Constructors)
{
    EntryTestEnv e;

    STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), IOU("USD", e.bob).issue()};

    expectKeylet<BridgeEntry>(
        e,
        keylet::bridge(bridge, STXChainBridge::ChainType::Locking),
        "bridge(bridge, Locking)",
        bridge,
        STXChainBridge::ChainType::Locking);

    expectKeylet<BridgeEntry>(
        e,
        keylet::bridge(bridge, STXChainBridge::ChainType::Issuing),
        "bridge(bridge, Issuing)",
        bridge,
        STXChainBridge::ChainType::Issuing);

    // The two chain types must not collide, or the assertions above would
    // pass with chainType ignored entirely.
    EXPECT_NE(
        keylet::bridge(bridge, STXChainBridge::ChainType::Locking).key,
        keylet::bridge(bridge, STXChainBridge::ChainType::Issuing).key);
}

}  // namespace xrpl::test
