#include <test/jtx.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>

namespace xrpl {
namespace test {

class DeliverMin_test : public beast::unit_test::suite
{
public:
    void
    test_convert_all_of_an_asset(FeatureBitset features)
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
            env(pay("alice", "bob", usd(10)), deliver_min(usd(10)), ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(usd(-5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(XRP(5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(Account("carol")["USD"](5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", usd(10)),
                deliver_min(usd(15)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(gw, "carol", usd(50)));
            env(offer("carol", XRP(5), usd(5)));
            env(pay("alice", "bob", usd(10)),
                paths(XRP),
                deliver_min(usd(7)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                ter(tecPATH_PARTIAL));
            env.require(balance("alice", XRP(10000) - drops(env.current()->fees().base)));
            env.require(balance("bob", XRP(10000)));
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
                paths(XRP),
                deliver_min(usd(100)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.require(balance("alice", usd(100)));
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
                paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1000)),
                ter(tecPATH_PARTIAL));
            env(pay("alice", "carol", usd(10000)),
                paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1100)));
            env.require(balance("bob", usd(0)));
            env.require(balance("carol", usd(200)));
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
                paths(XRP),
                deliver_min(usd(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(200)));
            env.require(balance("bob", usd(0)));
            env.require(balance("carol", usd(200)));
            env.require(balance("dan", usd(0)));
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testable_amendments();
        test_convert_all_of_an_asset(sa - featurePermissionedDEX);
        test_convert_all_of_an_asset(sa);
    }
};

BEAST_DEFINE_TESTSUITE(DeliverMin, app, xrpl);

}  // namespace test
}  // namespace xrpl
