
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class AccountOffers_test : public beast::unit_test::Suite
{
public:
    // test helper
    static bool
    checkMarker(json::Value const& val)
    {
        return val.isMember(jss::marker) && val[jss::marker].isString() &&
            !val[jss::marker].asString().empty();
    }

    void
    testNonAdminMinLimit()
    {
        testcase("Non-Admin Min Limit");

        using namespace jtx;
        Env env{*this, envconfig(noAdmin)};
        Account const gw("G1");
        auto const usdGw = gw["USD"];
        Account const bob("bob");
        auto const usdBob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(usdGw(1000), bob);

        // this is to provide some USD from gw in the
        // bob account so that it can rightly
        // make offers that give those USDs
        env(pay(gw, bob, usdGw(10)));
        unsigned const offerCount = 12u;
        for (auto i = 0u; i < offerCount; i++)
        {
            json::Value jvo = offer(bob, XRP(100 + i), usdGw(1));
            jvo[sfExpiration.fieldName] = 10000000u;
            env(jvo);
        }

        // make non-limited RPC call
        auto const jroNl = env.rpc("account_offers", bob.human())[jss::result][jss::offers];
        BEAST_EXPECT(checkArraySize(jroNl, offerCount));

        // now make a low-limit query, should get "corrected"
        // to a min of 10 results with a marker set since there
        // are more than 10 total
        json::Value jvParams;
        jvParams[jss::account] = bob.human();
        jvParams[jss::limit] = 1u;
        auto const jrrL = env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
        auto const& jroL = jrrL[jss::offers];
        BEAST_EXPECT(checkMarker(jrrL));
        // 9u is the expected size, since one account object is a trustline
        BEAST_EXPECT(checkArraySize(jroL, 9u));
    }

    void
    testSequential(bool asAdmin)
    {
        testcase(std::string("Sequential - ") + (asAdmin ? "admin" : "non-admin"));

        using namespace jtx;
        Env env{*this, asAdmin ? envconfig() : envconfig(noAdmin)};
        Account const gw("G1");
        auto const usdGw = gw["USD"];
        Account const bob("bob");
        auto const usdBob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(usdGw(1000), bob);

        // this is to provide some USD from gw in the
        // bob account so that it can rightly
        // make offers that give those USDs
        env(pay(gw, bob, usdGw(10)));

        env(offer(bob, XRP(100), usdBob(1)));
        env(offer(bob, XRP(200), usdGw(2)));
        env(offer(bob, XRP(30), usdGw(6)));

        // make the RPC call
        auto const jroOuter = env.rpc("account_offers", bob.human())[jss::result][jss::offers];
        if (BEAST_EXPECT(checkArraySize(jroOuter, 3u)))
        {
            // Note that the returned offers are sorted by index, not by
            // order of insertion or by sequence number.  There is no
            // guarantee that their order will not change in the future
            // if the sequence numbers or the account IDs change.
            BEAST_EXPECT(jroOuter[0u][jss::quality] == "100000000");
            BEAST_EXPECT(jroOuter[0u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(jroOuter[0u][jss::taker_gets][jss::issuer] == gw.human());
            BEAST_EXPECT(jroOuter[0u][jss::taker_gets][jss::value] == "2");
            BEAST_EXPECT(jroOuter[0u][jss::taker_pays] == "200000000");

            BEAST_EXPECT(jroOuter[1u][jss::quality] == "100000000");
            BEAST_EXPECT(jroOuter[1u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(jroOuter[1u][jss::taker_gets][jss::issuer] == bob.human());
            BEAST_EXPECT(jroOuter[1u][jss::taker_gets][jss::value] == "1");
            BEAST_EXPECT(jroOuter[1u][jss::taker_pays] == "100000000");

            BEAST_EXPECT(jroOuter[2u][jss::quality] == "5000000");
            BEAST_EXPECT(jroOuter[2u][jss::taker_gets][jss::currency] == "USD");
            BEAST_EXPECT(jroOuter[2u][jss::taker_gets][jss::issuer] == gw.human());
            BEAST_EXPECT(jroOuter[2u][jss::taker_gets][jss::value] == "6");
            BEAST_EXPECT(jroOuter[2u][jss::taker_pays] == "30000000");
        }

        {
            // now make a limit (= 1) query for the same data
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = 1u;
            auto const jrrL1 =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            auto const& jroL1 = jrrL1[jss::offers];
            // there is a difference in the validation of the limit param
            // between admin and non-admin requests. with admin requests, the
            // limit parameter is NOT subject to sane defaults, but with a
            // non-admin there are pre-configured limit ranges applied. That's
            // why we have different BEAST_EXPECT()s here for the two scenarios
            BEAST_EXPECT(checkArraySize(jroL1, asAdmin ? 1u : 3u));
            BEAST_EXPECT(asAdmin ? checkMarker(jrrL1) : (!jrrL1.isMember(jss::marker)));
            if (asAdmin)
            {
                BEAST_EXPECT(jroOuter[0u] == jroL1[0u]);

                // second item...with previous marker passed
                jvParams[jss::marker] = jrrL1[jss::marker];
                auto const jrrL2 =
                    env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
                auto const& jroL2 = jrrL2[jss::offers];
                BEAST_EXPECT(checkMarker(jrrL2));
                BEAST_EXPECT(checkArraySize(jroL2, 1u));
                BEAST_EXPECT(jroOuter[1u] == jroL2[0u]);

                // last item...with previous marker passed
                jvParams[jss::marker] = jrrL2[jss::marker];
                jvParams[jss::limit] = 10u;
                auto const jrrL3 =
                    env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
                auto const& jroL3 = jrrL3[jss::offers];
                BEAST_EXPECT(!jrrL3.isMember(jss::marker));
                BEAST_EXPECT(checkArraySize(jroL3, 1u));
                BEAST_EXPECT(jroOuter[2u] == jroL3[0u]);
            }
            else
            {
                BEAST_EXPECT(jroOuter == jroL1);
            }
        }

        {
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = 0u;
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::error_message));
        }
    }

    void
    testBadInput()
    {
        testcase("Bad input");

        using namespace jtx;
        Env env(*this);
        Account const gw("G1");
        auto const usdGw = gw["USD"];
        Account const bob("bob");
        auto const usdBob = bob["USD"];

        env.fund(XRP(10000), gw, bob);
        env.trust(usdGw(1000), bob);

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
                json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc("json", "account_offers", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(json::Value(json::NullValue));
            testInvalidAccountParam(json::Value(json::ObjectValue));
            testInvalidAccountParam(json::Value(json::ArrayValue));
        }

        {
            // empty string account
            json::Value jvParams;
            jvParams[jss::account] = "";
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "actMalformed");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Account malformed.");
        }

        {
            // bogus account value
            auto const jrr = env.rpc("account_offers", Account("bogus").human())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "actNotFound");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Account not found.");
        }

        {
            // bad limit
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::limit] = "0";  // NOT an integer
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'limit', not unsigned integer.");
        }

        {
            // invalid marker
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::marker] = "NOT_A_MARKER";
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECTS(
                jrr[jss::error_message] == "Invalid field 'marker'.", jrr.toStyledString());
        }

        {
            // invalid marker - not a string
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::marker] = 1;
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'marker', not string.");
        }

        {
            // ask for a bad ledger index
            json::Value jvParams;
            jvParams[jss::account] = bob.human();
            jvParams[jss::ledger_index] = 10u;
            auto const jrr =
                env.rpc("json", "account_offers", jvParams.toStyledString())[jss::result];
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

BEAST_DEFINE_TESTSUITE(AccountOffers, rpc, xrpl);

}  // namespace xrpl::test
