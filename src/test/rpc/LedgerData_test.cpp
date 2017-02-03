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

#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <test/jtx.h>

namespace ripple {

class LedgerData_test : public beast::unit_test::suite
{
public:

    static
    std::unique_ptr<Config>
    makeConfig(bool setup_admin)
    {
        auto p = std::make_unique<Config>();
        test::setupConfigForUnitTests(*p);
        // the default config has admin active
        // ...we remove them if setup_admin is false
        if (! setup_admin)
        {
            (*p)["port_rpc"].set("admin","");
            (*p)["port_ws"].set("admin","");
        }
        return p;
    }

    // test helper
    static bool checkArraySize(Json::Value const& val, unsigned int size)
    {
        return val.isArray() &&
               val.size() == size;
    }

    // test helper
    static bool checkMarker(Json::Value const& val)
    {
        return val.isMember(jss::marker) &&
               val[jss::marker].isString() &&
               val[jss::marker].asString().size() > 0;
    }

    void testCurrentLedgerToLimits(bool as_admin)
    {
        using namespace test::jtx;
        Env env {*this, makeConfig(as_admin)};
        Account const gw {"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const max_limit = 256; //would be 2048 for binary requests, no need to test that here

        for (auto i = 0; i < max_limit + 10; i++)
        {
            Account const bob {std::string("bob") + std::to_string(i)};
            env.fund(XRP(1000), bob);
        }
        env.close();

        // no limit specified
        // with no limit specified, we get the max_limit if the total number of
        // accounts is greater than max, which it is here
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary]       = false;
        auto const jrr = env.rpc ( "json", "ledger_data",
            boost::lexical_cast<std::string>(jvParams)) [jss::result];
        BEAST_EXPECT(
            jrr[jss::ledger_current_index].isIntegral() &&
            jrr[jss::ledger_current_index].asInt() > 0 );
        BEAST_EXPECT( checkMarker(jrr) );
        BEAST_EXPECT( checkArraySize(jrr[jss::state], max_limit) );

        // check limits values around the max_limit (+/- 1)
        for (auto delta = -1; delta <= 1; delta++)
        {
            jvParams[jss::limit] = max_limit + delta;
            auto const jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(
                checkArraySize( jrr[jss::state],
                    (delta > 0 && !as_admin) ? max_limit : max_limit + delta ));
        }
    }

    void testCurrentLedgerBinary()
    {
        using namespace test::jtx;
        Env env { *this, makeConfig(false) };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const num_accounts = 10;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const bob { std::string("bob") + std::to_string(i) };
            env.fund(XRP(1000), bob);
        }
        env.close();

        // no limit specified
        // with no limit specified, we should get all of our fund entries
        // plus three more related to the gateway setup
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary]       = true;
        auto const jrr = env.rpc ( "json", "ledger_data",
            boost::lexical_cast<std::string>(jvParams)) [jss::result];
        BEAST_EXPECT(
            jrr[jss::ledger_current_index].isIntegral() &&
            jrr[jss::ledger_current_index].asInt() > 0);
        BEAST_EXPECT( ! jrr.isMember(jss::marker) );
        BEAST_EXPECT( checkArraySize(jrr[jss::state], num_accounts + 3) );
    }

    void testBadInput()
    {
        using namespace test::jtx;
        Env env { *this };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        Account const bob { "bob" };

        env.fund(XRP(10000), gw, bob);
        env.trust(USD(1000), bob);

        {
            // bad limit
            Json::Value jvParams;
            jvParams[jss::limit] = "0"; // NOT an integer
            auto const jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'limit', not integer.");
        }

        {
            // invalid marker
            Json::Value jvParams;
            jvParams[jss::marker] = "NOT_A_MARKER";
            auto const jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'marker', not valid.");
        }

        {
            // invalid marker - not a string
            Json::Value jvParams;
            jvParams[jss::marker] = 1;
            auto const jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'marker', not valid.");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }
    }

    void testMarkerFollow()
    {
        using namespace test::jtx;
        Env env { *this, makeConfig(false) };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const num_accounts = 20;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const bob { std::string("bob") + std::to_string(i) };
            env.fund(XRP(1000), bob);
        }
        env.close();

        // no limit specified
        // with no limit specified, we should get all of our fund entries
        // plus three more related to the gateway setup
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary]       = false;
        auto jrr = env.rpc ( "json", "ledger_data",
            boost::lexical_cast<std::string>(jvParams)) [jss::result];
        auto const total_count = jrr[jss::state].size();

        // now make request with a limit and loop until we get all
        jvParams[jss::limit]        = 5;
        jrr = env.rpc ( "json", "ledger_data",
            boost::lexical_cast<std::string>(jvParams)) [jss::result];
        BEAST_EXPECT( checkMarker(jrr) );
        auto running_total = jrr[jss::state].size();
        while ( jrr.isMember(jss::marker) )
        {
            jvParams[jss::marker] = jrr[jss::marker];
            jrr = env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            running_total += jrr[jss::state].size();
        }
        BEAST_EXPECT( running_total == total_count );
    }

    void testLedgerHeader()
    {
        using namespace test::jtx;
        Env env { *this };
        env.fund(XRP(100000), "alice");
        env.close();

        // Ledger header should be present in the first query
        {
            // Closed ledger with non binary form
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            auto jrr = env.rpc("json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            if (BEAST_EXPECT(jrr.isMember(jss::ledger)))
                BEAST_EXPECT(jrr[jss::ledger][jss::ledger_hash] ==
                    to_string(env.closed()->info().hash));
        }
        {
            // Closed ledger with binary form
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            jvParams[jss::binary] = true;
            auto jrr = env.rpc("json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            if (BEAST_EXPECT(jrr.isMember(jss::ledger)))
            {
                auto data = strUnHex(
                    jrr[jss::ledger][jss::ledger_data].asString());
                if (BEAST_EXPECT(data.second))
                {
                    Serializer s(data.first.data(), data.first.size());
                    std::uint32_t seq = 0;
                    BEAST_EXPECT(s.getInteger<std::uint32_t>(seq, 0));
                    BEAST_EXPECT(seq == 3);
                }
            }
        }
        {
            // Current ledger with binary form
            Json::Value jvParams;
            jvParams[jss::binary] = true;
            auto jrr = env.rpc("json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(! jrr[jss::ledger].isMember(jss::ledger_data));
        }
    }

    void run()
    {
        testCurrentLedgerToLimits(true);
        testCurrentLedgerToLimits(false);
        testCurrentLedgerBinary();
        testBadInput();
        testMarkerFollow();
        testLedgerHeader();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerData,app,ripple);

}
