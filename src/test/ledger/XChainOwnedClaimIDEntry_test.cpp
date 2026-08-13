#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/XChainOwnedClaimIDEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STXChainBridge.h>

#include <cstdint>

namespace xrpl::test {

class XChainOwnedClaimIDEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        STXChainBridge const bridge{e.alice.id(), xrpIssue(), e.bob.id(), e.bob["USD"].issue()};

        expectKeylet<XChainOwnedClaimIDEntry>(
            *this,
            e,
            keylet::xChainClaimID(bridge, 5u),
            "xChainClaimID(bridge, seq)",
            bridge,
            std::uint64_t{5});
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(XChainOwnedClaimIDEntry, ledger, xrpl);

}  // namespace xrpl::test
