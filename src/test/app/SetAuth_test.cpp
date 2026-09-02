
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

        // Post fixCleanup3_5_0 paying an unauthorized line fails cleanly
        // with tecNO_AUTH; before it, the retriable terNO_AUTH left a dry
        // path.
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

    // An account that requires authorization for its own issuances may still
    // hold, and return, some other issuer's IOU. Returning it creates no
    // unauthorized holding anywhere, so the receive gate in
    // DirectIPaymentStep::check must not fire on that direction just because
    // the sender happens to carry lsfRequireAuth.
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

        // gwA requires authorization for the IOUs gwA itself issues. It says
        // nothing about what gwA may hold.
        env(fset(gwA, asfRequireAuth));
        env.close();

        // gwA comes to hold 100 of gwB's USD. gwA never authorizes gwB to
        // hold gwA's USD, which is the ordinary state of this line: gwB
        // never asked to hold gwA's issuance.
        env(trust(gwA, usdB(1000)));
        env.close();
        env(pay(gwB, gwA, usdB(100)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(100));

        // Returning part of that balance to the issuer that created it must
        // succeed in every amendment era.
        env(pay(gwA, gwB, usdB(40)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(60));

        // Returning the rest drains the line, which is the whole point of
        // keeping redemption legal.
        env(pay(gwA, gwB, usdB(60)));
        env.close();
        BEAST_EXPECT(env.balance(gwA, usdB) == usdB(0));

        // The permission is directional, so the opposite flow over the very
        // same line stays shut: gwA issuing its own USD would leave gwB
        // holding a balance gwA never authorized. gwB extends trust first so
        // the rejection can only be about authorization and not about a
        // missing trust limit.
        TER const noAuth = features[fixCleanup3_5_0] ? TER{tecNO_AUTH} : TER{tecPATH_DRY};
        auto const usdA = gwA["USD"];

        env(trust(gwB, usdA(1000)));
        env.close();
        env(pay(gwA, gwB, usdA(50)), Ter(noAuth));
        env.close();
        BEAST_EXPECT(env.balance(gwB, usdA) == usdA(0));

        // A third party is no way around it either: gwA cannot route its own
        // USD to gwB through an authorized holder.
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

        // Authorizing gwB is the only thing that opens the direction, and it
        // keeps gwA's own limit for gwB's USD intact.
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
