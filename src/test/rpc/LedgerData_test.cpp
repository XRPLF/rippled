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

    /// @brief ledger_header RPC requests as a way to drive
    /// input options to lookupLedger. The point of this test is
    /// coverage for lookupLedger, not so much the ledger_header
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
            auto jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "validated";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "current";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");

            // ask for a bad ledger keyword
            jvParams[jss::ledger] = "invalid";
            jrr = env.rpc ( "json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            jvParams[jss::ledger] = 4;
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "4");

            // numeric index - out of range
            jvParams[jss::ledger] = 20;
            jrr = env.rpc("json", "ledger_header",
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
            auto jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // extra leading hex chars in hash will be ignored
            jvParams[jss::ledger_hash] =
                "DEADBEEF"
                "7C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // request with non-string ledger_hash
            jvParams[jss::ledger_hash] = 2;
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashNotString");

            // malformed (non hex) hash
            jvParams[jss::ledger_hash] =
                "ZZZZZZZZZZZD92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // properly formed, but just doesn't exist
            jvParams[jss::ledger_hash] =
                "8C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            //access via the ledger_index field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            auto jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr.isMember(jss::ledger_index));

            jvParams[jss::ledger_index] = "validated";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger_index] = "current";
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_data));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");
            BEAST_EXPECT(jrr.isMember(jss::ledger_current_index));

            // ask for a bad ledger keyword
            jvParams[jss::ledger_index] = "invalid";
            jrr = env.rpc ( "json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            for (auto i : {1, 2, 3, 4 ,5, 6})
            {
                jvParams[jss::ledger_index] = i;
                jrr = env.rpc("json", "ledger_header",
                    boost::lexical_cast<std::string>(jvParams))[jss::result];
                BEAST_EXPECT(jrr.isMember(jss::ledger));
                BEAST_EXPECT(jrr.isMember(jss::ledger_data));
                BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == std::to_string(i));
            }

            // numeric index - out of range
            jvParams[jss::ledger_index] = 7;
            jrr = env.rpc("json", "ledger_header",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
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
        testLookupLedger();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerData,app,ripple);

}
