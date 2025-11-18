#include <test/jtx.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

struct SetAuth_test : public beast::unit_test::suite
{
    // Set just the tfSetfAuth flag on a trust line
    // If the trust line does not exist, then it should
    // be created under the new rules.
    static Json::Value
    auth(
        jtx::Account const& account,
        jtx::Account const& dest,
        std::string const& currency)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::Account] = account.human();
        jv[jss::LimitAmount] = STAmount(Issue{to_currency(currency), dest})
                                   .getJson(JsonOptions::none);
        jv[jss::TransactionType] = jss::TrustSet;
        jv[jss::Flags] = tfSetfAuth;
        return jv;
    }

    void
    testAuth(FeatureBitset features)
    {
        using namespace jtx;
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        Env env(*this);

        env.fund(XRP(100000), "alice", "bob", gw);
        env(fset(gw, asfRequireAuth));
        env.close();
        env(auth(gw, "alice", "USD"));
        BEAST_EXPECT(
            env.le(keylet::line(Account("alice").id(), gw.id(), USD.currency)));
        env(trust("alice", USD(1000)));
        env(trust("bob", USD(1000)));
        env(pay(gw, "alice", USD(100)));
        env(pay(gw, "bob", USD(100)),
            ter(tecPATH_DRY));  // Should be terNO_AUTH
        env(pay("alice", "bob", USD(50)),
            ter(tecPATH_DRY));  // Should be terNO_AUTH
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testable_amendments();
        testAuth(sa - featurePermissionedDEX);
        testAuth(sa);
    }
};

BEAST_DEFINE_TESTSUITE(SetAuth, app, ripple);

}  // namespace test
}  // namespace ripple
