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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>

namespace ripple {

namespace RPC {

class LedgerRequestRPC_test : public beast::unit_test::suite
{
public:
    void
    testLedgerRequest()
    {
        using namespace test::jtx;

        Env env(*this);

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const result = env.rpc("ledger_request", "arbitrary_text");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "-1");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "0");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "1");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 1 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "2");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 2 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "3");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 3 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());

            auto const ledgerHash =
                result[jss::result][jss::ledger][jss::ledger_hash].asString();

            {
                auto const r = env.rpc("ledger_request", ledgerHash);
                BEAST_EXPECT(
                    !RPC::contains_error(r[jss::result]) &&
                    r[jss::result][jss::ledger_index] == 3 &&
                    r[jss::result].isMember(jss::ledger));
                BEAST_EXPECT(
                    r[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                    r[jss::result][jss::ledger][jss::ledger_hash] ==
                        ledgerHash);
            }
        }

        {
            std::string ledgerHash(64, 'q');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Invalid field 'ledger_hash'.");
        }

        {
            std::string ledgerHash(64, '1');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::have_header] == false);
        }

        {
            auto const result = env.rpc("ledger_request", "4");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }

        {
            auto const result = env.rpc("ledger_request", "5");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }
    }

    void
    testEvolution()
    {
        using namespace test::jtx;
        Env env{*this, FeatureBitset{}};  // the hashes being checked below
                                          // assume no amendments
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);
        env.close();

        env.memoize("bob");
        env.fund(XRP(1000), "bob");
        env.close();

        env.memoize("alice");
        env.fund(XRP(1000), "alice");
        env.close();

        env.memoize("carol");
        env.fund(XRP(1000), "carol");
        env.close();

        auto result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "1");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "E9BB323980D202EC7E51BAB2AA8E35353F9C7BDAB59BF17378EADD4D0486EF9F");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "0000000000000000000000000000000000000000000000000000000000000000");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "A21ED30C04C88046FC61DB9DC19375EEDBD365FD8C17286F27127DF804E9CAA6");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "0000000000000000000000000000000000000000000000000000000000000000");

        result = env.rpc("ledger_request", "2")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "2");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "A15F7FBE0B06286915D971BF9802C9431CD7DE40E2AC7D07C409EDB1C0715C60");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "E9BB323980D202EC7E51BAB2AA8E35353F9C7BDAB59BF17378EADD4D0486EF9F");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "CB07F3CA0398BE969A5B88F874629D4DBB6E103DE7C6DB8037281A89E51AA8C6");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "0000000000000000000000000000000000000000000000000000000000000000");

        result = env.rpc("ledger_request", "3")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "3");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999980");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "9BCA8AE5FD41D223D82E1B8288961D693EB1B2EFA10F51827A641AD4B12111D7");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "A15F7FBE0B06286915D971BF9802C9431CD7DE40E2AC7D07C409EDB1C0715C60");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "5B793533909906D15CE27D1A423732D113160AB166188D89A2DFD8737CBDCBD5");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "0213EC486C058B3942FBE3DAC6839949A5C5B02B8B4244C8998EFDF04DBD8222");

        result = env.rpc("ledger_request", "4")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "4");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999960");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "433D1E42F2735F926BF594E4F3DFC70AE3E74F51464156ED83A33D0FF121D136");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "9BCA8AE5FD41D223D82E1B8288961D693EB1B2EFA10F51827A641AD4B12111D7");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "39C91E2227ACECD057AFDC64AE8FEFF5A0E07CF26ED29D1AECC55B0385F3EFDE");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "3CBDB8F42E04333E1642166BFB93AC9A7E1C6C067092CD5D881D6F3AB3D67E76");

        result = env.rpc("ledger_request", "5")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "5");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999940");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "9ED4D0C397810980904AF3FC08583D23B09C3C7CCF835D2A4768145A8BAC1175");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "433D1E42F2735F926BF594E4F3DFC70AE3E74F51464156ED83A33D0FF121D136");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "8F047B6A0D2083DF4F69C17F7CC9AE997B0D59020A43D9799A31D22F55837147");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "C3D086CD6BDB9E97AD1D513B2C049EF2840BD21D0B3E22D84EBBB89B6D2EF59D");

        result = env.rpc("ledger_request", "6")[jss::result];
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(result[jss::error_message] == "Ledger index too large");
    }

    void
    testBadInput()
    {
        using namespace test::jtx;
        Env env{*this};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_hash] =
            "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7";
        jvParams[jss::ledger_index] = "1";
        auto result = env.rpc(
            "json", "ledger_request", jvParams.toStyledString())[jss::result];
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(
            result[jss::error_message] ==
            "Exactly one of ledger_hash and ledger_index can be set.");

        // the purpose in this test is to force the ledger expiration/out of
        // date check to trigger
        env.timeKeeper().adjustCloseTime(weeks{3});
        result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::status] == "error");
        if (RPC::ApiMaximumSupportedVersion == 1)
        {
            BEAST_EXPECT(result[jss::error] == "noCurrent");
            BEAST_EXPECT(
                result[jss::error_message] == "Current ledger is unavailable.");
        }
        else
        {
            BEAST_EXPECT(result[jss::error] == "notSynced");
            BEAST_EXPECT(
                result[jss::error_message] == "Not synced to the network.");
        }
    }

    void
    testMoreThan256Closed()
    {
        using namespace test::jtx;
        using namespace std::chrono_literals;
        Env env{*this};
        Account const gw{"gateway"};
        env.app().getLedgerMaster().tune(0, 1h);
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const max_limit = 256;

        for (auto i = 0; i < max_limit + 10; i++)
        {
            Account const bob{std::string("bob") + std::to_string(i)};
            env.fund(XRP(1000), bob);
            env.close();
        }

        auto result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "1");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(
            result[jss::ledger][jss::ledger_hash] ==
            "E9BB323980D202EC7E51BAB2AA8E35353F9C7BDAB59BF17378EADD4D0486EF9F");
        BEAST_EXPECT(
            result[jss::ledger][jss::parent_hash] ==
            "0000000000000000000000000000000000000000000000000000000000000000");
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "A21ED30C04C88046FC61DB9DC19375EEDBD365FD8C17286F27127DF804E9CAA6");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "0000000000000000000000000000000000000000000000000000000000000000");
    }

    void
    testNonAdmin()
    {
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        auto const result = env.rpc("ledger_request", "1")[jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error.  The JSONRPCClient just eats
        // that error and returns an null result.
        BEAST_EXPECT(result.type() == Json::nullValue);
    }

    void
    run() override
    {
        testLedgerRequest();
        testEvolution();
        testBadInput();
        testMoreThan256Closed();
        testNonAdmin();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRequestRPC, app, ripple);

}  // namespace RPC
}  // namespace ripple
