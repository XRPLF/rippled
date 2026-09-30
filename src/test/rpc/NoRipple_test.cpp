
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/flags.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace xrpl::test {

class NoRipple_test : public beast::unit_test::Suite
{
public:
    void
    testSetAndClear()
    {
        testcase("Set and clear noripple");

        using namespace jtx;
        Env env(*this);

        auto const gw = Account("gateway");
        auto const alice = Account("alice");

        env.fund(XRP(10000), gw, alice);

        auto const usd = gw["USD"];

        json::Value accountGw;
        accountGw[jss::account] = gw.human();
        json::Value accountAlice;
        accountAlice[jss::account] = alice.human();

        for (auto setOrClear : {true, false})
        {
            // Create a trust line with no-ripple flag setting
            env(trust(gw, usd(100), alice, setOrClear ? tfSetNoRipple : tfClearNoRipple));
            env.close();

            // Check no-ripple flag on sender 'gateway'
            json::Value lines{env.rpc("json", "account_lines", to_string(accountGw))};
            auto const& gwLine0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(gwLine0[jss::no_ripple].asBool() == setOrClear);

            // Check no-ripple peer flag on destination 'alice'
            lines = env.rpc("json", "account_lines", to_string(accountAlice));
            auto const& aline0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(aline0[jss::no_ripple_peer].asBool() == setOrClear);
        }
    }

    void
    testNegativeBalance(FeatureBitset features)
    {
        testcase("Set noripple on a line with negative balance");

        using namespace jtx;
        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env(*this, features);

        env.fund(XRP(10000), gw, alice, bob, carol);
        env.close();

        env.trust(alice["USD"](100), bob);
        env.trust(bob["USD"](100), carol);
        env.close();

        // After this payment alice has a -50 USD balance with bob, and
        // bob has a -50 USD balance with carol.  So neither alice nor
        // bob should be able to clear the noRipple flag.
        env(pay(alice, carol, carol["USD"](50)), Path(bob));
        env.close();

        TER const terNeg{TER{tecNO_PERMISSION}};

        env(trust(alice, bob["USD"](100), bob, tfSetNoRipple), Ter(terNeg));
        env(trust(bob, carol["USD"](100), carol, tfSetNoRipple), Ter(terNeg));
        env.close();

        json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = carol.human();
        params[jss::destination_amount] = [] {
            json::Value destAmt;
            destAmt[jss::currency] = "USD";
            destAmt[jss::value] = "1";
            destAmt[jss::issuer] = Account("carol").human();
            return destAmt;
        }();

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        BEAST_EXPECT(resp[jss::result][jss::alternatives].size() == 1);

        auto getAccountLines = [&env](Account const& acct) {
            auto const r = jtx::getAccountLines(env, acct);
            return r[jss::lines];
        };
        {
            auto const aliceLines = getAccountLines(alice);
            BEAST_EXPECT(aliceLines.size() == 1);
            BEAST_EXPECT(aliceLines[0u][jss::no_ripple].asBool() == false);

            auto const bobLines = getAccountLines(bob);
            BEAST_EXPECT(bobLines.size() == 2);
            BEAST_EXPECT(bobLines[0u][jss::no_ripple].asBool() == false);
            BEAST_EXPECT(bobLines[1u][jss::no_ripple].asBool() == false);
        }

        // Now carol sends the 50 USD back to alice.  Then alice and
        // bob can set the noRipple flag.
        env(pay(carol, alice, alice["USD"](50)), Path(bob));
        env.close();

        env(trust(alice, bob["USD"](100), bob, tfSetNoRipple));
        env(trust(bob, carol["USD"](100), carol, tfSetNoRipple));
        env.close();
        {
            auto const aliceLines = getAccountLines(alice);
            BEAST_EXPECT(aliceLines.size() == 1);
            BEAST_EXPECT(aliceLines[0u].isMember(jss::no_ripple));

            auto const bobLines = getAccountLines(bob);
            BEAST_EXPECT(bobLines.size() == 2);
            BEAST_EXPECT(bobLines[0u].isMember(jss::no_ripple_peer));
            BEAST_EXPECT(bobLines[1u].isMember(jss::no_ripple));
        }
    }

    void
    testPairwise(FeatureBitset features)
    {
        testcase("pairwise NoRipple");

        using namespace jtx;
        Env env(*this, features);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        env.fund(XRP(10000), alice, bob, carol);

        env(trust(bob, alice["USD"](100)));
        env(trust(carol, bob["USD"](100)));

        env(trust(bob, alice["USD"](100), alice, tfSetNoRipple));
        env(trust(bob, carol["USD"](100), carol, tfSetNoRipple));
        env.close();

        json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = carol.human();
        params[jss::destination_amount] = [] {
            json::Value destAmt;
            destAmt[jss::currency] = "USD";
            destAmt[jss::value] = "1";
            destAmt[jss::issuer] = Account("carol").human();
            return destAmt;
        }();

        json::Value const resp{env.rpc("json", "ripple_path_find", to_string(params))};
        BEAST_EXPECT(resp[jss::result][jss::alternatives].size() == 0);

        env(pay(alice, carol, bob["USD"](50)), Ter(tecPATH_DRY));
    }

    void
    testDefaultRipple(FeatureBitset features, unsigned int apiVersion)
    {
        testcase(
            "Set default ripple on an account and check new trustlines "
            "Version " +
            std::to_string(apiVersion));

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), gw, noripple(alice, bob));

        env(fset(bob, asfDefaultRipple));

        auto const usd = gw["USD"];

        env(trust(gw, usd(100), alice, 0));
        env(trust(gw, usd(100), bob, 0));
        json::Value params;
        params[jss::api_version] = apiVersion;

        {
            params[jss::account] = gw.human();
            params[jss::peer] = alice.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple_peer].asBool() == true);
        }
        {
            params[jss::account] = alice.human();
            params[jss::peer] = gw.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple].asBool() == true);
        }
        {
            params[jss::account] = gw.human();
            params[jss::peer] = bob.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple].asBool() == false);
        }
        {
            params[jss::account] = bob.human();
            params[jss::peer] = gw.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple_peer].asBool() == false);
        }
        {
            // test for transactions
            {
                params[jss::account] = bob.human();
                params[jss::role] = "gateway";
                params[jss::transactions] = "asdf";

                auto lines = env.rpc("json", "noripple_check", to_string(params));
                if (apiVersion < 2u)
                {
                    BEAST_EXPECT(lines[jss::result][jss::status] == "success");
                }
                else
                {
                    BEAST_EXPECT(lines[jss::result][jss::error] == "invalidParams");
                }
            }
        }
    }

    void
    run() override
    {
        testSetAndClear();

        auto withFeatsTests = [this](FeatureBitset features) {
            forAllApiVersions(
                [&, this](unsigned testVersion) { testDefaultRipple(features, testVersion); });
            testNegativeBalance(features);
            testPairwise(features);
        };
        using namespace jtx;
        auto const sa = testableAmendments();
        withFeatsTests(sa - featurePermissionedDEX);
        withFeatsTests(sa);
    }
};

BEAST_DEFINE_TESTSUITE(NoRipple, rpc, xrpl);

}  // namespace xrpl::test
