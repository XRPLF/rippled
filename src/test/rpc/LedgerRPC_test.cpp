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

#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
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
    }

    // Corrupt a valid address by replacing the 10th character with '!'.
    // '!' is not part of the ripple alphabet.
    std::string
    makeBadAddress (std::string good)
    {
        std::string ret = std::move (good);
        ret.replace (10, 1, 1, '!');
        return ret;
    }

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

        {
            // Request queue for closed ledger
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::queue] = true;
            auto const jrr = env.rpc ( "json", "ledger", to_string(jvParams) ) [jss::result];
            checkErrorValue(jrr, "invalidParams", "Invalid parameters.");
        }

        {
            // Request a ledger with a very large (double) sequence.
            auto const ret = env.rpc (
                "json", "ledger", "{ \"ledger_index\" : 2e15 }");
            BEAST_EXPECT (RPC::contains_error(ret));
            BEAST_EXPECT (ret[jss::error_message] == "Invalid parameters.");
        }

        {
            // Request a ledger with very large (integer) sequence.
            auto const ret = env.rpc (
                "json", "ledger", "{ \"ledger_index\" : 1000000000000000 }");
            checkErrorValue(ret, "invalidParams", "Invalid parameters.");
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

    void testMissingLedgerEntryLedgerHash()
    {
        testcase("Missing ledger_entry ledger_hash");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        Json::Value jvParams;
        jvParams[jss::account_root] = alice.human();
        jvParams[jss::ledger_hash] =
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        auto const jrr =
            env.rpc ("json", "ledger_entry", to_string(jvParams))[jss::result];
        checkErrorValue (jrr, "lgrNotFound", "ledgerNotFound");
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

        Env env { *this, envconfig(no_admin) };

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

    void testLedgerEntryAccountRoot()
    {
        testcase ("ledger_entry Request AccountRoot");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund (XRP(10000), alice);
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc ("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 3);
        }

        std::string accountRootIndex;
        {
            // Request alice's account root.
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            constexpr char alicesAcctRootBinary[] {
                "1100612200800000240000000425000000032D00000000559CE54C3B934E4"
                "73A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F6240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D"
            };

            // Request alice's account root, but with binary == true;
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::binary] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] == alicesAcctRootBinary);
        }
        {
            // Request alice's account root using the index.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(! jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request alice's account root by index, but with binary == false.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            jvParams[jss::binary] = 0;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request using a corrupted AccountID.
            Json::Value jvParams;
            jvParams[jss::account_root] = makeBadAddress (alice.human());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAddress", "");
        }
        {
            // Request an account that is not in the ledger.
            Json::Value jvParams;
            jvParams[jss::account_root] = Account("bob").human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "entryNotFound", "");
        }
    }

    void testLedgerEntryCheck()
    {
        testcase ("ledger_entry Request Check");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund (XRP(10000), alice);
        env.close();

        uint256 const checkId {
            getCheckIndex (env.master, env.seq (env.master))};

        // Lambda to create a check.
        auto checkCreate = [] (test::jtx::Account const& account,
            test::jtx::Account const& dest, STAmount const& sendMax)
        {
            Json::Value jv;
            jv[sfAccount.jsonName] = account.human();
            jv[sfSendMax.jsonName] = sendMax.getJson(JsonOptions::none);
            jv[sfDestination.jsonName] = dest.human();
            jv[sfTransactionType.jsonName] = jss::CheckCreate;
            jv[sfFlags.jsonName] = tfUniversal;
            return jv;
        };

        env (checkCreate (env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value jvParams;
            jvParams[jss::check] = to_string (checkId);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
        {
            // Request an index that is not a check.  We'll use alice's
            // account root index.
            std::string accountRootIndex;
            {
                Json::Value jvParams;
                jvParams[jss::account_root] = alice.human();
                Json::Value const jrr = env.rpc (
                    "json", "ledger_entry", to_string (jvParams))[jss::result];
                accountRootIndex = jrr[jss::index].asString();
            }
            Json::Value jvParams;
            jvParams[jss::check] = accountRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
    }

    void testLedgerEntryDepositPreauth()
    {
        testcase ("ledger_entry Request Directory");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        Account const becky {"becky"};

        env.fund (XRP(10000), alice, becky);
        env.close();

        env (deposit::auth (alice, becky));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        std::string depositPreauthIndex;
        {
            // Request a depositPreauth by owner and authorized.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
            depositPreauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request a depositPreauth by index.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = depositPreauthIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];

            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
        }
        {
            // Malformed request: deposit_preauth neither object nor string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = -5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed request: deposit_preauth not hex string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = "0123456789ABCDEFG";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed request: missing [jss::deposit_preauth][jss::owner]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed request: [jss::deposit_preauth][jss::owner] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = 7;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed: missing [jss::deposit_preauth][jss::authorized]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = 47;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::owner] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedOwner", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAuthorized", "");
        }
    }

    void testLedgerEntryDirectory()
    {
        testcase ("ledger_entry Request Directory");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund (XRP(10000), alice, gw);
        env.close();

        env.trust(USD(1000), alice);
        env.close();

        // Run up the number of directory entries so alice has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env (offer (alice, USD (1), drops (d)));
        }
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc ("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 5);
        }

        std::string const dirRootIndex =
            "A33EC6BB85FB5674074C4A3A43373BB17645308F3EAE1933E3E35252162B217D";
        {
            // Locate directory by index.
            Json::Value jvParams;
            jvParams[jss::directory] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 32);
        }
        {
            // Locate directory by directory root.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by directory root and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Locate directory by owner and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Null directory argument.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::nullValue;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Non-integer sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1.5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;

            std::string const badAddress = makeBadAddress (alice.human());
            jvParams[jss::directory][jss::owner] = badAddress;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAddress", "");
        }
        {
            // Malformed directory object.  Specify both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Incomplete directory object.  Missing both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
    }

    void testLedgerEntryEscrow()
    {
        testcase ("ledger_entry Request Escrow");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund (XRP(10000), alice);
        env.close();

        // Lambda to create an escrow.
        auto escrowCreate = [] (
            test::jtx::Account const& account, test::jtx::Account const& to,
            STAmount const& amount, NetClock::time_point const& cancelAfter)
        {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfFinishAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        env (escrowCreate (alice, alice, XRP(333), env.now() + 2s));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        std::string escrowIndex;
        {
            // Request the escrow using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] = env.seq (alice) - 1;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
            escrowIndex = jrr[jss::index].asString();
        }
        {
            // Request the escrow by index.
            Json::Value jvParams;
            jvParams[jss::escrow] = escrowIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());

        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;

            std::string const badAddress = makeBadAddress (alice.human());
            jvParams[jss::escrow][jss::owner] = badAddress;
            jvParams[jss::escrow][jss::seq] = env.seq (alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedOwner", "");
        }
        {
            // Missing owner.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::seq] = env.seq (alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Missing sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Non-integer sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] =
                std::to_string (env.seq (alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
    }

    void testLedgerEntryOffer()
    {
        testcase ("ledger_entry Request Offer");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund (XRP(10000), alice, gw);
        env.close();

        env (offer (alice, USD (321), XRP (322)));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        std::string offerIndex;
        {
            // Request the offer using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = env.seq (alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
            offerIndex = jrr[jss::index].asString();
        }
        {
            // Request the offer using its index.
            Json::Value jvParams;
            jvParams[jss::offer] = offerIndex;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
        }
        {
            // Malformed account entry.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;

            std::string const badAddress = makeBadAddress (alice.human());
            jvParams[jss::offer][jss::account] = badAddress;
            jvParams[jss::offer][jss::seq] = env.seq (alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAddress", "");
        }
        {
            // Malformed offer object.  Missing account member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::seq] = env.seq (alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Missing seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Non-integral seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] =
                std::to_string (env.seq (alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
    }

    void testLedgerEntryPayChan()
    {
        testcase ("ledger_entry Request Pay Chan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env {*this};
        Account const alice {"alice"};

        env.fund (XRP(10000), alice);
        env.close();

        // Lambda to create a PayChan.
        auto payChanCreate = [] (
            test::jtx::Account const& account,
            test::jtx::Account const& to,
            STAmount const& amount,
            NetClock::duration const& settleDelay,
            PublicKey const& pk)
        {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson (JsonOptions::none);
            jv[sfSettleDelay.jsonName] = settleDelay.count();
            jv[sfPublicKey.jsonName] = strHex (pk.slice());
            return jv;
        };

        env (payChanCreate (alice, env.master, XRP(57), 18s, alice.pk()));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};

        uint256 const payChanIndex {
            keylet::payChan (alice, env.master, env.seq (alice) - 1).key};
        {
            // Request the payment channel using its index.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = to_string (payChanIndex);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "57000000");
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "0");
            BEAST_EXPECT(jrr[jss::node][sfSettleDelay.jsonName] == 18);
        }
        {
            // Request an index that is not a payment channel.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = ledgerHash;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "entryNotFound", "");
        }
    }

    void testLedgerEntryRippleState()
    {
        testcase ("ledger_entry Request RippleState");
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(999), alice);
        env.close();

        env(pay (gw, alice, USD(97)));
        env.close();

        std::string const ledgerHash {to_string (env.closed()->info().hash)};
        {
            // Request the trust line using the accounts and currency.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfBalance.jsonName][jss::value] == "-97");
            BEAST_EXPECT(
                jrr[jss::node][sfHighLimit.jsonName][jss::value] == "999");
        }
        {
            // ripple_state is not an object.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = "ripple_state";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state.currency is missing.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state accounts is not an array.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = 2;
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state one of the accounts is missing.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state more than 2 accounts.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::accounts][2u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[0] is not a string.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = 44;
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[1] is not a string.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = 21;
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[0] == account[1].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedRequest", "");
        }
        {
            // ripple_state malformed account[0].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] =
                makeBadAddress (alice.human());
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAddress", "");
        }
        {
            // ripple_state malformed account[1].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] =
                makeBadAddress (gw.human());
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedAddress", "");
        }
        {
            // ripple_state malformed currency.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USDollars";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc (
                "json", "ledger_entry", to_string (jvParams))[jss::result];
            checkErrorValue (jrr, "malformedCurrency", "");
        }
    }

    void testLedgerEntryUnknownOption()
    {
        testcase ("ledger_entry Request Unknown Option");
        using namespace test::jtx;
        Env env {*this};

        std::string const ledgerHash {to_string (env.closed()->info().hash)};

        // "features" is not an option supported by ledger_entry.
        Json::Value jvParams;
        jvParams[jss::features] = ledgerHash;
        jvParams[jss::ledger_hash] = ledgerHash;
        Json::Value const jrr = env.rpc (
            "json", "ledger_entry", to_string (jvParams))[jss::result];
        checkErrorValue (jrr, "unknownOption", "");
    }

    /// @brief ledger RPC requests as a way to drive
    /// input options to lookupLedger. The point of this test is
    /// coverage for lookupLedger, not so much the ledger
    /// RPC request.
    void testLookupLedger()
    {
        testcase ("Lookup ledger");
        using namespace test::jtx;
        Env env {*this, FeatureBitset{}}; // hashes requested below assume
                                     //no amendments
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

    void testNoQueue()
    {
        testcase("Ledger with queueing disabled");
        using namespace test::jtx;
        Env env{ *this };

        Json::Value jv;
        jv[jss::ledger_index] = "current";
        jv[jss::queue] = true;
        jv[jss::expand] = true;

        auto jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(! jrr.isMember(jss::queue_data));
    }

    void testQueue()
    {
        testcase("Ledger with Queued Transactions");
        using namespace test::jtx;
        Env env { *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                auto& section = cfg->section("transaction_queue");
                section.set("minimum_txn_in_ledger_standalone", "3");
                section.set("normal_consensus_increase_percent", "0");
                return cfg;
            })};

        Json::Value jv;
        jv[jss::ledger_index] = "current";
        jv[jss::queue] = true;
        jv[jss::expand] = true;

        Account const alice{ "alice" };
        Account const bob{ "bob" };
        Account const charlie{ "charlie" };
        Account const daria{ "daria" };
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);
        env.close();
        env.fund(XRP(10000), charlie);
        env.fund(XRP(10000), daria);
        env.close();

        auto jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(! jrr.isMember(jss::queue_data));

        // Fill the open ledger
        for (;;)
        {
            auto metrics = env.app().getTxQ().getMetrics(*env.current());
            if (metrics.openLedgerFeeLevel > metrics.minProcessingFeeLevel)
                break;
            env(noop(alice));
        }

        BEAST_EXPECT(env.current()->info().seq == 5);
        // Put some txs in the queue
        // Alice
        auto aliceSeq = env.seq(alice);
        env(pay(alice, "george", XRP(1000)), json(R"({"LastLedgerSequence":7})"),
            ter(terQUEUED));
        env(offer(alice, XRP(50000), alice["USD"](5000)), seq(aliceSeq + 1),
            ter(terQUEUED));
        env(noop(alice), seq(aliceSeq + 2), ter(terQUEUED));
        // Bob
        auto batch = [&env](Account a)
        {
            auto aSeq = env.seq(a);
            // Enough fee to get in front of alice in the queue
            for (int i = 0; i < 10; ++i)
            {
                env(noop(a), fee(1000 + i), seq(aSeq + i), ter(terQUEUED));
            }
        };
        batch(bob);
        // Charlie
        batch(charlie);
        // Daria
        batch(daria);

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(jrr[jss::queue_data].size() == 33);

        // Close enough ledgers so that alice's first tx expires.
        env.close();
        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 8);

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(jrr[jss::queue_data].size() == 11);

        env.close();

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        std::string txid1;
        std::string txid2;
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 10);
            BEAST_EXPECT(txj.isMember(jss::tx));
            auto const& tx = txj[jss::tx];
            BEAST_EXPECT(tx[jss::Account] == alice.human());
            BEAST_EXPECT(tx[jss::TransactionType] == jss::OfferCreate);
            txid1 = tx[jss::hash].asString();
        }

        env.close();

        jv[jss::expand] = false;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 9);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            BEAST_EXPECT(txj[jss::tx] == txid1);
        }

        env.close();

        jv[jss::expand] = true;
        jv[jss::binary] = true;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 8);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            BEAST_EXPECT(txj[jss::tx].isMember(jss::tx_blob));

            auto const& txj2 = jrr[jss::queue_data][1u];
            BEAST_EXPECT(txj2[jss::account] == alice.human());
            BEAST_EXPECT(txj2[jss::fee_level] == "256");
            BEAST_EXPECT(txj2["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj2["retries_remaining"] == 10);
            BEAST_EXPECT(! txj2.isMember("last_result"));
            BEAST_EXPECT(txj2.isMember(jss::tx));
            BEAST_EXPECT(txj2[jss::tx].isMember(jss::tx_blob));
        }

        for (int i = 0; i != 9; ++i)
        {
            env.close();
        }

        jv[jss::expand] = false;
        jv[jss::binary] = false;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 1))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 1);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            BEAST_EXPECT(txj[jss::tx] != txid1);
            txid2 = txj[jss::tx].asString();
        }

        jv[jss::full] = true;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 1))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 1);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            auto const& tx = txj[jss::tx];
            BEAST_EXPECT(tx[jss::Account] == alice.human());
            BEAST_EXPECT(tx[jss::TransactionType] == jss::AccountSet);
            BEAST_EXPECT(tx[jss::hash] == txid2);
        }
    }

    void testLedgerAccountsOption()
    {
        testcase("Ledger Request, Accounts Option");
        using namespace test::jtx;

        Env env {*this};

        env.close();

        std::string index;
        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 3u;
            jvParams[jss::accounts] = true;
            jvParams[jss::expand] = true;
            jvParams[jss::type] = "hashes";
            auto const jrr = env.rpc (
                "json", "ledger", to_string(jvParams) )[jss::result];
            BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 1u);
            BEAST_EXPECT(
                jrr[jss::ledger][jss::accountState][0u]["LedgerEntryType"]
                == jss::LedgerHashes);
            index = jrr[jss::ledger][jss::accountState][0u]["index"].asString();
        }
        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 3u;
            jvParams[jss::accounts] = true;
            jvParams[jss::expand] = false;
            jvParams[jss::type] = "hashes";
            auto const jrr = env.rpc (
                "json", "ledger", to_string(jvParams) )[jss::result];
            BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 1u);
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState][0u] == index);
        }
    }

public:
    void run () override
    {
        testLedgerRequest();
        testBadInput();
        testLedgerCurrent();
        testMissingLedgerEntryLedgerHash();
        testLedgerFull();
        testLedgerFullNonAdmin();
        testLedgerAccounts();
        testLedgerEntryAccountRoot();
        testLedgerEntryCheck();
        testLedgerEntryDepositPreauth();
        testLedgerEntryDirectory();
        testLedgerEntryEscrow();
        testLedgerEntryOffer();
        testLedgerEntryPayChan();
        testLedgerEntryRippleState();
        testLedgerEntryUnknownOption();
        testLookupLedger();
        testNoQueue();
        testQueue();
        testLedgerAccountsOption();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC,app,ripple);

} // ripple

