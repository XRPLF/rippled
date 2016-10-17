//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class LedgerRPC_test : public beast::unit_test::suite
{
    static
    std::unique_ptr<Config>
    makeNonAdminConfig()
    {
        auto p = std::make_unique<Config>();
        test::setupConfigForUnitTests(*p);
        (*p)["port_rpc"].set("admin","");
        (*p)["port_ws"].set("admin","");
        return p;
    }

public:
    void testBadInput()
    {
        testcase("Bad Input");
        using namespace test::jtx;
        Env env { *this };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        Account const bob { "bob" };

        env.fund(XRP(10000), gw, bob);
        env.trust(USD(1000), bob);
        env.close();

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "0"; // NOT an integer
            auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            // arbitrary text is converted to 0.
            auto const jrr = env.rpc("ledger", "arbitrary_text") [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

    }

    void testLedgerRequest()
    {
        testcase("Basic Request");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const jrr = env.rpc("ledger", "1") [jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::accepted] == true);
            BEAST_EXPECT(jrr[jss::ledger][jss::totalCoins] == env.balance(env.master).value().getText());
        }

        {
            // using current identifier
            auto const jrr = env.rpc("ledger", "current") [jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::closed] == false);
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == to_string(env.current()->info().seq));
            BEAST_EXPECT(jrr[jss::ledger_current_index] == env.current()->info().seq);
        }
    }

    void testLedgerCurrent()
    {
        testcase("ledger_current Request");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const jrr = env.rpc("ledger_current") [jss::result];
            BEAST_EXPECT(jrr[jss::ledger_current_index] == env.current()->info().seq);
        }
    }

    void testLedgerFull()
    {
        testcase("Ledger Request, Full Option");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::full] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() > 0);
    }

    void testLedgerFullNonAdmin()
    {
        testcase("Ledger Request, Full Option Without Admin");
        using namespace test::jtx;

        Env env { *this, makeNonAdminConfig() };

        env.close();
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::full] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::error]         == "noPermission");
        BEAST_EXPECT(jrr[jss::status]        == "error");
        BEAST_EXPECT(jrr[jss::error_message] == "You don't have permission for this command.");
    }

    void testLedgerAccounts()
    {
        testcase("Ledger Request, Accounts Option");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::accounts] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() > 0);
    }

    void testAccountRoot()
    {
        testcase("Basic Ledger Entry Request");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto jrr = env.rpc("ledger_closed") [jss::result];
        BEAST_EXPECT(jrr[jss::ledger_hash] == to_string(env.closed()->info().hash));
        BEAST_EXPECT(jrr[jss::ledger_index] == 3);

        Json::Value jvParams;
        jvParams[jss::account_root] = alice.human();
        jvParams[jss::ledger_hash] = jrr[jss::ledger_hash];
        jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr.isMember(jss::node));
        BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName] == XRP(10000).value().getText());
    }

    void testMalformedAccountRoot()
    {
        testcase("Malformed Ledger Entry Request");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto jrr = env.rpc("ledger_closed") [jss::result];

        Json::Value jvParams;
        jvParams[jss::account_root] = std::string(alice.human()).replace(0, 2, 2, 'x');
        jvParams[jss::ledger_hash] = jrr[jss::ledger_hash];
        jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::error]         == "malformedAddress");
        BEAST_EXPECT(jrr[jss::status]        == "error");
        BEAST_EXPECT(jrr[jss::error_message] == Json::nullValue);
    }

    void testNotFoundAccountRoot()
    {
        testcase("Ledger Entry Not Found");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto jrr = env.rpc("ledger_closed") [jss::result];

        Json::Value jvParams;
        jvParams[jss::account_root] = Account("bob").human();
        jvParams[jss::ledger_hash] = jrr[jss::ledger_hash];
        jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::error]         == "entryNotFound");
        BEAST_EXPECT(jrr[jss::status]        == "error");
        BEAST_EXPECT(jrr[jss::error_message] == Json::nullValue);
    }

    void testAccountRootFromIndex()
    {
        testcase("Ledger Entry Request From Index");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto jrr = env.rpc("ledger_closed") [jss::result];
        BEAST_EXPECT(jrr[jss::ledger_hash] == to_string(env.closed()->info().hash));
        BEAST_EXPECT(jrr[jss::ledger_index] == 3);

        Json::Value jvParams;
        jvParams[jss::account_root] = alice.human();
        jvParams[jss::ledger_hash] = jrr[jss::ledger_hash];
        jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        jvParams[jss::index] = jrr[jss::index];
        jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr.isMember(jss::node_binary));
    }

    void run ()
    {
        testLedgerRequest();
        testBadInput();
        testLedgerCurrent();
        testAccountRoot();
        testMalformedAccountRoot();
        testNotFoundAccountRoot();
        testAccountRootFromIndex();
        testLedgerFull();
        testLedgerFullNonAdmin();
        testLedgerAccounts();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC,app,ripple);

} // ripple

