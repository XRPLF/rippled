//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <test/jtx/WSClient.h>
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class GatewayBalances_test : public beast::unit_test::suite
{
public:

    void
    testGWB(std::initializer_list<uint256> fs)
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, features(fs));

        // Gateway account and assets
        Account const alice {"alice"};
        env.fund(XRP(10000), "alice");
        auto USD = alice["USD"];
        auto CNY = alice["CNY"];
        auto JPY = alice["JPY"];

        // Create a hotwallet
        Account const hw {"hw"};
        env.fund(XRP(10000), "hw");
        env(trust(hw, USD(10000)));
        env(trust(hw, JPY(10000)));
        env(pay(alice, hw, USD(5000)));
        env(pay(alice, hw, JPY(5000)));

        // Create some clients
        Account const bob {"bob"};
        env.fund(XRP(10000), "bob");
        env(trust(bob, USD(100)));
        env(trust(bob, CNY(100)));
        env(pay(alice, bob, USD(50)));

        Account const charley {"charley"};
        env.fund(XRP(10000), "charley");
        env(trust(charley, CNY(500)));
        env(trust(charley, JPY(500)));
        env(pay(alice, charley, CNY(250)));
        env(pay(alice, charley, JPY(250)));

        Account const dave {"dave"};
        env.fund(XRP(10000), "dave");
        env(trust(dave, CNY(100)));
        env(pay(alice, dave, CNY(30)));

        // give the gateway an asset
        env(trust(alice, charley["USD"](50)));
        env(pay(charley, alice, USD(10)));

        // freeze dave
        env(trust(alice, dave["CNY"](0), dave, tfSetFreeze));

        env.close();

        auto wsc = makeWSClient(env.app().config());

        Json::Value qry;
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
            expect (balances.isObject(), "balances is not an object");
            expect (balances.size() == 1, "balances size is not 1");

            auto const& hwBalance = balances[hw.human()];
            expect (hwBalance.isArray(), "hwBalance is not an array");
            expect (hwBalance.size() == 2);
            auto c1 = hwBalance[0u][jss::currency];
            auto c2 = hwBalance[1u][jss::currency];
            expect (c1 == "USD" || c2 == "USD");
            expect (c1 == "JPY" || c2 == "JPY");
            expect (hwBalance[0u][jss::value] == "5000" &&
                hwBalance[1u][jss::value] == "5000");
        }

        {
            auto const& fBalances = result[jss::frozen_balances];
            expect (fBalances.isObject());
            expect (fBalances.size() == 1);

            auto const& fBal = fBalances[dave.human()];
            expect (fBal.isArray());
            expect (fBal.size() == 1);
            expect (fBal[0u].isObject());
            expect (fBal[0u][jss::currency] == "CNY");
            expect (fBal[0u][jss::value] == "30");
        }

        {
            auto const& assets = result[jss::assets];
            expect (assets.isObject(), "assets it not an object");
            expect (assets.size() == 1, "assets size is not 1");

            auto const& cAssets = assets[charley.human()];
            expect (cAssets.isArray());
            expect (cAssets.size() == 1);
            expect (cAssets[0u][jss::currency] == "USD");
            expect (cAssets[0u][jss::value] == "10");
        }

        {
            auto const& obligations = result[jss::obligations];
            expect (obligations.isObject(), "obligations is not an object");
            expect (obligations.size() == 3);
            expect (obligations["CNY"] == "250");
            expect (obligations["JPY"] == "250");
            expect (obligations["USD"] == "50");
        }

    }

    void
    run() override
    {
        testGWB({});
        testGWB({featureFlow, fix1373});
    }
};

BEAST_DEFINE_TESTSUITE(GatewayBalances,app,ripple);

} // test
} // ripple
