//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <test/jtx.h>

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

        for (auto SetOrClear : {true,false})
        {
            // Create a trust line with no-ripple flag setting
            env( trust(gw, USD(100), alice, SetOrClear ? tfSetNoRipple
                                                       : tfClearNoRipple));
            env.close();

            // Check no-ripple flag on sender 'gateway'
            auto lines = env.rpc("json", "account_lines", to_string(account_gw));
            auto const& gline0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(gline0[jss::no_ripple].asBool() == SetOrClear);

            // Check no-ripple peer flag on destination 'alice'
            lines = env.rpc("json", "account_lines", to_string(account_alice));
            auto const& aline0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(aline0[jss::no_ripple_peer].asBool() == SetOrClear);
        }
    }

    void testNegativeBalance(std::initializer_list<uint256> fs)
    {
        testcase("Set noripple on a line with negative balance");

        using namespace jtx;
        Env env(*this, with_features(fs));

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        env.fund(XRP(10000), gw, alice, bob, carol);

        env.trust(alice["USD"](100), bob);
        env.trust(bob["USD"](100), carol);
        env.close();

        env(pay(alice, carol, carol["USD"](50)), path(bob));

        env(trust(alice, bob["USD"](100), bob, tfSetNoRipple));
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

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        BEAST_EXPECT(resp[jss::result][jss::alternatives].size()==1);

        Json::Value account_alice;
        account_alice[jss::account] = alice.human();
        auto const res = env.rpc("json", "account_lines", to_string(account_alice));
        auto const& lines = res[jss::result][jss::lines];
        BEAST_EXPECT(lines.size() == 1);
        BEAST_EXPECT(!lines[0u].isMember(jss::no_ripple));
    }

    void testPairwise(std::initializer_list<uint256> fs)
    {
        testcase("pairwise NoRipple");

        using namespace jtx;
        Env env(*this, with_features(fs));

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

        auto const resp = env.rpc("json", "ripple_path_find", to_string(params));
        BEAST_EXPECT(resp[jss::result][jss::alternatives].size() == 0);

        env(pay(alice, carol, bob["USD"](50)), ter(tecPATH_DRY));
    }

    void testDefaultRipple(std::initializer_list<uint256> fs)
    {
        testcase("Set default ripple on an account and check new trustlines");

        using namespace jtx;
        Env env(*this, with_features(fs));

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob =   Account("bob");

        env.fund(XRP(10000), gw, noripple(alice, bob));

        env(fset(bob, asfDefaultRipple));

        auto const USD = gw["USD"];

        env(trust(gw, USD(100), alice, 0));
        env(trust(gw, USD(100), bob, 0));

        {
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::peer] = alice.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple_peer].asBool() == true);
        }
        {
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::peer] = gw.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple].asBool() == true);
        }
        {
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::peer] = bob.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple].asBool() == false);
        }
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::peer] = gw.human();

            auto lines = env.rpc("json", "account_lines", to_string(params));
            auto const& line0 = lines[jss::result][jss::lines][0u];
            BEAST_EXPECT(line0[jss::no_ripple_peer].asBool() == false);
        }
    }

    void run ()
    {
        testSetAndClear();

        auto withFeatsTests = [this](std::initializer_list<uint256> fs) {
            testNegativeBalance(fs);
            testPairwise(fs);
            testDefaultRipple(fs);
        };
        withFeatsTests({});
        withFeatsTests({featureFlow});
        withFeatsTests({featureFlow, fix1373});
        withFeatsTests({featureFlow, fix1373, featureFlowCross});
    }
};

BEAST_DEFINE_TESTSUITE(NoRipple,app,ripple);

} // RPC
} // ripple

