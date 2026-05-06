
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

class OwnerInfo_test : public beast::unit_test::Suite
{
    void
    testBadInput()
    {
        testcase("Bad input to owner_info");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        {  // missing account field
            auto const result = env.rpc("json", "owner_info", "{}")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Missing field 'account'.");
        }

        {  // ask for empty account
            json::Value params;
            params[jss::account] = "";
            auto const result = env.rpc("json", "owner_info", to_string(params))[jss::result];
            if (BEAST_EXPECT(result.isMember(jss::accepted) && result.isMember(jss::current)))
            {
                BEAST_EXPECT(result[jss::accepted][jss::error] == "actMalformed");
                BEAST_EXPECT(result[jss::accepted][jss::error_message] == "Account malformed.");
                BEAST_EXPECT(result[jss::current][jss::error] == "actMalformed");
                BEAST_EXPECT(result[jss::current][jss::error_message] == "Account malformed.");
            }
        }

        {  // ask for nonexistent account
           // this seems like it should be an error, but current impl
           // (deprecated) does not return an error, just empty fields.
            json::Value params;
            params[jss::account] = Account{"bob"}.human();
            auto const result = env.rpc("json", "owner_info", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::accepted] == json::ObjectValue);
            BEAST_EXPECT(result[jss::current] == json::ObjectValue);
            BEAST_EXPECT(result[jss::status] == "success");
        }
    }

    void
    testBasic()
    {
        testcase("Basic request for owner_info");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        auto const gw = Account{"gateway"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        auto const usd = gw["USD"];
        auto const cny = gw["CNY"];
        env(trust(alice, usd(1000)));
        env(trust(alice, cny(1000)));
        env(offer(alice, usd(1), XRP(1000)));
        env.close();

        env(pay(gw, alice, usd(50)));
        env(pay(gw, alice, cny(50)));
        env(offer(alice, cny(2), XRP(1000)));

        json::Value params;
        params[jss::account] = alice.human();
        auto const result = env.rpc("json", "owner_info", to_string(params))[jss::result];
        if (!BEAST_EXPECT(result.isMember(jss::accepted) && result.isMember(jss::current)))
        {
            return;
        }

        // accepted ledger entry
        if (!BEAST_EXPECT(result[jss::accepted].isMember(jss::ripple_lines)))
            return;
        auto lines = result[jss::accepted][jss::ripple_lines];
        if (!BEAST_EXPECT(lines.isArray() && lines.size() == 2))
            return;

        BEAST_EXPECT(
            lines[0u][sfBalance.fieldName] ==
            (STAmount{Issue{toCurrency("CNY"), noAccount()}, 0}.value().getJson(
                JsonOptions::KNone)));
        BEAST_EXPECT(
            lines[0u][sfHighLimit.fieldName] ==
            alice["CNY"](1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(
            lines[0u][sfLowLimit.fieldName] == gw["CNY"](0).value().getJson(JsonOptions::KNone));

        BEAST_EXPECT(
            lines[1u][sfBalance.fieldName] ==
            (STAmount{Issue{toCurrency("USD"), noAccount()}, 0}.value().getJson(
                JsonOptions::KNone)));
        BEAST_EXPECT(
            lines[1u][sfHighLimit.fieldName] ==
            alice["USD"](1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(lines[1u][sfLowLimit.fieldName] == usd(0).value().getJson(JsonOptions::KNone));

        if (!BEAST_EXPECT(result[jss::accepted].isMember(jss::offers)))
            return;
        auto offers = result[jss::accepted][jss::offers];
        if (!BEAST_EXPECT(offers.isArray() && offers.size() == 1))
            return;

        BEAST_EXPECT(offers[0u][jss::Account] == alice.human());
        BEAST_EXPECT(
            offers[0u][sfTakerGets.fieldName] == XRP(1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(
            offers[0u][sfTakerPays.fieldName] == usd(1).value().getJson(JsonOptions::KNone));

        // current ledger entry
        if (!BEAST_EXPECT(result[jss::current].isMember(jss::ripple_lines)))
            return;
        lines = result[jss::current][jss::ripple_lines];
        if (!BEAST_EXPECT(lines.isArray() && lines.size() == 2))
            return;

        BEAST_EXPECT(
            lines[0u][sfBalance.fieldName] ==
            (STAmount{Issue{toCurrency("CNY"), noAccount()}, -50}.value().getJson(
                JsonOptions::KNone)));
        BEAST_EXPECT(
            lines[0u][sfHighLimit.fieldName] ==
            alice["CNY"](1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(
            lines[0u][sfLowLimit.fieldName] == gw["CNY"](0).value().getJson(JsonOptions::KNone));

        BEAST_EXPECT(
            lines[1u][sfBalance.fieldName] ==
            (STAmount{Issue{toCurrency("USD"), noAccount()}, -50}.value().getJson(
                JsonOptions::KNone)));
        BEAST_EXPECT(
            lines[1u][sfHighLimit.fieldName] ==
            alice["USD"](1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(
            lines[1u][sfLowLimit.fieldName] == gw["USD"](0).value().getJson(JsonOptions::KNone));

        if (!BEAST_EXPECT(result[jss::current].isMember(jss::offers)))
            return;
        offers = result[jss::current][jss::offers];
        // 1 additional offer in current, (2 total)
        if (!BEAST_EXPECT(offers.isArray() && offers.size() == 2))
            return;

        BEAST_EXPECT(offers[1u] == result[jss::accepted][jss::offers][0u]);
        BEAST_EXPECT(offers[0u][jss::Account] == alice.human());
        BEAST_EXPECT(
            offers[0u][sfTakerGets.fieldName] == XRP(1000).value().getJson(JsonOptions::KNone));
        BEAST_EXPECT(
            offers[0u][sfTakerPays.fieldName] == cny(2).value().getJson(JsonOptions::KNone));
    }

public:
    void
    run() override
    {
        testBadInput();
        testBasic();
    }
};

BEAST_DEFINE_TESTSUITE(OwnerInfo, rpc, xrpl);

}  // namespace xrpl
