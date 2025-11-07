#include <test/jtx.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace test {

class NoRipple_test : public beast::unit_test::suite
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

        auto const USD = gw["USD"];

        Json::Value account_gw;
        account_gw[jss::account] = gw.human();
        Json::Value account_alice;
        account_alice[jss::account] = alice.human();

        for (auto SetOrClear : {true, false})
        {
            // Create a trust line with no-ripple flag setting
            env(trust(
                gw,
                USD(100),
                alice,
                SetOrClear ? tfSetNoRipple : tfClearNoRipple));
            env.close();

            // Check no-ripple flag on sender 'gateway'
            Json::Value lines{
                env.rpc("json", "account_lines", to_string(account_gw))};
            auto const& gline0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(gline0[jss::no_ripple].asBool() == SetOrClear);

            // Check no-ripple peer flag on destination 'alice'
            lines = env.rpc("json", "account_lines", to_string(account_alice));
            auto const& aline0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(aline0[jss::no_ripple_peer].asBool() == SetOrClear);
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
        env(pay(alice, carol, carol["USD"](50)), path(bob));
        env.close();

        TER const terNeg{TER{tecNO_PERMISSION}};

        env(trust(alice, bob["USD"](100), bob, tfSetNoRipple), ter(terNeg));
        env(trust(bob, carol["USD"](100), carol, tfSetNoRipple), ter(terNeg));
        env.close();

        Json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = carol.human();
        params[jss::destination_amount] = [] {
            Json::Value dest_amt;
            dest_amt[jss::currency] = "USD";
            dest_amt[jss::value] = "1";
            dest_amt[jss::issuer] = Account("carol").human();
            return dest_amt;
        }();

        auto const resp =
            env.rpc("json", "ripple_path_find", to_string(params));
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
        env(pay(carol, alice, alice["USD"](50)), path(bob));
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

        Json::Value params;
        params[jss::source_account] = alice.human();
        params[jss::destination_account] = carol.human();
        params[jss::destination_amount] = [] {
            Json::Value dest_amt;
            dest_amt[jss::currency] = "USD";
            dest_amt[jss::value] = "1";
            dest_amt[jss::issuer] = Account("carol").human();
            return dest_amt;
        }();

        Json::Value const resp{
            env.rpc("json", "ripple_path_find", to_string(params))};
        BEAST_EXPECT(resp[jss::result][jss::alternatives].size() == 0);

        env(pay(alice, carol, bob["USD"](50)), ter(tecPATH_DRY));
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

        auto const USD = gw["USD"];

        env(trust(gw, USD(100), alice, 0));
        env(trust(gw, USD(100), bob, 0));
        Json::Value params;
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

                auto lines =
                    env.rpc("json", "noripple_check", to_string(params));
                if (apiVersion < 2u)
                    BEAST_EXPECT(lines[jss::result][jss::status] == "success");
                else
                    BEAST_EXPECT(
                        lines[jss::result][jss::error] == "invalidParams");
            }
        }
    }

    void
    run() override
    {
        testSetAndClear();

        auto withFeatsTests = [this](FeatureBitset features) {
            forAllApiVersions([&, this](unsigned testVersion) {
                testDefaultRipple(features, testVersion);
            });
            testNegativeBalance(features);
            testPairwise(features);
        };
        using namespace jtx;
        auto const sa = testable_amendments();
        withFeatsTests(sa - featurePermissionedDEX);
        withFeatsTests(sa);
    }
};

BEAST_DEFINE_TESTSUITE(NoRipple, rpc, ripple);

}  // namespace test
}  // namespace ripple
