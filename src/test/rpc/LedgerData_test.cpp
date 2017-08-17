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
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <test/jtx.h>

namespace ripple {

class LedgerData_test : public beast::unit_test::suite
{
public:
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

    void testCurrentLedgerToLimits(bool asAdmin)
    {
        using namespace test::jtx;
        Env env {*this, asAdmin ? envconfig() : envconfig(no_admin)};
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
                    (delta > 0 && !asAdmin) ? max_limit : max_limit + delta ));
        }
    }

    void testCurrentLedgerBinary()
    {
        using namespace test::jtx;
        Env env { *this, envconfig(no_admin) };
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
        Env env { *this, envconfig(no_admin) };
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

    void testLedgerType()
    {
        // Put a bunch of different LedgerEntryTypes into a ledger
        using namespace test::jtx;
        using namespace std::chrono;
        Env env { *this, envconfig(validator, ""),
            supported_features_plus(featureTickets) };

        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const num_accounts = 10;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const bob { std::string("bob") + std::to_string(i) };
            env.fund(XRP(1000), bob);
        }
        env(offer(Account{"bob0"}, USD(100), XRP(100)));
        env.trust(Account{"bob2"}["USD"](100), Account{"bob3"});

        auto majorities = getMajorityAmendments(*env.closed());
        for (int i = 0; i <= 256; ++i)
        {
            env.close();
            majorities = getMajorityAmendments(*env.closed());
            if (!majorities.empty())
                break;
        }
        env(signers(Account{"bob0"}, 1,
                {{Account{"bob1"}, 1}, {Account{"bob2"}, 1}}));
        env(ticket::create(env.master));

        {
            Json::Value jv;
            jv[jss::TransactionType] = "EscrowCreate";
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = Account{"bob5"}.human();
            jv[jss::Destination] = Account{"bob6"}.human();
            jv[jss::Amount] = XRP(50).value().getJson(0);
            jv[sfFinishAfter.fieldName] =
                NetClock::time_point{env.now() + 10s}
                    .time_since_epoch().count();
            env(jv);
        }

        {
            Json::Value jv;
            jv[jss::TransactionType] = "PaymentChannelCreate";
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = Account{"bob6"}.human ();
            jv[jss::Destination] = Account{"bob7"}.human ();
            jv[jss::Amount] = XRP(100).value().getJson (0);
            jv[jss::SettleDelay] = NetClock::duration{10s}.count();
            jv[sfPublicKey.fieldName] = strHex (Account{"bob6"}.pk().slice ());
            jv[sfCancelAfter.fieldName] =
                NetClock::time_point{env.now() + 300s}
                    .time_since_epoch().count();
            env(jv);
        }

        env.close();


        // Now fetch each type
        auto makeRequest = [&env](Json::StaticString t)
        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "current";
            jvParams[jss::type] = t;
            return env.rpc ( "json", "ledger_data",
                boost::lexical_cast<std::string>(jvParams)) [jss::result];
        };

        {  // jvParams[jss::type] = "account";
        auto const jrr = makeRequest(jss::account);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 12) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "AccountRoot" );
        }

        {  // jvParams[jss::type] = "amendments";
        auto const jrr = makeRequest(jss::amendments);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "Amendments" );
        }

        {  // jvParams[jss::type] = "directory";
        auto const jrr = makeRequest(jss::directory);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 7) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "DirectoryNode" );
        }

        {  // jvParams[jss::type] = "fee";
        auto const jrr = makeRequest(jss::fee);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "FeeSettings" );
        }

        {  // jvParams[jss::type] = "hashes";
        auto const jrr = makeRequest(jss::hashes);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 2) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "LedgerHashes" );
        }

        {  // jvParams[jss::type] = "offer";
        auto const jrr = makeRequest(jss::offer);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "Offer" );
        }

        {  // jvParams[jss::type] = "signer_list";
        auto const jrr = makeRequest(jss::signer_list);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "SignerList" );
        }

        {  // jvParams[jss::type] = "state";
        auto const jrr = makeRequest(jss::state);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "RippleState" );
        }

        {  // jvParams[jss::type] = "ticket";
        auto const jrr = makeRequest(jss::ticket);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "Ticket" );
        }

        {  // jvParams[jss::type] = "escrow";
        auto const jrr = makeRequest(jss::escrow);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "Escrow" );
        }

        {  // jvParams[jss::type] = "payment_channel";
        auto const jrr = makeRequest(jss::payment_channel);
        BEAST_EXPECT( checkArraySize(jrr[jss::state], 1) );
        for (auto const& j : jrr[jss::state])
            BEAST_EXPECT( j["LedgerEntryType"] == "PayChannel" );
        }

        {  // jvParams[jss::type] = "misspelling";
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::type] = "misspelling";
        auto const jrr = env.rpc ( "json", "ledger_data",
            boost::lexical_cast<std::string>(jvParams)) [jss::result];
        BEAST_EXPECT( jrr.isMember("error") );
        BEAST_EXPECT( jrr["error"] == "invalidParams" );
        BEAST_EXPECT( jrr["error_message"] == "Invalid field 'type'." );
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
        testLedgerType();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerData,app,ripple);

}
