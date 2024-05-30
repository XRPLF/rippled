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

#include <functional>

namespace ripple {

namespace RPC {

class LedgerRequestRPC_test : public beast::unit_test::suite
{
    static constexpr char const* hash1 =
        "3020EB9E7BE24EF7D7A060CB051583EC117384636D1781AFB5B87F3E348DA489";
    static constexpr char const* accounthash1 =
        "BD8A3D72CA73DDE887AD63666EC2BAD07875CBA997A102579B5B95ECDFFEAED8";

    static constexpr char const* zerohash =
        "0000000000000000000000000000000000000000000000000000000000000000";

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
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == zerohash);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == accounthash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);

        result = env.rpc("ledger_request", "2")[jss::result];
        constexpr char const* hash2 =
            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "2");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash2);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash1);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "3C834285F7F464FBE99AFEB84D354A968EB2CAA24523FF26797A973D906A3D29");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);

        result = env.rpc("ledger_request", "3")[jss::result];
        constexpr char const* hash3 =
            "8D631B20BC989AF568FBA97375290544B0703A5ADC1CF9E9053580461690C9EE";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "3");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999980");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash3);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash2);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "BC9EF2A16BFF80BCFABA6FA84688D858D33BD0FA0435CAA9DF6DA4105A39A29E");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "0213EC486C058B3942FBE3DAC6839949A5C5B02B8B4244C8998EFDF04DBD8222");

        result = env.rpc("ledger_request", "4")[jss::result];
        constexpr char const* hash4 =
            "1A8E7098B23597E73094DADA58C9D62F3AB93A12C6F7666D56CA85A6CFDE530F";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "4");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999960");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash4);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash3);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "C690188F123C91355ADA8BDF4AC5B5C927076D3590C215096868A5255264C6DD");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "3CBDB8F42E04333E1642166BFB93AC9A7E1C6C067092CD5D881D6F3AB3D67E76");

        result = env.rpc("ledger_request", "5")[jss::result];
        constexpr char const* hash5 =
            "C6A222D71AE65D7B4F240009EAD5DEB20D7EEDE5A4064F28BBDBFEEB6FBE48E5";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "5");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999940");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash5);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash4);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "EA81CD9D36740736F00CB747E0D0E32D3C10B695823D961F0FB9A1CE7133DD4D");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "C3D086CD6BDB9E97AD1D513B2C049EF2840BD21D0B3E22D84EBBB89B6D2EF59D");

        result = env.rpc("ledger_request", "6")[jss::result];
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(result[jss::error_message] == "Ledger index too large");
    }

    void
    testBadInput(unsigned apiVersion)
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
        result = env.rpc(apiVersion, "ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::status] == "error");
        if (apiVersion == 1)
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
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->NODE_SIZE = 0;
                    return cfg;
                })};
        Account const gw{"gateway"};
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
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == zerohash);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == accounthash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);
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
        forAllApiVersions(
            std::bind_front(&LedgerRequestRPC_test::testBadInput, this));
        testMoreThan256Closed();
        testNonAdmin();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRequestRPC, app, ripple);

}  // namespace RPC
}  // namespace ripple
