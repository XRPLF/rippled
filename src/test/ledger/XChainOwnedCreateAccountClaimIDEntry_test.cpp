#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/XChainOwnedCreateAccountClaimIDEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <cstdint>

namespace xrpl::test {

class XChainOwnedCreateAccountClaimIDEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), e.bob["USD"].issue()};

        expectKeylet<XChainOwnedCreateAccountClaimIDEntry>(
            *this,
            e,
            keylet::xChainCreateAccountClaimID(bridge, 5u),
            "xChainCreateAccountClaimID(bridge, seq)",
            bridge,
            std::uint64_t{5});

        // Must not collide with the plain claim-ID keylet, which takes the same
        // arguments.
        BEAST_EXPECT(
            keylet::xChainCreateAccountClaimID(bridge, 5u).key !=
            keylet::xChainClaimID(bridge, 5u).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(XChainOwnedCreateAccountClaimIDEntry, ledger, xrpl);

}  // namespace xrpl::test
