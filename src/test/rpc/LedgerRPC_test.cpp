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

        Env env { *this, makeNonAdminConfig() };

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

    /// @brief ledger RPC requests as a way to drive
    /// input options to lookupLedger. The point of this test is
    /// coverage for lookupLedger, not so much the ledger
    /// RPC request.
    void testLookupLedger()
    {
        using namespace test::jtx;
        Env env { *this };
        env.fund(XRP(10000), "alice");
        env.close();
        env.fund(XRP(10000), "bob");
        env.close();
        env.fund(XRP(10000), "jim");
        env.close();
        env.fund(XRP(10000), "jill");

        // closed ledger hashes are:
        //1 - AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7
        //2 - 8AEDBB96643962F1D40F01E25632ABB3C56C9F04B0231EE4B18248B90173D189
        //3 - 7C3EEDB3124D92E49E75D81A8826A2E65A75FD71FC3FD6F36FEB803C5F1D812D
        //4 - 9F9E6A4ECAA84A08FF94713FA41C3151177D6222EA47DD2F0020CA49913EE2E6
        //5 - C516522DE274EB52CE69A3D22F66DD73A53E16597E06F7A86F66DF7DD4309173
        //
        {
            //access via the legacy ledger field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger] = "closed";
            auto jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "validated";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "current";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");

            // ask for a bad ledger keyword
            jvParams[jss::ledger] = "invalid";
            jrr = env.rpc ( "json", "ledger",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            jvParams[jss::ledger] = 4;
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "4");

            // numeric index - out of range
            jvParams[jss::ledger] = 20;
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            //access via the ledger_hash field
            Json::Value jvParams;
            jvParams[jss::ledger_hash] =
                "7C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            auto jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // extra leading hex chars in hash will be ignored
            jvParams[jss::ledger_hash] =
                "DEADBEEF"
                "7C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // request with non-string ledger_hash
            jvParams[jss::ledger_hash] = 2;
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashNotString");

            // malformed (non hex) hash
            jvParams[jss::ledger_hash] =
                "ZZZZZZZZZZZD92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // properly formed, but just doesn't exist
            jvParams[jss::ledger_hash] =
                "8C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            //access via the ledger_index field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            auto jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr.isMember(jss::ledger_index));

            jvParams[jss::ledger_index] = "validated";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger_index] = "current";
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");
            BEAST_EXPECT(jrr.isMember(jss::ledger_current_index));

            // ask for a bad ledger keyword
            jvParams[jss::ledger_index] = "invalid";
            jrr = env.rpc ( "json", "ledger",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            for (auto i : {1, 2, 3, 4 ,5, 6})
            {
                jvParams[jss::ledger_index] = i;
                jrr = env.rpc("json", "ledger",
                    boost::lexical_cast<std::string>(jvParams))[jss::result];
                BEAST_EXPECT(jrr.isMember(jss::ledger));
                if(i < 6)
                    BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
                BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == std::to_string(i));
            }

            // numeric index - out of range
            jvParams[jss::ledger_index] = 7;
            jrr = env.rpc("json", "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
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
        testLookupLedger();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC,app,ripple);

} // ripple

