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

#include <test/jtx.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class AccountOffers_test : public beast::unit_test::suite
{
public:
    // test helper
    static bool
    checkMarker(Json::Value const& val)
    {
        return val.isMember(jss::marker) && val[jss::marker].isString() &&
            val[jss::marker].asString().size() > 0;
    }

    void
    testNonAdminMinLimit()
    {
        testcase("Non-Admin Min Limit");

        using namespace jtx;
        Env env{*this, envconfig(no_admin)};
        Account const gw("G1");
        auto const USD_gw = gw["USD"];
        Account const bob("bob");
        auto const USD_bob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(USD_gw(1000), bob);

        // this is to provide some USD from gw in the
        // bob account so that it can rightly
        // make offers that give those USDs
        env(pay(gw, bob, USD_gw(10)));
        unsigned const offer_count = 12u;
        for (auto i = 0u; i < offer_count; i++)
        {
            Json::Value jvo = offer(bob, XRP(100 + i), USD_gw(1));
            jvo[sfExpiration.fieldName] = 10000000u;
            env(jvo);
        }

        // make non-limited RPC call
        auto const jro_nl =
            env.rpc("account_offers", bob.human())[jss::result][jss::offers];
        BEAST_EXPECT(checkArraySize(jro_nl, offer_count));

        // now make a low-limit query, should get "corrected"
        // to a min of 10 results with a marker set since there
        // are more than 10 total
        Json::Value jvParams;
        jvParams[jss::account] = bob.human();
        jvParams[jss::limit] = 1u;
        auto const jrr_l = env.rpc(
            "json", "account_offers", jvParams.toStyledString())[jss::result];
        auto const& jro_l = jrr_l[jss::offers];
        BEAST_EXPECT(checkMarker(jrr_l));
        // 9u is the expected size, since one account object is a trustline
        BEAST_EXPECT(checkArraySize(jro_l, 9u));
    }

    void
    testSequential(bool asAdmin)
    {
        testcase(
            std::string("Sequential - ") + (asAdmin ? "admin" : "non-admin"));

        using namespace jtx;
        Env env{*this, asAdmin ? envconfig() : envconfig(no_admin)};
        Account const gw("G1");
        auto const USD_gw = gw["USD"];
        Account const bob("bob");
        auto const USD_bob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(USD_gw(1000), bob);

        // this is to provide some USD from gw in the
        // bob account so that it can rightly
        // make offers that give those USDs
        env(pay(gw, bob, USD_gw(10)));

        env(offer(bob, XRP(100), USD_bob(1)));
        env(offer(bob, XRP(200), USD_gw(2)));
        env(offer(bob, XRP(30), USD_gw(6)));

        // make the RPC call
        auto const jroOuter =
            env.rpc("account_offers", bob.human())[jss::result][jss::offers];
        if (BEAST_EXPECT(checkArraySize(jroOuter, 3u)))
        {
            // Note that the returned offers are sorted by index, not by
            // order of insertion or by sequence number.  There is no
            // guarantee that their order will not change in the future
            // if the sequence numbers or the account IDs change.
            BEAST_EXPECT(jroOuter[0u][jss::quality] == "100000000");
            BEAST_EXPECT(jroOuter[0u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(
                jroOuter[0u][jss::taker_gets][jss::issuer] == gw.human());
            BEAST_EXPECT(jroOuter[0u][jss::taker_gets][jss::value] == "2");
            BEAST_EXPECT(jroOuter[0u][jss::taker_pays] == "200000000");

            BEAST_EXPECT(jroOuter[1u][jss::quality] == "100000000");
            BEAST_EXPECT(jroOuter[1u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(
                jroOuter[1u][jss::taker_gets][jss::issuer] == bob.human());
            BEAST_EXPECT(jroOuter[1u][jss::taker_gets][jss::value] == "1");
            BEAST_EXPECT(jroOuter[1u][jss::taker_pays] == "100000000");

            BEAST_EXPECT(jroOuter[2u][jss::quality] == "5000000");
            BEAST_EXPECT(jroOuter[2u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(
                jroOuter[2u][jss::taker_gets][jss::issuer] == gw.human());
            BEAST_EXPECT(jroOuter[2u][jss::taker_gets][jss::value] == "6");
            BEAST_EXPECT(jroOuter[2u][jss::taker_pays] == "30000000");
        }

        {
            // now make a limit (= 1) query for the same data
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = 1u;
            auto const jrr_l_1 = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            auto const& jro_l_1 = jrr_l_1[jss::offers];
            // there is a difference in the validation of the limit param
            // between admin and non-admin requests. with admin requests, the
            // limit parameter is NOT subject to sane defaults, but with a
            // non-admin there are pre-configured limit ranges applied. That's
            // why we have different BEAST_EXPECT()s here for the two scenarios
            BEAST_EXPECT(checkArraySize(jro_l_1, asAdmin ? 1u : 3u));
            BEAST_EXPECT(
                asAdmin ? checkMarker(jrr_l_1)
                        : (!jrr_l_1.isMember(jss::marker)));
            if (asAdmin)
            {
                BEAST_EXPECT(jroOuter[0u] == jro_l_1[0u]);

                // second item...with previous marker passed
                jvParams[jss::marker] = jrr_l_1[jss::marker];
                auto const jrr_l_2 = env.rpc(
                    "json",
                    "account_offers",
                    jvParams.toStyledString())[jss::result];
                auto const& jro_l_2 = jrr_l_2[jss::offers];
                BEAST_EXPECT(checkMarker(jrr_l_2));
                BEAST_EXPECT(checkArraySize(jro_l_2, 1u));
                BEAST_EXPECT(jroOuter[1u] == jro_l_2[0u]);

                // last item...with previous marker passed
                jvParams[jss::marker] = jrr_l_2[jss::marker];
                jvParams[jss::limit] = 10u;
                auto const jrr_l_3 = env.rpc(
                    "json",
                    "account_offers",
                    jvParams.toStyledString())[jss::result];
                auto const& jro_l_3 = jrr_l_3[jss::offers];
                BEAST_EXPECT(!jrr_l_3.isMember(jss::marker));
                BEAST_EXPECT(checkArraySize(jro_l_3, 1u));
                BEAST_EXPECT(jroOuter[2u] == jro_l_3[0u]);
            }
            else
            {
                BEAST_EXPECT(jroOuter == jro_l_1);
            }
        }

        {
            // now make a limit (= 0) query for the same data
            // since we operate on the admin port, the limit
            // value of 0 is not adjusted into tuned ranges for admin requests
            // so we literally get 0 elements in that case. For non-admin
            // requests, we get limit defaults applied thus all our results
            // come back (we are below the min results limit)
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = 0u;
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            auto const& jro = jrr[jss::offers];
            if (asAdmin)
            {
                // limit == 0 is invalid
                BEAST_EXPECT(jrr.isMember(jss::error_message));
            }
            else
            {
                // Call should enforce min limit of 10
                BEAST_EXPECT(checkArraySize(jro, 3u));
                BEAST_EXPECT(!jrr.isMember(jss::marker));
            }
        }
    }

    void
    testBadInput()
    {
        testcase("Bad input");

        using namespace jtx;
        Env env(*this);
        Account const gw("G1");
        auto const USD_gw = gw["USD"];
        Account const bob("bob");
        auto const USD_bob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(USD_gw(1000), bob);

        {
            // no account field
            auto const jrr = env.rpc("account_offers");
            BEAST_EXPECT(jrr[jss::error] == "badSyntax");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Syntax error.");
        }

        {
            // test account non-string
            auto testInvalidAccountParam = [&](auto const& param) {
                Json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc(
                    "json", "account_offers", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(Json::Value(Json::nullValue));
            testInvalidAccountParam(Json::Value(Json::objectValue));
            testInvalidAccountParam(Json::Value(Json::arrayValue));
        }

        {
            // empty string account
            Json::Value jvParams;
            jvParams[jss::account] = "";
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "actMalformed");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Account malformed.");
        }

        {
            // bogus account value
            auto const jrr = env.rpc(
                "account_offers", Account("bogus").human())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "actNotFound");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Account not found.");
        }

        {
            // bad limit
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = "0";  // NOT an integer
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }

        {
            // invalid marker
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::marker] = "NOT_A_MARKER";
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECTS(
                jrr[jss::error_message] == "Invalid field 'marker'.",
                jrr.toStyledString());
        }

        {
            // invalid marker - not a string
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::marker] = 1;
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'marker', not string.");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc(
                "json",
                "account_offers",
                jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }
    }

    void
    run() override
    {
        testSequential(true);
        testSequential(false);
        testBadInput();
        testNonAdminMinLimit();
    }
};

BEAST_DEFINE_TESTSUITE(AccountOffers, rpc, ripple);

}  // namespace test
}  // namespace ripple
