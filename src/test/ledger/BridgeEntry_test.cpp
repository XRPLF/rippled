#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/BridgeEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STXChainBridge.h>

namespace xrpl::test {

class BridgeEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), e.bob["USD"].issue()};

        expectKeylet<BridgeEntry>(
            *this,
            e,
            keylet::bridge(bridge, STXChainBridge::ChainType::Locking),
            "bridge(bridge, Locking)",
            bridge,
            STXChainBridge::ChainType::Locking);

        expectKeylet<BridgeEntry>(
            *this,
            e,
            keylet::bridge(bridge, STXChainBridge::ChainType::Issuing),
            "bridge(bridge, Issuing)",
            bridge,
            STXChainBridge::ChainType::Issuing);

        // The two chain types must not collide, or the assertions above would
        // pass with chainType ignored entirely.
        BEAST_EXPECT(
            keylet::bridge(bridge, STXChainBridge::ChainType::Locking).key !=
            keylet::bridge(bridge, STXChainBridge::ChainType::Issuing).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(BridgeEntry, ledger, xrpl);

}  // namespace xrpl::test
