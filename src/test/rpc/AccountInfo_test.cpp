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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <ripple/resource/Charge.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <test/jtx/WSClient.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {

class AccountInfo_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        using namespace jtx;
        Env env(*this);
        {
            // account_info with no account.
            auto const info = env.rpc("json", "account_info", "{ }");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] ==
                "Missing field 'account'.");
        }
        {
            // account_info with a malformed account string.
            auto const info = env.rpc(
                "json",
                "account_info",
                "{\"account\": "
                "\"n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV\"}");
            BEAST_EXPECT(
                info[jss::result][jss::error_code] == rpcACT_MALFORMED);
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Account malformed.");
        }
        {
            // account_info with an account that's not in the ledger.
            Account const bogie{"bogie"};
            auto const info = env.rpc(
                "json",
                "account_info",
                R"({ "account": ")" + bogie.human() + R"("})");
            BEAST_EXPECT(
                info[jss::result][jss::error_code] == rpcACT_NOT_FOUND);
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Account not found.");
        }
        {
            // Cannot use a seed as account
            auto const info =
                env.rpc("json", "account_info", R"({"account": "foo"})");
            BEAST_EXPECT(
                info[jss::result][jss::error_code] == rpcACT_MALFORMED);
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Account malformed.");
        }
    }

    // Test the "signer_lists" argument in account_info.
    void
    testSignerLists()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners =
            std::string("{ ") + "\"account\": \"" + alice.human() + "\"}";

        auto const withSigners = std::string("{ ") + "\"account\": \"" +
            alice.human() + "\", " + "\"signer_lists\": true }";

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            BEAST_EXPECT(entry0[sfSignerWeight.jsonName] == 3);
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};

        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                BEAST_EXPECT(entry.size() == 2);
                BEAST_EXPECT(entry.isMember(sfAccount.jsonName));
                BEAST_EXPECT(entry[sfSignerWeight.jsonName] == 1);
            }
        }
    }

    // Test the "signer_lists" argument in account_info, with api_version 2.
    void
    testSignerListsApiVersion2()
    {
        using namespace jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners = std::string("{ ") +
            "\"api_version\": 2, \"account\": \"" + alice.human() + "\"}";

        auto const withSigners = std::string("{ ") +
            "\"api_version\": 2, \"account\": \"" + alice.human() + "\", " +
            "\"signer_lists\": true }";

        auto const withSignersAsString = std::string("{ ") +
            "\"api_version\": 2, \"account\": \"" + alice.human() + "\", " +
            "\"signer_lists\": asdfggh }";

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(info.isMember(jss::result));
            BEAST_EXPECT(!info[jss::result].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(info.isMember(jss::result));
            auto const& data = info[jss::result];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(info.isMember(jss::result));
            BEAST_EXPECT(!info[jss::result].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(info.isMember(jss::result));
            auto const& data = info[jss::result];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            BEAST_EXPECT(entry0[sfSignerWeight.jsonName] == 3);
        }
        {
            // account_info with "signer_lists" as not bool should error out
            auto const info =
                env.rpc("json", "account_info", withSignersAsString);
            BEAST_EXPECT(info[jss::status] == "error");
            BEAST_EXPECT(info[jss::error] == "invalidParams");
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};

        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(info.isMember(jss::result));
            auto const& data = info[jss::result];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                BEAST_EXPECT(entry.size() == 2);
                BEAST_EXPECT(entry.isMember(sfAccount.jsonName));
                BEAST_EXPECT(entry[sfSignerWeight.jsonName] == 1);
            }
        }
    }

    // Test the "signer_lists" argument in account_info, version 2 API.
    void
    testSignerListsV2()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"account_info\", "
            "\"params\": { "
            "\"account\": \"" +
            alice.human() + "\"}}";

        auto const withSigners = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"account_info\", "
            "\"params\": { "
            "\"account\": \"" +
            alice.human() + "\", " + "\"signer_lists\": true }}";
        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json2", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
        {
            // Do both of the above as a batch job
            auto const info = env.rpc(
                "json2", '[' + withoutSigners + ", " + withSigners + ']');
            BEAST_EXPECT(
                info[0u].isMember(jss::result) &&
                info[0u][jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[0u][jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info[0u].isMember(jss::jsonrpc) &&
                info[0u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info[0u].isMember(jss::ripplerpc) &&
                info[0u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[0u].isMember(jss::id) && info[0u][jss::id] == 5);

            BEAST_EXPECT(
                info[1u].isMember(jss::result) &&
                info[1u][jss::result].isMember(jss::account_data));
            auto const& data = info[1u][jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(
                info[1u].isMember(jss::jsonrpc) &&
                info[1u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info[1u].isMember(jss::ripplerpc) &&
                info[1u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[1u].isMember(jss::id) && info[1u][jss::id] == 6);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json2", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            BEAST_EXPECT(entry0[sfSignerWeight.jsonName] == 3);
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};

        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                BEAST_EXPECT(entry.size() == 2);
                BEAST_EXPECT(entry.isMember(sfAccount.jsonName));
                BEAST_EXPECT(entry[sfSignerWeight.jsonName] == 1);
            }
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
    }

    void
    testAccountFlags(FeatureBitset const& features)
    {
        using namespace jtx;

        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1000), alice, bob);

        auto getAccountFlag = [&env](
                                  std::string_view fName,
                                  Account const& account) {
            auto const info = env.rpc(
                "json",
                "account_info",
                R"({"account" : ")" + account.human() + R"("})");

            std::optional<bool> res;
            if (info[jss::result][jss::status] == "success" &&
                info[jss::result][jss::account_flags].isMember(fName.data()))
                res.emplace(info[jss::result][jss::account_flags][fName.data()]
                                .asBool());

            return res;
        };

        static constexpr std::
            array<std::pair<std::string_view, std::uint32_t>, 7>
                asFlags{
                    {{"defaultRipple", asfDefaultRipple},
                     {"depositAuth", asfDepositAuth},
                     {"disallowIncomingXRP", asfDisallowXRP},
                     {"globalFreeze", asfGlobalFreeze},
                     {"noFreeze", asfNoFreeze},
                     {"requireAuthorization", asfRequireAuth},
                     {"requireDestinationTag", asfRequireDest}}};

        for (auto& asf : asFlags)
        {
            // Clear a flag and check that account_info returns results
            // as expected
            env(fclear(alice, asf.second));
            env.close();
            auto const f1 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());

            // Set a flag and check that account_info returns results
            // as expected
            env(fset(alice, asf.second));
            env.close();
            auto const f2 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());
        }

        static constexpr std::
            array<std::pair<std::string_view, std::uint32_t>, 4>
                disallowIncomingFlags{
                    {{"disallowIncomingCheck", asfDisallowIncomingCheck},
                     {"disallowIncomingNFTokenOffer",
                      asfDisallowIncomingNFTokenOffer},
                     {"disallowIncomingPayChan", asfDisallowIncomingPayChan},
                     {"disallowIncomingTrustline",
                      asfDisallowIncomingTrustline}}};

        if (features[featureDisallowIncoming])
        {
            for (auto& asf : disallowIncomingFlags)
            {
                // Clear a flag and check that account_info returns results
                // as expected
                env(fclear(alice, asf.second));
                env.close();
                auto const f1 = getAccountFlag(asf.first, alice);
                BEAST_EXPECT(f1.has_value());
                BEAST_EXPECT(!f1.value());

                // Set a flag and check that account_info returns results
                // as expected
                env(fset(alice, asf.second));
                env.close();
                auto const f2 = getAccountFlag(asf.first, alice);
                BEAST_EXPECT(f2.has_value());
                BEAST_EXPECT(f2.value());
            }
        }
        else
        {
            for (auto& asf : disallowIncomingFlags)
            {
                BEAST_EXPECT(!getAccountFlag(asf.first, alice));
            }
        }

        static constexpr std::pair<std::string_view, std::uint32_t>
            allowTrustLineClawbackFlag{
                "allowTrustLineClawback", asfAllowTrustLineClawback};

        if (features[featureClawback])
        {
            // must use bob's account because alice has noFreeze set
            auto const f1 =
                getAccountFlag(allowTrustLineClawbackFlag.first, bob);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());

            // Set allowTrustLineClawback
            env(fset(bob, allowTrustLineClawbackFlag.second));
            env.close();
            auto const f2 =
                getAccountFlag(allowTrustLineClawbackFlag.first, bob);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());
        }
        else
        {
            BEAST_EXPECT(
                !getAccountFlag(allowTrustLineClawbackFlag.first, bob));
        }
    }

    void
    run() override
    {
        testErrors();
        testSignerLists();
        testSignerListsApiVersion2();
        testSignerListsV2();

        FeatureBitset const allFeatures{
            ripple::test::jtx::supported_amendments()};
        testAccountFlags(allFeatures);
        testAccountFlags(allFeatures - featureDisallowIncoming);
        testAccountFlags(
            allFeatures - featureDisallowIncoming - featureClawback);
    }
};

BEAST_DEFINE_TESTSUITE(AccountInfo, app, ripple);

}  // namespace test
}  // namespace ripple
