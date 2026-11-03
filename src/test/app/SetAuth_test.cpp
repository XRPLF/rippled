
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

        Env env(*this);

        env.fund(XRP(100000), "alice", "bob", gw);
        env(fset(gw, asfRequireAuth));
        env.close();
        env(auth(gw, "alice", "USD"));
        BEAST_EXPECT(env.le(keylet::trustLine(Account("alice").id(), gw.id(), usd.currency)));
        env(trust("alice", usd(1000)));
        env(trust("bob", usd(1000)));
        env(pay(gw, "alice", usd(100)));
        env(pay(gw, "bob", usd(100)),
            Ter(tecPATH_DRY));  // Should be terNO_AUTH
        env(pay("alice", "bob", usd(50)),
            Ter(tecPATH_DRY));  // Should be terNO_AUTH
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testableAmendments();
        testAuth(sa - featurePermissionedDEX);
        testAuth(sa);
    }
};

BEAST_DEFINE_TESTSUITE(SetAuth, app, xrpl);

}  // namespace xrpl::test
