
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace xrpl::test {

struct SetAuth_test : public beast::unit_test::Suite
{
    // Set just the tfSetfAuth flag on a trust line
    // If the trust line does not exist, then it should
    // be created under the new rules.
    static json::Value
    auth(jtx::Account const& account, jtx::Account const& dest, std::string const& currency)
    {
        using namespace jtx;
        json::Value jv;
        jv[jss::Account] = account.human();
        jv[jss::LimitAmount] =
            STAmount(Issue{toCurrency(currency), dest}).getJson(JsonOptions::Values::None);
        jv[jss::TransactionType] = jss::TrustSet;
        jv[jss::Flags] = tfSetfAuth;
        return jv;
    }

    void
    testAuth(FeatureBitset features)
    {
        using namespace jtx;
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        Env env(*this, features);

        // tecNO_AUTH is final post amendment; terNO_AUTH left a dry path before.
        TER const noAuth = features[fixCleanup3_5_0] ? TER{tecNO_AUTH} : TER{tecPATH_DRY};

        env.fund(XRP(100000), "alice", "bob", gw);
        env(fset(gw, asfRequireAuth));
        env.close();
        env(auth(gw, "alice", "USD"));
        BEAST_EXPECT(env.le(keylet::trustLine(Account("alice").id(), gw.id(), usd.currency)));
        env(trust("alice", usd(1000)));
        env(trust("bob", usd(1000)));
        env(pay(gw, "alice", usd(100)));
        env(pay(gw, "bob", usd(100)), Ter(noAuth));
        env(pay("alice", "bob", usd(50)), Ter(noAuth));
    }

    // An account with lsfRequireAuth may still hold and return another
    // issuer's IOU: returning it creates no unauthorized holding, so the
    // receive gate must not fire on that direction.
    void
    testRedeemForeignIou(FeatureBitset features)
    {
        testcase("Redeem a foreign IOU while requiring auth");
        using namespace jtx;

        auto const gwA = Account("gwA");
        auto const gwB = Account("gwB");
        auto const usdB = gwB["USD"];

        Env env(*this, features);

        env.fund(XRP(10000), gwA, gwB);
        env.close();

        env(fset(gwA, asfRequireAuth));
        env.close();

        // gwA holds gwB's USD on a line gwA never authorizes.
        env(trust(gwA, usdB(1000)));
        env.close();
        env(pay(gwB, gwA, usdB(100)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(100));

        // Redeeming to the issuer succeeds in both eras.
        env(pay(gwA, gwB, usdB(40)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(60));

        env(pay(gwA, gwB, usdB(60)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(0));

        // Authorization is directional: gwA may not issue its own USD to gwB.
        // gwB extends trust first so only authorization can fail.
        TER const noAuth = features[fixCleanup3_5_0] ? TER{tecNO_AUTH} : TER{tecPATH_DRY};
        auto const usdA = gwA["USD"];

        env(trust(gwB, usdA(1000)));
        env.close();
        env(pay(gwA, gwB, usdA(50)), Ter(noAuth));
        env.close();
        BEAST_EXPECT(env.balance(gwB, usdA) == usdA(0));

        // alice cannot pass gwA's USD to gwB either; the path still issues through gwA.
        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);
        env.close();
        env(trust(alice, usdA(1000)));
        env(trust(gwA, usdA(0), alice, tfSetfAuth));
        env.close();
        env(pay(gwA, alice, usdA(100)));
        env.close();
        BEAST_EXPECT(env.balance(alice, usdA) == usdA(100));

        env(pay(alice, gwB, usdA(50)), Ter(noAuth));
        env.close();
        BEAST_EXPECT(env.balance(gwB, usdA) == usdA(0));

        // Authorizing gwB opens it; the 1000 limit keeps gwA's trust in gwB's USD.
        env(trust(gwA, usdB(1000), gwB, tfSetfAuth));
        env.close();
        env(pay(gwA, gwB, usdA(50)));
        env.close();
        BEAST_EXPECT(env.balance(gwB, usdA) == usdA(50));
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testableAmendments();
        testAuth(sa - fixCleanup3_5_0);
        testAuth(sa - featurePermissionedDEX);
        testAuth(sa);
        testRedeemForeignIou(sa - fixCleanup3_5_0);
        testRedeemForeignIou(sa);
    }
};

BEAST_DEFINE_TESTSUITE(SetAuth, app, xrpl);

}  // namespace xrpl::test
