
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <utility>

namespace xrpl {

class LedgerClosed_test : public beast::unit_test::Suite
{
public:
    void
    testMonitorRoot()
    {
        using namespace test::jtx;

        // This test relies on ledger hash so must lock it to fee 10.
        auto p = envconfig();
        p->fees.referenceFee = 10;
        Env env{*this, std::move(p), FeatureBitset{}};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);

        auto lcResult = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lcResult[jss::ledger_hash] ==
            "81B485208CBB93989F2D675F61D385CE9E15A03C68B8CEF1B071C0DD5C5781E1");
        BEAST_EXPECT(lcResult[jss::ledger_index] == 2);

        env.close();
        auto const arMaster = env.le(env.master);
        BEAST_EXPECT(arMaster->getAccountID(sfAccount) == env.master.id());
        BEAST_EXPECT((*arMaster)[sfBalance] == drops(99999989999999980));

        auto const arAlice = env.le(alice);
        BEAST_EXPECT(arAlice->getAccountID(sfAccount) == alice.id());
        BEAST_EXPECT((*arAlice)[sfBalance] == XRP(10000));

        lcResult = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lcResult[jss::ledger_hash] ==
            "5CC95BD8BA54A009A4F5BA437867BBFCF18F5A9CFB7F68FE7816258D164C1516");
        BEAST_EXPECT(lcResult[jss::ledger_index] == 3);
    }

    void
    run() override
    {
        testMonitorRoot();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerClosed, rpc, xrpl);

}  // namespace xrpl
