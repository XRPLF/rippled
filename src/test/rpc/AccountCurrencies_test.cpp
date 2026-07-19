
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

class AccountCurrencies_test : public beast::unit_test::Suite
{
    void
    testBadInput()
    {
        testcase("Bad input to account_currencies");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        {  // invalid ledger (hash)
            json::Value params;
            params[jss::account] = Account{"bob"}.human();
            params[jss::ledger_hash] = 1;
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(
                result[jss::error_message] == "Invalid field 'ledger_hash', not hex string.");
        }

        {  // missing account field
            auto const result = env.rpc("json", "account_currencies", "{}")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Missing field 'account'.");
        }

        {
            // test account non-string
            auto testInvalidAccountParam = [&](auto const& param) {
                json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc("json", "account_currencies", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(json::Value(json::ValueType::Null));
            testInvalidAccountParam(json::Value(json::ValueType::Object));
            testInvalidAccountParam(json::Value(json::ValueType::Array));
        }

        {
            // test ident non-string
            auto testInvalidIdentParam = [&](auto const& param) {
                json::Value params;
                params[jss::ident] = param;
                auto jrr = env.rpc("json", "account_currencies", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'ident'.");
            };

            testInvalidIdentParam(1);
            testInvalidIdentParam(1.1);
            testInvalidIdentParam(true);
            testInvalidIdentParam(json::Value(json::ValueType::Null));
            testInvalidIdentParam(json::Value(json::ValueType::Object));
            testInvalidIdentParam(json::Value(json::ValueType::Array));
        }

        {
            // test expanded non-boolean
            auto testInvalidExpandedParam = [&](auto const& param) {
                json::Value params;
                params[jss::account] = alice.human();
                params[jss::expanded] = param;
                auto jrr = env.rpc("json", "account_currencies", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'expanded'.");
            };

            testInvalidExpandedParam(1);
            testInvalidExpandedParam("true");
            testInvalidExpandedParam(json::Value(json::ValueType::Null));
            testInvalidExpandedParam(json::Value(json::ValueType::Object));
            testInvalidExpandedParam(json::Value(json::ValueType::Array));
        }

        {
            json::Value params;
            params[jss::account] = "llIIOO";  // these are invalid in bitcoin alphabet
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "actMalformed");
            BEAST_EXPECT(result[jss::error_message] == "Account malformed.");
        }

        {
            // Cannot use a seed as account
            json::Value params;
            params[jss::account] = "Bob";
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "actMalformed");
            BEAST_EXPECT(result[jss::error_message] == "Account malformed.");
        }

        {  // ask for nonexistent account
            json::Value params;
            params[jss::account] = Account{"bob"}.human();
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "actNotFound");
            BEAST_EXPECT(result[jss::error_message] == "Account not found.");
        }
    }

    void
    testBasic()
    {
        testcase("Basic request for account_currencies");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        auto const gw = Account{"gateway"};
        env.fund(XRP(10000), alice, gw);
        char currencySuffix{'A'};
        std::vector<std::optional<IOU>> gwCurrencies(26);  // A - Z
        std::ranges::generate(gwCurrencies, [&]() {
            auto gwc = gw[std::string("US") + currencySuffix++];
            env(trust(alice, gwc(100)));
            return gwc;
        });
        env.close();

        json::Value params;
        params[jss::account] = alice.human();
        auto result = env.rpc("json", "account_currencies", to_string(params))[jss::result];

        auto arrayCheck = [&result](
                              json::StaticString const& fld,
                              std::vector<std::optional<IOU>> const& expected) -> bool {
            bool stat = result.isMember(fld) && result[fld].isArray() &&
                result[fld].size() == expected.size();
            for (size_t i = 0; stat && i < expected.size(); ++i)
            {
                stat &= (to_string(expected[i].value().currency) == result[fld][i].asString());
            }
            return stat;
        };

        BEAST_EXPECT(arrayCheck(jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT(arrayCheck(jss::send_currencies, {}));

        // now form a payment for each currency
        for (auto const& c : gwCurrencies)
            env(pay(gw, alice, c.value()(50)));  // NOLINT(bugprone-unchecked-optional-access)

        // send_currencies should be populated now
        result = env.rpc("json", "account_currencies", to_string(params))[jss::result];
        BEAST_EXPECT(arrayCheck(jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT(arrayCheck(jss::send_currencies, gwCurrencies));

        // freeze the USD trust line and verify that the receive currencies
        // does not change
        env(trust(alice, gw["USD"](100), tfSetFreeze));
        result = env.rpc("account_lines", alice.human());
        for (auto const& l : result[jss::lines])
            BEAST_EXPECT(l[jss::freeze].asBool() == (l[jss::currency] == "USD"));
        result = env.rpc("json", "account_currencies", to_string(params))[jss::result];
        BEAST_EXPECT(arrayCheck(jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT(arrayCheck(jss::send_currencies, gwCurrencies));
        // clear the freeze
        env(trust(alice, gw["USD"](100), tfClearFreeze));

        // make a payment that exhausts the trustline from alice to gw for USA
        env(pay(gw, alice, gw["USA"](50)));
        // USA should now be missing from receive_currencies
        result = env.rpc("json", "account_currencies", to_string(params))[jss::result];
        decltype(gwCurrencies)
            const gwCurrenciesNoUSA(gwCurrencies.begin() + 1, gwCurrencies.end());
        BEAST_EXPECT(arrayCheck(jss::receive_currencies, gwCurrenciesNoUSA));
        BEAST_EXPECT(arrayCheck(jss::send_currencies, gwCurrencies));

        // add trust from gw to alice and then exhaust that trust line
        // so that send_currencies for alice will now omit USA
        env(trust(gw, alice["USA"](100)));
        env(pay(alice, gw, alice["USA"](200)));
        result = env.rpc("json", "account_currencies", to_string(params))[jss::result];
        BEAST_EXPECT(arrayCheck(jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT(arrayCheck(jss::send_currencies, gwCurrenciesNoUSA));
    }

    void
    testExpanded()
    {
        testcase("Expanded request for account_currencies");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        auto const gw1 = Account{"gw1"};
        auto const gw2 = Account{"gw2"};
        env.fund(XRP(10000), alice, gw1, gw2);
        env.close();

        // Two trust lines with the same currency code but different issuers
        env(trust(alice, gw1["USD"](100)));
        env(trust(alice, gw2["USD"](100)));
        env.close();
        env(pay(gw1, alice, gw1["USD"](50)));
        env.close();

        json::Value params;
        params[jss::account] = alice.human();
        params[jss::expanded] = true;

        auto findEntry = [](json::Value const& array,
                            std::string const& currency,
                            std::string const& issuer) -> std::optional<json::Value> {
            for (auto const& entry : array)
            {
                if (entry[jss::currency].asString() == currency &&
                    entry[jss::issuer].asString() == issuer)
                    return entry;
            }
            return std::nullopt;
        };

        {
            // Both issuers are reported separately in receive_currencies,
            // only the funded line qualifies for send_currencies
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(
                result[jss::receive_currencies].isArray() &&
                result[jss::receive_currencies].size() == 2);
            BEAST_EXPECT(
                result[jss::send_currencies].isArray() && result[jss::send_currencies].size() == 1);
            auto const recv1 = findEntry(result[jss::receive_currencies], "USD", gw1.human());
            auto const recv2 = findEntry(result[jss::receive_currencies], "USD", gw2.human());
            auto const send1 = findEntry(result[jss::send_currencies], "USD", gw1.human());
            BEAST_EXPECT(recv1 && recv2 && send1);
            // value is the remaining capacity of the line: alice holds 50
            // out of a 100 limit from gw1, so she can receive 50 more and
            // send the 50 she holds
            BEAST_EXPECT(recv1 && (*recv1)[jss::value] == "50");
            BEAST_EXPECT(recv2 && (*recv2)[jss::value] == "100");
            BEAST_EXPECT(send1 && (*send1)[jss::value] == "50");
            // entries carry exactly currency, issuer and value
            for (auto const& entry : {recv1, recv2, send1})
                BEAST_EXPECT(entry && entry->size() == 3);
        }

        {
            // like the legacy format, membership and value ignore freeze
            // state: freezing the line changes nothing in the response
            env(trust(gw1, alice["USD"](0), tfSetFreeze));
            env.close();
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            auto const entry = findEntry(result[jss::receive_currencies], "USD", gw1.human());
            BEAST_EXPECT(entry && (*entry)[jss::value] == "50" && entry->size() == 3);
            env(trust(gw1, alice["USD"](0), tfClearFreeze));
            env.close();
        }

        {
            // a second currency from another issuer: entries report the
            // full remaining capacity of the line
            auto const gw3 = Account{"gw3"};
            env.fund(XRP(10000), gw3);
            env.close();
            env(trust(alice, gw3["EUR"](100)));
            env.close();

            auto result = env.rpc("json", "account_currencies", to_string(params))[jss::result];
            auto entry = findEntry(result[jss::receive_currencies], "EUR", gw3.human());
            BEAST_EXPECT(entry && (*entry)[jss::value] == "100");

            // gw3's own perspective: issuance capacity is reported as a
            // single aggregated entry with issuer = gw3 and no value
            json::Value gwParams;
            gwParams[jss::account] = gw3.human();
            gwParams[jss::expanded] = true;
            result = env.rpc("json", "account_currencies", to_string(gwParams))[jss::result];
            BEAST_EXPECT(result[jss::send_currencies].size() == 1);
            BEAST_EXPECT(result[jss::receive_currencies].size() == 0);
            entry = findEntry(result[jss::send_currencies], "EUR", gw3.human());
            BEAST_EXPECT(entry && !entry->isMember(jss::value));
            BEAST_EXPECT(entry && entry->size() == 2);
        }

        {
            // Issuer with multiple holders: self-issued entries are
            // aggregated to one entry per currency instead of one entry
            // per trust line
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), bob);
            env.close();
            env(trust(bob, gw1["USD"](200)));
            env.close();
            env(pay(gw1, bob, gw1["USD"](30)));
            env.close();

            json::Value gwParams;
            gwParams[jss::account] = gw1.human();
            gwParams[jss::expanded] = true;
            auto const result =
                env.rpc("json", "account_currencies", to_string(gwParams))[jss::result];
            // two holders (alice and bob), but a single aggregated entry
            BEAST_EXPECT(result[jss::send_currencies].size() == 1);
            BEAST_EXPECT(result[jss::receive_currencies].size() == 1);
            auto const sendEntry = findEntry(result[jss::send_currencies], "USD", gw1.human());
            auto const recvEntry = findEntry(result[jss::receive_currencies], "USD", gw1.human());
            BEAST_EXPECT(sendEntry && recvEntry);
            // aggregated self-issuance entries carry no value
            BEAST_EXPECT(sendEntry && !sendEntry->isMember(jss::value));
            BEAST_EXPECT(recvEntry && !recvEntry->isMember(jss::value));
        }

        {
            // expanded: false behaves exactly like the legacy format
            params[jss::expanded] = false;
            auto const result =
                env.rpc("json", "account_currencies", to_string(params))[jss::result];
            BEAST_EXPECT(
                result[jss::receive_currencies].isArray() &&
                result[jss::receive_currencies].size() == 2);
            for (auto const& c : result[jss::receive_currencies])
                BEAST_EXPECT(c.isString());
        }
    }

public:
    void
    run() override
    {
        testBadInput();
        testBasic();
        testExpanded();
    }
};

BEAST_DEFINE_TESTSUITE(AccountCurrencies, rpc, xrpl);

}  // namespace xrpl
