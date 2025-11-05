#include <test/jtx.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {
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
        auto const USD = gw["USD"];

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.close();
            env.trust(USD(100), "alice", "bob", "carol");
            env.close();
            env(pay("alice", "bob", USD(10)),
                delivermin(USD(10)),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", USD(10)),
                delivermin(USD(-5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", USD(10)),
                delivermin(XRP(5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", USD(10)),
                delivermin(Account("carol")["USD"](5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay("alice", "bob", USD(10)),
                delivermin(USD(15)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(gw, "carol", USD(50)));
            env(offer("carol", XRP(5), USD(5)));
            env(pay("alice", "bob", USD(10)),
                paths(XRP),
                delivermin(USD(7)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                ter(tecPATH_PARTIAL));
            env.require(balance(
                "alice", XRP(10000) - drops(env.current()->fees().base)));
            env.require(balance("bob", XRP(10000)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", gw);
            env.close();
            env.trust(USD(1000), "alice", "bob");
            env.close();
            env(pay(gw, "bob", USD(100)));
            env(offer("bob", XRP(100), USD(100)));
            env(pay("alice", "alice", USD(10000)),
                paths(XRP),
                delivermin(USD(100)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.require(balance("alice", USD(100)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.close();
            env.trust(USD(1000), "bob", "carol");
            env.close();
            env(pay(gw, "bob", USD(200)));
            env(offer("bob", XRP(100), USD(100)));
            env(offer("bob", XRP(1000), USD(100)));
            env(offer("bob", XRP(10000), USD(100)));
            env(pay("alice", "carol", USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1000)),
                ter(tecPATH_PARTIAL));
            env(pay("alice", "carol", USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1100)));
            env.require(balance("bob", USD(0)));
            env.require(balance("carol", USD(200)));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), "alice", "bob", "carol", "dan", gw);
            env.close();
            env.trust(USD(1000), "bob", "carol", "dan");
            env.close();
            env(pay(gw, "bob", USD(100)));
            env(pay(gw, "dan", USD(100)));
            env(offer("bob", XRP(100), USD(100)));
            env(offer("bob", XRP(1000), USD(100)));
            env(offer("dan", XRP(100), USD(100)));
            env(pay("alice", "carol", USD(10000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(200)));
            env.require(balance("bob", USD(0)));
            env.require(balance("carol", USD(200)));
            env.require(balance("dan", USD(0)));
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

BEAST_DEFINE_TESTSUITE(DeliverMin, app, ripple);

}  // namespace test
}  // namespace ripple
