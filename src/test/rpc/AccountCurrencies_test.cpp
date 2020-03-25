//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/jss.h>

namespace ripple {

class AccountCurrencies_test : public beast::unit_test::suite
{
    void
    testBadInput ()
    {
        testcase ("Bad input to account_currencies");

        using namespace test::jtx;
        Env env {*this};

        auto const alice = Account {"alice"};
        env.fund (XRP(10000), alice);
        env.close ();

        { // invalid ledger (hash)
            Json::Value params;
            params[jss::ledger_hash] = 1;
            auto const result = env.rpc ("json", "account_currencies",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "ledgerHashNotString");
        }

        { // missing account field
            auto const result =
                env.rpc ("json", "account_currencies", "{}") [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Missing field 'account'.");
        }

        { // strict mode, invalid bitcoin token
            Json::Value params;
            params[jss::account] = "llIIOO"; //these are invalid in bitcoin alphabet
            params[jss::strict] = true;
            auto const result = env.rpc ("json", "account_currencies",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "actMalformed");
            BEAST_EXPECT (result[jss::error_message] ==
                "Account malformed.");
        }

        { // strict mode, using properly formatted bitcoin token
            Json::Value params;
            params[jss::account] = base58EncodeTokenBitcoin (
                TokenType::AccountID, alice.id().data(), alice.id().size());
            params[jss::strict] = true;
            auto const result = env.rpc ("json", "account_currencies",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "actBitcoin");
            BEAST_EXPECT (result[jss::error_message] ==
                "Account is bitcoin address.");
        }

        { // ask for nonexistent account
            Json::Value params;
            params[jss::account] = Account{"bob"}.human();
            auto const result = env.rpc ("json", "account_currencies",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "actNotFound");
            BEAST_EXPECT (result[jss::error_message] ==
                "Account not found.");
        }
    }

    void
    testBasic ()
    {
        testcase ("Basic request for account_currencies");

        using namespace test::jtx;
        Env env {*this};

        auto const alice = Account {"alice"};
        auto const gw = Account {"gateway"};
        env.fund (XRP(10000), alice, gw);
        char currencySuffix {'A'};
        std::vector<boost::optional<IOU>> gwCurrencies (26); // A - Z
        std::generate (gwCurrencies.begin(), gwCurrencies.end(),
            [&]()
            {
                auto gwc = gw[std::string("US") + currencySuffix++];
                env (trust (alice, gwc (100)));
                return gwc;
            });
        env.close ();

        Json::Value params;
        params[jss::account] = alice.human();
        auto result = env.rpc ("json", "account_currencies",
            boost::lexical_cast<std::string>(params)) [jss::result];

        auto arrayCheck =
            [&result] (
                Json::StaticString const& fld,
                std::vector<boost::optional<IOU>> const& expected) -> bool
            {
                bool stat =
                    result.isMember (fld) &&
                    result[fld].isArray() &&
                    result[fld].size() == expected.size();
                for (size_t i = 0; stat && i < expected.size(); ++i)
                {
                    Currency foo;
                    stat &= (
                        to_string(expected[i].value().currency) ==
                        result[fld][i].asString()
                    );
                }
                return stat;
            };

        BEAST_EXPECT (arrayCheck (jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT (arrayCheck (jss::send_currencies, {}));

        // now form a payment for each currency
        for (auto const& c : gwCurrencies)
            env (pay (gw, alice, c.value()(50)));

        // send_currencies should be populated now
        result = env.rpc ("json", "account_currencies",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (arrayCheck (jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT (arrayCheck (jss::send_currencies, gwCurrencies));

        // freeze the USD trust line and verify that the receive currencies
        // does not change
        env(trust(alice, gw["USD"](100), tfSetFreeze));
        result = env.rpc ("account_lines", alice.human());
        for (auto const& l : result[jss::lines])
            BEAST_EXPECT(
                l[jss::freeze].asBool() == (l[jss::currency] == "USD"));
        result = env.rpc ("json", "account_currencies",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (arrayCheck (jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT (arrayCheck (jss::send_currencies, gwCurrencies));
        // clear the freeze
        env(trust(alice, gw["USD"](100), tfClearFreeze));

        // make a payment that exhausts the trustline from alice to gw for USA
        env (pay (gw, alice, gw["USA"](50)));
        // USA should now be missing from receive_currencies
        result = env.rpc ("json", "account_currencies",
            boost::lexical_cast<std::string>(params)) [jss::result];
        decltype(gwCurrencies) gwCurrenciesNoUSA (gwCurrencies.begin() + 1,
            gwCurrencies.end());
        BEAST_EXPECT (arrayCheck (jss::receive_currencies, gwCurrenciesNoUSA));
        BEAST_EXPECT (arrayCheck (jss::send_currencies, gwCurrencies));

        // add trust from gw to alice and then exhaust that trust line
        // so that send_currencies for alice will now omit USA
        env (trust (gw, alice["USA"] (100)));
        env (pay (alice, gw, alice["USA"](200)));
        result = env.rpc ("json", "account_currencies",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (arrayCheck (jss::receive_currencies, gwCurrencies));
        BEAST_EXPECT (arrayCheck (jss::send_currencies, gwCurrenciesNoUSA));
    }

public:
    void run () override
    {
        testBadInput ();
        testBasic ();
    }
};

BEAST_DEFINE_TESTSUITE(AccountCurrencies,app,ripple);

} // ripple

