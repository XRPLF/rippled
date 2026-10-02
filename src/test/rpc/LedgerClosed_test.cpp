
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
            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5");
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
            "0F1A9E0C109ADEF6DA2BDE19217C12BBEC57174CBDBD212B0EBDC1CEDB853185");
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
