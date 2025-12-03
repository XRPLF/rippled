#include <test/jtx.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class LedgerClosed_test : public beast::unit_test::suite
{
public:
    void
    testMonitorRoot()
    {
        using namespace test::jtx;

        // This test relies on ledger hash so must lock it to fee 10.
        auto p = envconfig();
        p->FEES.reference_fee = 10;
        Env env{*this, std::move(p), FeatureBitset{}};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);

        auto lc_result = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lc_result[jss::ledger_hash] ==
            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 2);

        env.close();
        auto const ar_master = env.le(env.master);
        BEAST_EXPECT(ar_master->getAccountID(sfAccount) == env.master.id());
        BEAST_EXPECT((*ar_master)[sfBalance] == drops(99999989999999980));

        auto const ar_alice = env.le(alice);
        BEAST_EXPECT(ar_alice->getAccountID(sfAccount) == alice.id());
        BEAST_EXPECT((*ar_alice)[sfBalance] == XRP(10000));

        lc_result = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lc_result[jss::ledger_hash] ==
            "0F1A9E0C109ADEF6DA2BDE19217C12BBEC57174CBDBD212B0EBDC1CEDB853185");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 3);
    }

    void
    run() override
    {
        testMonitorRoot();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerClosed, rpc, ripple);

}  // namespace ripple
