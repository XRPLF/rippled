//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2025 Ripple Labs Inc.

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
#include <test/jtx/Oracle.h>
#include <test/jtx/attester.h>
#include <test/jtx/delegate.h>
#include <test/jtx/multisign.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace test {

class LedgerEntry_test : public beast::unit_test::suite
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
    makeBadAddress(std::string good)
    {
        std::string ret = std::move(good);
        ret.replace(10, 1, 1, '!');
        return ret;
    }

    void
    testLedgerEntryInvalid()
    {
        testcase("Invalid requests");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        {
            // Missing ledger_entry ledger_hash
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] =
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AA";
            auto const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }

        {
            // ask for an zero index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::index] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "0000";
            auto const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryAccountRoot()
    {
        testcase("ledger_entry Request AccountRoot");
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->FEES.reference_fee = 10;
        Env env{*this, std::move(cfg)};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 3);
        }

        std::string accountRootIndex;
        {
            // Request alice's account root.
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            constexpr char alicesAcctRootBinary[]{
                "1100612200800000240000000425000000032D00000000559CE54C3B934E4"
                "73A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F6240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D"};

            // Request alice's account root, but with binary == true;
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::binary] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] == alicesAcctRootBinary);
        }
        {
            // Request alice's account root using the index.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request alice's account root by index, but with binary == false.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            jvParams[jss::binary] = 0;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request using a corrupted AccountID.
            Json::Value jvParams;
            jvParams[jss::account_root] = makeBadAddress(alice.human());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Request an account that is not in the ledger.
            Json::Value jvParams;
            jvParams[jss::account_root] = Account("bob").human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryCheck()
    {
        testcase("ledger_entry Request Check");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const checkId = keylet::check(env.master, env.seq(env.master));

        env(check::create(env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value jvParams;
            jvParams[jss::check] = to_string(checkId.key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
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
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                accountRootIndex = jrr[jss::index].asString();
            }
            Json::Value jvParams;
            jvParams[jss::check] = accountRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "");
        }
    }

    void
    testLedgerEntryCredentials()
    {
        testcase("ledger_entry credentials");

        using namespace test::jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        char const credType[] = "abcde";

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        // Setup credentials with DepositAuth object for Alice and Bob
        env(credentials::create(alice, issuer, credType));
        env.close();

        {
            // Succeed
            auto jv = credentials::ledgerEntry(env, alice, issuer, credType);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::Credential);

            std::string const credIdx = jv[jss::result][jss::index].asString();

            jv = credentials::ledgerEntry(env, credIdx);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::Credential);
        }

        {
            // Fail, index not a hash
            auto const jv = credentials::ledgerEntry(env, "");
            checkErrorValue(jv[jss::result], "malformedRequest", "");
        }

        {
            // Fail, credential doesn't exist
            auto const jv = credentials::ledgerEntry(
                env,
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4");
            checkErrorValue(jv[jss::result], "entryNotFound", "");
        }

        {
            // Fail, invalid subject
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = 42;
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, invalid issuer
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = 42;
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, invalid credentials type
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] = 42;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, empty subject
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = "";
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, empty issuer
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = "";
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, empty credentials type
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] = "";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, no subject
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, no issuer
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, no credentials type
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = issuer.human();
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, not AccountID subject
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = "wehsdbvasbdfvj";
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, not AccountID issuer
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = "c4p93ugndfbsiu";
            jv[jss::credential][jss::credential_type] =
                strHex(std::string_view(credType));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, credentials type isn't hex encoded
            Json::Value jv;
            jv[jss::ledger_index] = jss::validated;
            jv[jss::credential][jss::subject] = alice.human();
            jv[jss::credential][jss::issuer] = issuer.human();
            jv[jss::credential][jss::credential_type] = "12KK";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jv));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }
    }

    void
    testLedgerEntryDelegate()
    {
        testcase("ledger_entry Delegate");

        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();
        env(delegate::set(alice, bob, {"Payment", "CheckCreate"}));
        env.close();
        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string delegateIndex;
        {
            // Request by account and authorize
            Json::Value jvParams;
            jvParams[jss::delegate][jss::account] = alice.human();
            jvParams[jss::delegate][jss::authorize] = bob.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == bob.human());
            delegateIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request by index.
            Json::Value jvParams;
            jvParams[jss::delegate] = delegateIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == bob.human());
        }
        {
            // Malformed request: delegate neither object nor string.
            Json::Value jvParams;
            jvParams[jss::delegate] = 5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: delegate not hex string.
            Json::Value jvParams;
            jvParams[jss::delegate] = "0123456789ABCDEFG";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: account not a string
            Json::Value jvParams;
            jvParams[jss::delegate][jss::account] = 5;
            jvParams[jss::delegate][jss::authorize] = bob.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed request: authorize not a string
            Json::Value jvParams;
            jvParams[jss::delegate][jss::account] = alice.human();
            jvParams[jss::delegate][jss::authorize] = 5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // this lambda function is used test malformed account and authroize
            auto testMalformedAccount =
                [&](std::optional<std::string> const& account,
                    std::optional<std::string> const& authorize,
                    std::string const& error) {
                    Json::Value jvParams;
                    jvParams[jss::ledger_hash] = ledgerHash;
                    if (account)
                        jvParams[jss::delegate][jss::account] = *account;
                    if (authorize)
                        jvParams[jss::delegate][jss::authorize] = *authorize;
                    auto const jrr = env.rpc(
                        "json",
                        "ledger_entry",
                        to_string(jvParams))[jss::result];
                    checkErrorValue(jrr, error, "");
                };
            // missing account
            testMalformedAccount(std::nullopt, bob.human(), "malformedRequest");
            // missing authorize
            testMalformedAccount(
                alice.human(), std::nullopt, "malformedRequest");
            // malformed account
            testMalformedAccount("-", bob.human(), "malformedAddress");
            // malformed authorize
            testMalformedAccount(alice.human(), "-", "malformedAddress");
        }
    }

    void
    testLedgerEntryDepositPreauth()
    {
        testcase("ledger_entry Deposit Preauth");

        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(10000), alice, becky);
        env.close();

        env(deposit::auth(alice, becky));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string depositPreauthIndex;
        {
            // Request a depositPreauth by owner and authorized.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
            depositPreauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request a depositPreauth by index.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = depositPreauthIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
        }
        {
            // Malformed request: deposit_preauth neither object nor string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = -5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: deposit_preauth not hex string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = "0123456789ABCDEFG";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: missing [jss::deposit_preauth][jss::owner]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: [jss::deposit_preauth][jss::owner] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = 7;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: missing [jss::deposit_preauth][jss::authorized]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = 47;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::owner] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedOwner", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAuthorized", "");
        }
    }

    void
    testLedgerEntryDepositPreauthCred()
    {
        testcase("ledger_entry Deposit Preauth with credentials");

        using namespace test::jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        char const credType[] = "abcde";

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        {
            // Setup Bob with DepositAuth
            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::authCredentials(bob, {{issuer, credType}}));
            env.close();
        }

        {
            // Succeed
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));
            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(
                jrr.isObject() && jrr.isMember(jss::result) &&
                !jrr[jss::result].isMember(jss::error) &&
                jrr[jss::result].isMember(jss::node) &&
                jrr[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jrr[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::DepositPreauth);
        }

        {
            // Failed, invalid account
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = to_string(xrpAccount());
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));
            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, duplicates in credentials
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(jo);
            arr.append(std::move(jo));
            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, invalid credential_type
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = "";
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, authorized and authorized_credentials both present
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized] = alice.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Failed, authorized_credentials is not an array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] = 42;

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Failed, authorized_credentials contains string data
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            arr.append("foobar");

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, authorized_credentials contains arrays
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            Json::Value payload = Json::arrayValue;
            payload.append(42);
            arr.append(std::move(payload));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, authorized_credentials is empty array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, authorized_credentials is too long

            static std::string_view const credTypes[] = {
                "cred1",
                "cred2",
                "cred3",
                "cred4",
                "cred5",
                "cred6",
                "cred7",
                "cred8",
                "cred9"};
            static_assert(
                sizeof(credTypes) / sizeof(credTypes[0]) >
                maxCredentialsArraySize);

            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;

            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            for (unsigned i = 0; i < sizeof(credTypes) / sizeof(credTypes[0]);
                 ++i)
            {
                Json::Value jo;
                jo[jss::issuer] = issuer.human();
                jo[jss::credential_type] =
                    strHex(std::string_view(credTypes[i]));
                arr.append(std::move(jo));
            }

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, issuer is not set
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, issuer isn't string
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = 42;
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, issuer is an array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            Json::Value payload = Json::arrayValue;
            payload.append(42);
            jo[jss::issuer] = std::move(payload);
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, issuer isn't valid encoded account
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = "invalid_account";
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, credential_type is not set
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, credential_type isn't string
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = 42;
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, credential_type is an array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            Json::Value payload = Json::arrayValue;
            payload.append(42);
            jo[jss::credential_type] = std::move(payload);
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }

        {
            // Failed, credential_type isn't hex encoded
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = "12KK";
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result], "malformedAuthorizedCredentials", "");
        }
    }

    void
    testLedgerEntryDirectory()
    {
        testcase("ledger_entry Request Directory");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(1000), alice);
        env.close();

        // Run up the number of directory entries so alice has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(alice, USD(1), drops(d)));
        }
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
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
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 32);
        }
        {
            // Locate directory by directory root.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by directory root and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
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
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Null directory argument.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::nullValue;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Non-integer sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1.5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::directory][jss::owner] = badAddress;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed directory object.  Specify both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Incomplete directory object.  Missing both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryEscrow()
    {
        testcase("ledger_entry Request Escrow");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create an escrow.
        auto escrowCreate = [](test::jtx::Account const& account,
                               test::jtx::Account const& to,
                               STAmount const& amount,
                               NetClock::time_point const& cancelAfter) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfFinishAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        env(escrowCreate(alice, alice, XRP(333), env.now() + 2s));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string escrowIndex;
        {
            // Request the escrow using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
            escrowIndex = jrr[jss::index].asString();
        }
        {
            // Request the escrow by index.
            Json::Value jvParams;
            jvParams[jss::escrow] = escrowIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::escrow][jss::owner] = badAddress;
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedOwner", "");
        }
        {
            // Missing owner.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Missing sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Non-integer sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] =
                std::to_string(env.seq(alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryOffer()
    {
        testcase("ledger_entry Request Offer");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env(offer(alice, USD(321), XRP(322)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string offerIndex;
        {
            // Request the offer using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
            offerIndex = jrr[jss::index].asString();
        }
        {
            // Request the offer using its index.
            Json::Value jvParams;
            jvParams[jss::offer] = offerIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
        }
        {
            // Malformed account entry.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::offer][jss::account] = badAddress;
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed offer object.  Missing account member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Missing seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Non-integral seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = std::to_string(env.seq(alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryPayChan()
    {
        testcase("ledger_entry Request Pay Chan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a PayChan.
        auto payChanCreate = [](test::jtx::Account const& account,
                                test::jtx::Account const& to,
                                STAmount const& amount,
                                NetClock::duration const& settleDelay,
                                PublicKey const& pk) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfSettleDelay.jsonName] = settleDelay.count();
            jv[sfPublicKey.jsonName] = strHex(pk.slice());
            return jv;
        };

        env(payChanCreate(alice, env.master, XRP(57), 18s, alice.pk()));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        uint256 const payChanIndex{
            keylet::payChan(alice, env.master, env.seq(alice) - 1).key};
        {
            // Request the payment channel using its index.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = to_string(payChanIndex);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "57000000");
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "0");
            BEAST_EXPECT(jrr[jss::node][sfSettleDelay.jsonName] == 18);
        }
        {
            // Request an index that is not a payment channel.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = ledgerHash;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryRippleState()
    {
        testcase("ledger_entry Request RippleState");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(999), alice);
        env.close();

        env(pay(gw, alice, USD(97)));
        env.close();

        // check both aliases
        for (auto const& fieldName : {jss::ripple_state, jss::state})
        {
            std::string const ledgerHash{to_string(env.closed()->info().hash)};
            {
                // Request the trust line using the accounts and currency.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                BEAST_EXPECT(
                    jrr[jss::node][sfBalance.jsonName][jss::value] == "-97");
                BEAST_EXPECT(
                    jrr[jss::node][sfHighLimit.jsonName][jss::value] == "999");
            }
            {
                // ripple_state is not an object.
                Json::Value jvParams;
                jvParams[fieldName] = "ripple_state";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state.currency is missing.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state accounts is not an array.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = 2;
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state one of the accounts is missing.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state more than 2 accounts.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::accounts][2u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state account[0] is not a string.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = 44;
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state account[1] is not a string.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = 21;
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state account[0] == account[1].
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "");
            }
            {
                // ripple_state malformed account[0].
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] =
                    makeBadAddress(alice.human());
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedAddress", "");
            }
            {
                // ripple_state malformed account[1].
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] =
                    makeBadAddress(gw.human());
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedAddress", "");
            }
            {
                // ripple_state malformed currency.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USDollars";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedCurrency", "");
            }
        }
    }

    void
    testLedgerEntryTicket()
    {
        testcase("ledger_entry Request Ticket");
        using namespace test::jtx;
        Env env{*this};
        env.close();

        // Create two tickets.
        std::uint32_t const tkt1{env.seq(env.master) + 1};
        env(ticket::create(env.master, 2));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        // Request four tickets: one before the first one we created, the
        // two created tickets, and the ticket that would come after the
        // last created ticket.
        {
            // Not a valid ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] =
                to_string(getTicketIndex(env.master, tkt1 - 1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // First real ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(getTicketIndex(env.master, tkt1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(jrr[jss::node][sfTicketSequence.jsonName] == tkt1);
        }
        {
            // Second real ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::index] ==
                to_string(getTicketIndex(env.master, tkt1 + 1)));
        }
        {
            // Not a valid ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 2;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // Request a ticket using an account root entry.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(keylet::account(env.master).key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "");
        }
        {
            // Malformed account entry.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;

            std::string const badAddress = makeBadAddress(env.master.human());
            jvParams[jss::ticket][jss::account] = badAddress;
            jvParams[jss::ticket][jss::ticket_seq] = env.seq(env.master) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed ticket object.  Missing account member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::ticket_seq] = env.seq(env.master) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed ticket object.  Missing seq member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed ticket object.  Non-integral seq member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] =
                std::to_string(env.seq(env.master) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryDID()
    {
        testcase("ledger_entry Request DID");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a DID.
        auto didCreate = [](test::jtx::Account const& account) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::DIDSet;
            jv[jss::Account] = account.human();
            jv[sfDIDDocument.jsonName] = strHex(std::string{"data"});
            jv[sfURI.jsonName] = strHex(std::string{"uri"});
            return jv;
        };

        env(didCreate(alice));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        {
            // Request the DID using its index.
            Json::Value jvParams;
            jvParams[jss::did] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfDIDDocument.jsonName] ==
                strHex(std::string{"data"}));
            BEAST_EXPECT(
                jrr[jss::node][sfURI.jsonName] == strHex(std::string{"uri"}));
        }
        {
            // Request an index that is not a DID.
            Json::Value jvParams;
            jvParams[jss::did] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryInvalidParams(unsigned int apiVersion)
    {
        testcase(
            "ledger_entry Request With Invalid Parameters v" +
            std::to_string(apiVersion));
        using namespace test::jtx;
        Env env{*this};

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        auto makeParams = [&apiVersion](std::function<void(Json::Value&)> f) {
            Json::Value params;
            params[jss::api_version] = apiVersion;
            f(params);
            return params;
        };
        // "features" is not an option supported by ledger_entry.
        {
            auto const jvParams =
                makeParams([&ledgerHash](Json::Value& jvParams) {
                    jvParams[jss::features] = ledgerHash;
                    jvParams[jss::ledger_hash] = ledgerHash;
                });
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            if (apiVersion < 2u)
                checkErrorValue(jrr, "unknownOption", "");
            else
                checkErrorValue(jrr, "invalidParams", "");
        }
        Json::Value const injectObject = []() {
            Json::Value obj(Json::objectValue);
            obj[jss::account] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            obj[jss::ledger_index] = "validated";
            return obj;
        }();
        Json::Value const injectArray = []() {
            Json::Value arr(Json::arrayValue);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "validated";
            return arr;
        }();

        // invalid input for fields that can handle an object, but can't handle
        // an array
        for (auto const& field :
             {jss::directory, jss::escrow, jss::offer, jss::ticket, jss::amm})
        {
            auto const jvParams =
                makeParams([&field, &injectArray](Json::Value& jvParams) {
                    jvParams[field] = injectArray;
                });

            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            if (apiVersion < 2u)
                checkErrorValue(jrr, "internal", "Internal error.");
            else
                checkErrorValue(jrr, "invalidParams", "");
        }
        // Fields that can handle objects just fine
        for (auto const& field :
             {jss::directory, jss::escrow, jss::offer, jss::ticket, jss::amm})
        {
            auto const jvParams =
                makeParams([&field, &injectObject](Json::Value& jvParams) {
                    jvParams[field] = injectObject;
                });

            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "malformedRequest", "");
        }

        for (auto const& inject : {injectObject, injectArray})
        {
            // invalid input for fields that can't handle an object or an array
            for (auto const& field :
                 {jss::index,
                  jss::account_root,
                  jss::check,
                  jss::payment_channel})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // directory sub-fields
            for (auto const& field : {jss::dir_root, jss::owner})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[jss::directory][field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // escrow sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::escrow][jss::owner] = inject;
                        jvParams[jss::escrow][jss::seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // offer sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::offer][jss::account] = inject;
                        jvParams[jss::offer][jss::seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // ripple_state sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        Json::Value rs(Json::objectValue);
                        rs[jss::currency] = "FOO";
                        rs[jss::accounts] = Json::Value(Json::arrayValue);
                        rs[jss::accounts][0u] =
                            "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
                        rs[jss::accounts][1u] =
                            "rKssEq6pg1KbqEqAFnua5mFAL6Ggpsh2wv";
                        rs[jss::currency] = inject;
                        jvParams[jss::ripple_state] = std::move(rs);
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // ticket sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::ticket][jss::account] = inject;
                        jvParams[jss::ticket][jss::ticket_seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }

            // Fields that can handle malformed inputs just fine
            for (auto const& field : {jss::nft_page, jss::deposit_preauth})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                checkErrorValue(jrr, "malformedRequest", "");
            }
            // Subfields of deposit_preauth that can handle malformed inputs
            // fine
            for (auto const& field : {jss::owner, jss::authorized})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        auto pa = Json::Value(Json::objectValue);
                        pa[jss::owner] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
                        pa[jss::authorized] =
                            "rKssEq6pg1KbqEqAFnua5mFAL6Ggpsh2wv";
                        pa[field] = inject;
                        jvParams[jss::deposit_preauth] = std::move(pa);
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                checkErrorValue(jrr, "malformedRequest", "");
            }
        }
    }

    void
    testInvalidOracleLedgerEntry()
    {
        testcase("Invalid Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(
            env,
            {.owner = owner,
             .fee = static_cast<int>(env.current()->fees().base.drops())});

        // Malformed document id
        auto res = Oracle::ledgerEntry(env, owner, NoneTag);
        BEAST_EXPECT(res[jss::error].asString() == "invalidParams");
        std::vector<AnyValue> invalid = {-1, 1.2, "", "Invalid"};
        for (auto const& v : invalid)
        {
            auto const res = Oracle::ledgerEntry(env, owner, v);
            BEAST_EXPECT(res[jss::error].asString() == "malformedDocumentID");
        }
        // Missing document id
        res = Oracle::ledgerEntry(env, owner, std::nullopt);
        BEAST_EXPECT(res[jss::error].asString() == "malformedRequest");

        // Missing account
        res = Oracle::ledgerEntry(env, std::nullopt, 1);
        BEAST_EXPECT(res[jss::error].asString() == "malformedRequest");

        // Malformed account
        std::string malfAccount = to_string(owner.id());
        malfAccount.replace(10, 1, 1, '!');
        res = Oracle::ledgerEntry(env, malfAccount, 1);
        BEAST_EXPECT(res[jss::error].asString() == "malformedAddress");
    }

    void
    testOracleLedgerEntry()
    {
        testcase("Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());
        std::vector<AccountID> accounts;
        std::vector<std::uint32_t> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle oracle(
                env, {.owner = owner, .documentID = i, .fee = baseFee});
            accounts.push_back(owner.id());
            oracles.push_back(oracle.documentID());
            // same account can have different asset pair
            Oracle oracle1(
                env, {.owner = owner, .documentID = i + 10, .fee = baseFee});
            accounts.push_back(owner.id());
            oracles.push_back(oracle1.documentID());
        }
        for (int i = 0; i < accounts.size(); ++i)
        {
            auto const jv = [&]() {
                // document id is uint32
                if (i % 2)
                    return Oracle::ledgerEntry(env, accounts[i], oracles[i]);
                // document id is string
                return Oracle::ledgerEntry(
                    env, accounts[i], std::to_string(oracles[i]));
            }();
            try
            {
                BEAST_EXPECT(
                    jv[jss::node][jss::Owner] == to_string(accounts[i]));
            }
            catch (...)
            {
                fail();
            }
        }
    }

    void
    testLedgerEntryMPT()
    {
        testcase("ledger_entry Request MPT");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};
        Account const bob("bob");

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create(
            {.transferFee = 10,
             .metadata = "123",
             .ownerCount = 1,
             .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |
                 tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback});
        mptAlice.authorize({.account = bob, .holderCount = 1});

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        std::string const badMptID =
            "00000193B9DDCAF401B5B3B26875986043F82CD0D13B4315";
        {
            // Request the MPTIssuance using its MPTIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mpt_issuance] = strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfMPTokenMetadata.jsonName] ==
                strHex(std::string{"123"}));
            BEAST_EXPECT(
                jrr[jss::node][jss::mpt_issuance_id] ==
                strHex(mptAlice.issuanceID()));
        }
        {
            // Request an index that is not a MPTIssuance.
            Json::Value jvParams;
            jvParams[jss::mpt_issuance] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // Request the MPToken using its owner + mptIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mptoken] = Json::objectValue;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] =
                strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfMPTokenIssuanceID.jsonName] ==
                strHex(mptAlice.issuanceID()));
        }
        {
            // Request the MPToken using a bad mptIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mptoken] = Json::objectValue;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryPermissionedDomain()
    {
        testcase("ledger_entry PermissionedDomain");

        using namespace test::jtx;

        Env env(*this, testable_amendments() | featurePermissionedDomains);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        auto const seq = env.seq(alice);
        env(pdomain::setTx(alice, {{alice, "first credential"}}));
        env.close();
        auto const objects = pdomain::getObjects(alice, env);
        if (!BEAST_EXPECT(objects.size() == 1))
            return;

        {
            // Succeed
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = alice.human();
            params[jss::permissioned_domain][jss::seq] = seq;
            auto jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::PermissionedDomain);

            std::string const pdIdx = jv[jss::result][jss::index].asString();
            BEAST_EXPECT(
                strHex(keylet::permissionedDomain(alice, seq).key) == pdIdx);

            params.clear();
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] = pdIdx;
            jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::PermissionedDomain);
        }

        {
            // Fail, invalid permissioned domain index
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] =
                "12F1F1F1F180D67377B2FAB292A31C922470326268D2B9B74CD1E582645B9A"
                "DE";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "entryNotFound", "");
        }

        {
            // Fail, invalid permissioned domain index
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] = "NotAHexString";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, permissioned domain is not an object
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] = 10;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }

        {
            // Fail, invalid account
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = 1;
            params[jss::permissioned_domain][jss::seq] = seq;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedAddress", "");
        }

        {
            // Fail, account is an object
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] =
                Json::Value{Json::ValueType::objectValue};
            params[jss::permissioned_domain][jss::seq] = seq;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedAddress", "");
        }

        {
            // Fail, no account
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = "";
            params[jss::permissioned_domain][jss::seq] = seq;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedAddress", "");
        }

        {
            // Fail, invalid sequence
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = alice.human();
            params[jss::permissioned_domain][jss::seq] = "12g";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "malformedRequest", "");
        }
    }

    void
    testLedgerEntryCLI()
    {
        testcase("ledger_entry command-line");
        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const checkId = keylet::check(env.master, env.seq(env.master));

        env(check::create(env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value const jrr =
                env.rpc("ledger_entry", to_string(checkId.key))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
    }

public:
    void
    run() override
    {
        testLedgerEntryInvalid();
        testLedgerEntryAccountRoot();
        testLedgerEntryCheck();
        testLedgerEntryCredentials();
        testLedgerEntryDelegate();
        testLedgerEntryDepositPreauth();
        testLedgerEntryDepositPreauthCred();
        testLedgerEntryDirectory();
        testLedgerEntryEscrow();
        testLedgerEntryOffer();
        testLedgerEntryPayChan();
        testLedgerEntryRippleState();
        testLedgerEntryTicket();
        testLedgerEntryDID();
        testInvalidOracleLedgerEntry();
        testOracleLedgerEntry();
        testLedgerEntryMPT();
        testLedgerEntryPermissionedDomain();
        testLedgerEntryCLI();

        forAllApiVersions(std::bind_front(
            &LedgerEntry_test::testLedgerEntryInvalidParams, this));
    }
};

class LedgerEntry_XChain_test : public beast::unit_test::suite,
                                public test::jtx::XChainBridgeObjects
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

    void
    testLedgerEntryBridge()
    {
        testcase("ledger_entry: bridge");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        std::string const ledgerHash{to_string(mcEnv.closed()->info().hash)};
        std::string bridge_index;
        Json::Value mcBridge;
        {
            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == mcDoor.human());

            BEAST_EXPECT(r.isMember(jss::Flags));

            BEAST_EXPECT(r.isMember(sfLedgerEntryType.jsonName));
            BEAST_EXPECT(r[sfLedgerEntryType.jsonName] == jss::Bridge);

            // we not created an account yet
            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 0);

            // we have not claimed a locking chain tx yet
            BEAST_EXPECT(r.isMember(sfXChainAccountClaimCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountClaimCount.jsonName].asInt() == 0);

            BEAST_EXPECT(r.isMember(jss::index));
            bridge_index = r[jss::index].asString();
            mcBridge = r;
        }
        {
            // request the bridge via RPC by index
            Json::Value jvParams;
            jvParams[jss::index] = bridge_index;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node] == mcBridge);
        }
        {
            // swap door accounts and make sure we get an error value
            Json::Value jvParams;
            // Sidechain door account is "master", not scDoor
            jvParams[jss::bridge_account] = Account::master.human();
            jvParams[jss::bridge] = jvb;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // create two claim ids and verify that the bridge counter was
            // incremented
            mcEnv(xchain_create_claim_id(mcAlice, jvb, reward, scAlice));
            mcEnv.close();
            mcEnv(xchain_create_claim_id(mcBob, jvb, reward, scBob));
            mcEnv.close();

            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            // we executed two create claim id txs
            BEAST_EXPECT(r.isMember(sfXChainClaimID.jsonName));
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
        }
    }

    void
    testLedgerEntryClaimID()
    {
        testcase("ledger_entry: xchain_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        scEnv(xchain_create_claim_id(scAlice, jvb, reward, mcAlice));
        scEnv.close();
        scEnv(xchain_create_claim_id(scBob, jvb, reward, mcBob));
        scEnv.close();

        std::string bridge_index;
        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scAlice.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 1);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }

        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                2;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scBob.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }
    }

    void
    testLedgerEntryCreateAccountClaimID()
    {
        testcase("ledger_entry: xchain_create_account_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        // note: signers.size() and quorum are both 5 in createBridgeObjects
        createBridgeObjects(mcEnv, scEnv);

        auto scCarol =
            Account("scCarol");  // Don't fund it - it will be created with the
                                 // xchain transaction
        auto const amt = XRP(1000);
        mcEnv(sidechain_xchain_account_create(
            mcAlice, jvb, scCarol, amt, reward));
        mcEnv.close();

        // send less than quorum of attestations (otherwise funds are
        // immediately transferred and no "claim" object is created)
        size_t constexpr num_attest = 3;
        auto attestations = create_account_attestations(
            scAttester,
            jvb,
            mcAlice,
            amt,
            reward,
            payee,
            /*wasLockingChainSend*/ true,
            1,
            scCarol,
            signers,
            UT_XCHAIN_DEFAULT_NUM_SIGNERS);
        for (size_t i = 0; i < num_attest; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();

        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            // std::cout << to_string(jrr) << '\n';

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == Account::master.human());

            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 1);

            BEAST_EXPECT(
                r.isMember(sfXChainCreateAccountAttestations.jsonName));
            auto attest = r[sfXChainCreateAccountAttestations.jsonName];
            BEAST_EXPECT(attest.isArray());
            BEAST_EXPECT(attest.size() == 3);
            BEAST_EXPECT(attest[Json::Value::UInt(0)].isMember(
                sfXChainCreateAccountProofSig.jsonName));
            Json::Value a[num_attest];
            for (size_t i = 0; i < num_attest; ++i)
            {
                a[i] = attest[Json::Value::UInt(0)]
                             [sfXChainCreateAccountProofSig.jsonName];
                BEAST_EXPECT(
                    a[i].isMember(jss::Amount) &&
                    a[i][jss::Amount].asInt() == 1000 * drop_per_xrp);
                BEAST_EXPECT(
                    a[i].isMember(jss::Destination) &&
                    a[i][jss::Destination] == scCarol.human());
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationSignerAccount.jsonName) &&
                    std::any_of(
                        signers.begin(), signers.end(), [&](signer const& s) {
                            return a[i][sfAttestationSignerAccount.jsonName] ==
                                s.account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationRewardAccount.jsonName) &&
                    std::any_of(
                        payee.begin(),
                        payee.end(),
                        [&](Account const& account) {
                            return a[i][sfAttestationRewardAccount.jsonName] ==
                                account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfWasLockingChainSend.jsonName) &&
                    a[i][sfWasLockingChainSend.jsonName] == 1);
                BEAST_EXPECT(
                    a[i].isMember(sfSignatureReward.jsonName) &&
                    a[i][sfSignatureReward.jsonName].asInt() ==
                        1 * drop_per_xrp);
            }
        }

        // complete attestations quorum - CreateAccountClaimID should not be
        // present anymore
        for (size_t i = num_attest; i < UT_XCHAIN_DEFAULT_NUM_SIGNERS; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();
        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

public:
    void
    run() override
    {
        testLedgerEntryBridge();
        testLedgerEntryClaimID();
        testLedgerEntryCreateAccountClaimID();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerEntry, rpc, ripple);
BEAST_DEFINE_TESTSUITE(LedgerEntry_XChain, rpc, ripple);

}  // namespace test
}  // namespace ripple
