
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl::test {

class DeliverMin_test : public beast::unit_test::suite
{
public:
    void
    testConvertAllOfAnAsset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.close();
            env.trust(usd(100), "alice", "bob", "carol");
            env.close();
            env(pay("alice", "bob", usd(10)), deliver_min(usd(10)), Ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(usd(-5)),
                txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(XRP(5)),
                txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(Account("carol")["USD"](5)),
                txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(usd(15)),
                txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(gw, "carol", usd(50)));
            env(offer("carol", XRP(5), usd(5)));
            env(pay("alice", "bob", usd(10)),
                Paths(XRP),
                deliver_min(usd(7)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                Ter(tecPATH_PARTIAL));
            env.Require(Balance("alice", XRP(10000) - drops(env.current()->fees().base)));
            env.Require(Balance("bob", XRP(10000)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", gw);
            env.close();
            env.trust(usd(1000), "alice", "bob");
            env.close();
            env(pay(gw, "bob", usd(100)));
            env(offer("bob", XRP(100), usd(100)));
            env(pay("alice", "alice", usd(10000)),
                Paths(XRP),
                deliver_min(usd(100)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.Require(Balance("alice", usd(100)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.close();
            env.trust(usd(1000), "bob", "carol");
            env.close();
            env(pay(gw, "bob", usd(200)));
            env(offer("bob", XRP(100), usd(100)));
            env(offer("bob", XRP(1000), usd(100)));
            env(offer("bob", XRP(10000), usd(100)));
            env(pay("alice", "carol", usd(10000)),
                Paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1000)),
                Ter(tecPATH_PARTIAL));
            env(pay("alice", "carol", usd(10000)),
                Paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1100)));
            env.Require(Balance("bob", usd(0)));
            env.Require(Balance("carol", usd(200)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", "dan", gw);
            env.close();
            env.trust(usd(1000), "bob", "carol", "dan");
            env.close();
            env(pay(gw, "bob", usd(100)));
            env(pay(gw, "dan", usd(100)));
            env(offer("bob", XRP(100), usd(100)));
            env(offer("bob", XRP(1000), usd(100)));
            env(offer("dan", XRP(100), usd(100)));
            env(pay("alice", "carol", usd(10000)),
                Paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(200)));
            env.Require(Balance("bob", usd(0)));
            env.Require(Balance("carol", usd(200)));
            env.Require(Balance("dan", usd(0)));
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testableAmendments();
        testConvertAllOfAnAsset(sa - featurePermissionedDEX);
        testConvertAllOfAnAsset(sa);
    }
};

BEAST_DEFINE_TESTSUITE(DeliverMin, app, xrpl);

}  // namespace xrpl::test
