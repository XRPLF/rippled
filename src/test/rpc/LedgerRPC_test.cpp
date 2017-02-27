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
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class LedgerRPC_test : public beast::unit_test::suite
{
    void
    checkErrorValue(
        Json::Value const& jv,
        std::string const& err,
        std::string const& msg)
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECT(jv[jss::status] == "error");
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECT(jv[jss::error] == err);
        if (msg.empty())
        {
            BEAST_EXPECT(
                jv[jss::error_message] == Json::nullValue ||
                jv[jss::error_message] == "");
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
            BEAST_EXPECT(jv[jss::error_message] == msg);
    };

    void testLedgerRequest()
    {
        testcase("Basic Request");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        BEAST_EXPECT(env.current()->info().seq == 4);

        {
            // in this case, numeric string converted to number
            auto const jrr = env.rpc("ledger", "1") [jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::closed] == true);
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "1");
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

    void testBadInput()
    {
        testcase("Bad Input");
        using namespace test::jtx;
        Env env { *this };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        Account const bob { "bob" };

        env.fund(XRP(10000), gw, bob);
        env.close();
        env.trust(USD(1000), bob);
        env.close();

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "0"; // NOT an integer
            auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
            checkErrorValue(jrr, "invalidParams", "ledgerIndexMalformed");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }

        {
            // unrecognized string arg -- error
            auto const jrr = env.rpc("ledger", "arbitrary_text") [jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }

    }

    void testLedgerCurrent()
    {
        testcase("ledger_current Request");
        using namespace test::jtx;

        Env env {*this};

        env.close();
        BEAST_EXPECT(env.current()->info().seq == 4);

        {
            auto const jrr = env.rpc("ledger_current") [jss::result];
            BEAST_EXPECT(jrr[jss::ledger_current_index] == env.current()->info().seq);
        }
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

    void testLedgerFull()
    {
        testcase("Ledger Request, Full Option");
        using namespace test::jtx;

        Env env {*this};

        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::full] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 2u);
    }

    void testLedgerFullNonAdmin()
    {
        testcase("Ledger Request, Full Option Without Admin");
        using namespace test::jtx;

        Env env { *this, std::make_unique<Config>(nonAdminConf) };

        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::full] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        checkErrorValue(jrr, "noPermission", "You don't have permission for this command."); }

    void testLedgerAccounts()
    {
        testcase("Ledger Request, Accounts Option");
        using namespace test::jtx;

        Env env {*this};

        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::accounts] = true;
        auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 2u);
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
        checkErrorValue(jrr, "malformedAddress", "");
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
        checkErrorValue(jrr, "entryNotFound", "");
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

        {
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = jrr[jss::ledger_hash];
            jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
        }
        {
            Json::Value jvParams;
            jvParams[jss::index] = jrr[jss::index];
            jrr = env.rpc ( "json", "ledger_entry", to_string(jvParams) ) [jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] ==
                "1100612200800000240000000225000000032D00000000554294BEBE5B569"
                "A18C0A2702387C9B1E7146DC3A5850C1E87204951C6FDAA4C426240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D");
        }
    }

public:
    void run ()
    {
        testLedgerRequest();
        testBadInput();
        testLedgerCurrent();
        testAccountRoot();
        testLedgerFull();
        testLedgerFullNonAdmin();
        testLedgerAccounts();
        testMalformedAccountRoot();
        testNotFoundAccountRoot();
        testAccountRootFromIndex();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC,app,ripple);

} // ripple

