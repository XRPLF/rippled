#include <test/jtx/Env.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/Tuning.h>
#include <xrpld/shamap/NodeFamily.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>

#include <chrono>

namespace xrpl::test {

class NodeFamily_test : public beast::unit_test::Suite
{
    void
    testFullBelowKeptUntilValidated()
    {
        testcase("FullBelowCache kept until the first validated ledger");
        using namespace std::chrono_literals;

        jtx::Env env{*this};
        TestStopwatch clock;
        NodeFamily family(env.app(), env.app().getCollectorManager(), clock);
        auto const fullBelow = family.getFullBelowCache();

        fullBelow->insert(UInt256{1});
        clock.advance(kFullBelowExpiration + 1s);
        family.sweep(false);
        BEAST_EXPECT(fullBelow->size() == 1);
        family.sweep(true);
        BEAST_EXPECT(fullBelow->size() == 0);

        // A standalone Env has a validated ledger, so sweep() expires the entry.
        BEAST_EXPECT(env.app().getLedgerMaster().haveValidated());
        fullBelow->insert(UInt256{2});
        clock.advance(kFullBelowExpiration + 1s);
        family.sweep();
        BEAST_EXPECT(fullBelow->size() == 0);
    }

public:
    void
    run() override
    {
        testFullBelowKeptUntilValidated();
    }
};

BEAST_DEFINE_TESTSUITE(NodeFamily, app, xrpl);

}  // namespace xrpl::test
