#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/escrow.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class GatewayBalances_test : public beast::unit_test::Suite
{
public:
    void
    testGWB(FeatureBitset features)
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, features);

        {
            // Gateway account and assets
            Account const alice{"alice"};
            env.fund(XRP(10000), "alice");
            auto usd = alice["USD"];
            auto cny = alice["CNY"];
            auto jpy = alice["JPY"];

            // Create a hotwallet
            Account const hw{"hw"};
            env.fund(XRP(10000), "hw");
            env.close();
            env(trust(hw, usd(10000)));
            env(trust(hw, jpy(10000)));
            env(pay(alice, hw, usd(5000)));
            env(pay(alice, hw, jpy(5000)));

            // Create some clients
            Account const bob{"bob"};
            env.fund(XRP(10000), "bob");
            env.close();
            env(trust(bob, usd(100)));
            env(trust(bob, cny(100)));
            env(pay(alice, bob, usd(50)));

            Account const charley{"charley"};
            env.fund(XRP(10000), "charley");
            env.close();
            env(trust(charley, cny(500)));
            env(trust(charley, jpy(500)));
            env(pay(alice, charley, cny(250)));
            env(pay(alice, charley, jpy(250)));

            Account const dave{"dave"};
            env.fund(XRP(10000), "dave");
            env.close();
            env(trust(dave, cny(100)));
            env(pay(alice, dave, cny(30)));

            // give the gateway an asset
            env(trust(alice, charley["USD"](50)));
            env(pay(charley, alice, usd(10)));

            // freeze dave
            env(trust(alice, dave["CNY"](0), dave, tfSetFreeze));

            env.close();

            auto wsc = makeWSClient(env.app().config());

            json::Value qry;
            qry[jss::account] = alice.human();
            qry[jss::hotwallet] = hw.human();

            auto jv = wsc->invoke("gateway_balances", qry);
            expect(jv[jss::status] == "success");
            if (wsc->version() == 2)
            {
                expect(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                expect(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                expect(jv.isMember(jss::id) && jv[jss::id] == 5);
            }

            auto const& result = jv[jss::result];
            expect(result[jss::account] == alice.human());
            expect(result[jss::status] == "success");

            {
                auto const& balances = result[jss::balances];
                expect(balances.isObject(), "balances is not an object");
                expect(balances.size() == 1, "balances size is not 1");

                auto const& hwBalance = balances[hw.human()];
                expect(hwBalance.isArray(), "hwBalance is not an array");
                expect(hwBalance.size() == 2);
                auto c1 = hwBalance[0u][jss::currency];
                auto c2 = hwBalance[1u][jss::currency];
                expect(c1 == "USD" || c2 == "USD");
                expect(c1 == "JPY" || c2 == "JPY");
                expect(hwBalance[0u][jss::value] == "5000" && hwBalance[1u][jss::value] == "5000");
            }

            {
                auto const& fBalances = result[jss::frozen_balances];
                expect(fBalances.isObject());
                expect(fBalances.size() == 1);

                auto const& fBal = fBalances[dave.human()];
                expect(fBal.isArray());
                expect(fBal.size() == 1);
                expect(fBal[0u].isObject());
                expect(fBal[0u][jss::currency] == "CNY");
                expect(fBal[0u][jss::value] == "30");
            }

            {
                auto const& assets = result[jss::assets];
                expect(assets.isObject(), "assets it not an object");
                expect(assets.size() == 1, "assets size is not 1");

                auto const& cAssets = assets[charley.human()];
                expect(cAssets.isArray());
                expect(cAssets.size() == 1);
                expect(cAssets[0u][jss::currency] == "USD");
                expect(cAssets[0u][jss::value] == "10");
            }

            {
                auto const& obligations = result[jss::obligations];
                expect(obligations.isObject(), "obligations is not an object");
                expect(obligations.size() == 3);
                expect(obligations["CNY"] == "250");
                expect(obligations["JPY"] == "250");
                expect(obligations["USD"] == "50");
            }
        }
    }

    void
    testGWBApiVersions(FeatureBitset features)
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, features);

        // Gateway account and assets
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        Account const hw{"hw"};
        env.fund(XRP(10000), hw);
        env.close();

        auto wsc = makeWSClient(env.app().config());

        json::Value qry2;
        qry2[jss::account] = alice.human();
        qry2[jss::hotwallet] = "asdf";

        forAllApiVersions([&, this](unsigned apiVersion) {
            qry2[jss::api_version] = apiVersion;
            auto jv = wsc->invoke("gateway_balances", qry2);
            expect(jv[jss::status] == "error");

            auto response = jv[jss::result];
            auto const error = apiVersion < 2u ? "invalidHotWallet" : "invalidParams";
            BEAST_EXPECT(response[jss::error] == error);
        });
    }

    void
    testGWBOverflow()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);

        // Gateway account and assets
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();
        auto usd = alice["USD"];

        // The largest valid STAmount of USD:
        STAmount const maxUSD(usd, STAmount::kMaxValue, STAmount::kMaxOffset);

        // Create a hotwallet
        Account const hw{"hw"};
        env.fund(XRP(10000), hw);
        env.close();
        env(trust(hw, maxUSD));
        env.close();
        env(pay(alice, hw, maxUSD));

        // Create some clients
        Account const bob{"bob"};
        env.fund(XRP(10000), bob);
        env.close();
        env(trust(bob, maxUSD));
        env.close();
        env(pay(alice, bob, maxUSD));

        Account const charley{"charley"};
        env.fund(XRP(10000), charley);
        env.close();
        env(trust(charley, maxUSD));
        env.close();
        env(pay(alice, charley, maxUSD));

        env.close();

        auto wsc = makeWSClient(env.app().config());

        json::Value query;
        query[jss::account] = alice.human();
        query[jss::hotwallet] = hw.human();

        // Note that the sum of bob's and charley's USD balances exceeds
        // the amount that can be represented in an STAmount.  Nevertheless
        // we get a valid "obligations" that shows the maximum valid
        // STAmount.
        auto jv = wsc->invoke("gateway_balances", query);
        expect(jv[jss::status] == "success");
        expect(jv[jss::result][jss::obligations]["USD"] == maxUSD.getText());
    }

    void
    testGWBWithMPT()
    {
        testcase("Gateway Balances with MPT Escrow");
        using namespace std::chrono_literals;
        using namespace jtx;

        // testableAmendments() includes MPT
        FeatureBitset const features = testableAmendments();
        Env env(*this, features);

        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(10000), alice, bob);
        env.close();

        // Create MPT issuance (Alice) with Escrow capability
        MPTTester mpt(env, alice, {.holders = {bob}, .fund = false});
        mpt.create({.flags = tfMPTCanEscrow});

        // Authorize Bob and fund him
        mpt.authorize({.account = bob, .holderCount = 1});
        mpt.pay(alice, bob, 1000);

        // Bob creates an escrow of MPT to Alice.
        auto const MPT = mpt["MPT"];  // NOLINT(readability-identifier-naming)
        env(escrow::create(bob, alice, MPT(100)), escrow::kFinishTime(env.now() + 10s));
        env.close();

        // Query gateway_balances for Bob.
        auto wsc = makeWSClient(env.app().config());
        json::Value qry;
        qry[jss::account] = bob.human();

        auto jv = wsc->invoke("gateway_balances", qry);
        expect(jv[jss::status] == "success");
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testableAmendments();
        for (auto feature : {sa - featurePermissionedDEX, sa})
        {
            testGWB(feature);
            testGWBApiVersions(feature);
        }
        testGWBWithMPT();
        testGWBOverflow();
    }
};

BEAST_DEFINE_TESTSUITE(GatewayBalances, rpc, xrpl);

}  // namespace xrpl::test
