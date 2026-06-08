
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace xrpl::test {

class AccountInfo_test : public beast::unit_test::Suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");
        using namespace jtx;
        Env env(*this);
        {
            // account_info with no account.
            auto const info = env.rpc("json", "account_info", "{ }");
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Missing field 'account'.");
        }
        {
            // account_info with a malformed account string.
            auto const info = env.rpc(
                "json",
                "account_info",
                "{\"account\": "
                "\"n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV\"}");
            BEAST_EXPECT(info[jss::result][jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Account malformed.");
        }
        {
            // account_info with an account that's not in the ledger.
            Account const bogie{"bogie"};
            json::Value params;
            params[jss::account] = bogie.human();
            auto const info = env.rpc("json", "account_info", to_string(params));
            BEAST_EXPECT(info[jss::result][jss::error_code] == RpcActNotFound);
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Account not found.");
        }
        {
            // Cannot use a seed as account
            auto const info = env.rpc("json", "account_info", R"({"account": "foo"})");
            BEAST_EXPECT(info[jss::result][jss::error_code] == RpcActMalformed);
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Account malformed.");
        }
        {
            // Cannot pass a non-string into the `account` param

            auto testInvalidAccountParam = [&](auto const& param) {
                json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc("json", "account_info", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(json::Value(json::ValueType::Null));
            testInvalidAccountParam(json::Value(json::ValueType::Object));
            testInvalidAccountParam(json::Value(json::ValueType::Array));
        }
        {
            // Cannot pass a non-string into the `ident` param

            auto testInvalidIdentParam = [&](auto const& param) {
                json::Value params;
                params[jss::ident] = param;
                auto jrr = env.rpc("json", "account_info", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'ident'.");
            };

            testInvalidIdentParam(1);
            testInvalidIdentParam(1.1);
            testInvalidIdentParam(true);
            testInvalidIdentParam(json::Value(json::ValueType::Null));
            testInvalidIdentParam(json::Value(json::ValueType::Object));
            testInvalidIdentParam(json::Value(json::ValueType::Array));
        }
    }

    // Test the "signer_lists" argument in account_info.
    void
    testSignerLists()
    {
        testcase("Signer lists");
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        json::Value withoutSigners;
        withoutSigners[jss::account] = alice.human();

        json::Value withSigners;
        withSigners[jss::account] = alice.human();
        withSigners[jss::signer_lists] = true;

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withoutSigners));
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withoutSigners));
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
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

        json::Value const bigSigners = signers(
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
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
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
        testcase("Signer lists APIv2");
        using namespace jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        json::Value withoutSigners;
        withoutSigners[jss::api_version] = 2;
        withoutSigners[jss::account] = alice.human();

        json::Value withSigners;
        withSigners[jss::api_version] = 2;
        withSigners[jss::account] = alice.human();
        withSigners[jss::signer_lists] = true;

        auto const withSignersAsString = std::string("{ ") + "\"api_version\": 2, \"account\": \"" +
            alice.human() + "\", " + "\"signer_lists\": asdfggh }";

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withoutSigners));
            BEAST_EXPECT(info.isMember(jss::result));
            BEAST_EXPECT(!info[jss::result].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
            BEAST_EXPECT(info.isMember(jss::result));
            auto const& data = info[jss::result];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withoutSigners));
            BEAST_EXPECT(info.isMember(jss::result));
            BEAST_EXPECT(!info[jss::result].isMember(jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
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
            auto const info = env.rpc("json", "account_info", withSignersAsString);
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

        json::Value const bigSigners = signers(
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
            auto const info = env.rpc("json", "account_info", to_string(withSigners));
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
        testcase("Signer lists v2");
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
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(jss::signer_lists));
            BEAST_EXPECT(info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
        {
            // Do both of the above as a batch job
            auto const info = env.rpc("json2", '[' + withoutSigners + ", " + withSigners + ']');
            BEAST_EXPECT(
                info[0u].isMember(jss::result) &&
                info[0u][jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[0u][jss::result][jss::account_data].isMember(jss::signer_lists));
            BEAST_EXPECT(info[0u].isMember(jss::jsonrpc) && info[0u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info[0u].isMember(jss::ripplerpc) && info[0u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[0u].isMember(jss::id) && info[0u][jss::id] == 5);

            BEAST_EXPECT(
                info[1u].isMember(jss::result) &&
                info[1u][jss::result].isMember(jss::account_data));
            auto const& data = info[1u][jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(info[1u].isMember(jss::jsonrpc) && info[1u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info[1u].isMember(jss::ripplerpc) && info[1u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[1u].isMember(jss::id) && info[1u][jss::id] == 6);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json2", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(jss::signer_lists));
            BEAST_EXPECT(info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
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
            BEAST_EXPECT(info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
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

        json::Value const bigSigners = signers(
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
                info.isMember(jss::result) && info[jss::result].isMember(jss::account_data));
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
            BEAST_EXPECT(info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
    }

    void
    testAccountFlags(FeatureBitset const& features)
    {
        testcase("Account flags");
        using namespace jtx;

        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1000), alice, bob);

        auto getAccountFlag = [&env](std::string_view fName, Account const& account) {
            json::Value params;
            params[jss::account] = account.human();
            auto const info = env.rpc("json", "account_info", to_string(params));
            auto const name = std::string(fName);

            std::optional<bool> res;
            if (info[jss::result][jss::status] == "success" &&
                info[jss::result][jss::account_flags].isMember(name))
                res.emplace(info[jss::result][jss::account_flags][name].asBool());

            return res;
        };

        static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7> kAsFlags{
            {{"defaultRipple", asfDefaultRipple},
             {"depositAuth", asfDepositAuth},
             {"disallowIncomingXRP", asfDisallowXRP},
             {"globalFreeze", asfGlobalFreeze},
             {"noFreeze", asfNoFreeze},
             {"requireAuthorization", asfRequireAuth},
             {"requireDestinationTag", asfRequireDest}}};

        for (auto& asf : kAsFlags)
        {
            // Clear a flag and check that account_info returns results
            // as expected
            env(fclear(alice, asf.second));
            env.close();
            auto const f1 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());  // NOLINT(bugprone-unchecked-optional-access)

            // Set a flag and check that account_info returns results
            // as expected
            env(fset(alice, asf.second));
            env.close();
            auto const f2 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());  // NOLINT(bugprone-unchecked-optional-access)
        }

        static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 4>
            kDisallowIncomingFlags{
                {{"disallowIncomingCheck", asfDisallowIncomingCheck},
                 {"disallowIncomingNFTokenOffer", asfDisallowIncomingNFTokenOffer},
                 {"disallowIncomingPayChan", asfDisallowIncomingPayChan},
                 {"disallowIncomingTrustline", asfDisallowIncomingTrustline}}};

        for (auto& asf : kDisallowIncomingFlags)
        {
            // Clear a flag and check that account_info returns results
            // as expected
            env(fclear(alice, asf.second));
            env.close();
            auto const f1 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());  // NOLINT(bugprone-unchecked-optional-access)

            // Set a flag and check that account_info returns results
            // as expected
            env(fset(alice, asf.second));
            env.close();
            auto const f2 = getAccountFlag(asf.first, alice);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());  // NOLINT(bugprone-unchecked-optional-access)
        }

        static constexpr std::pair<std::string_view, std::uint32_t> kAllowTrustLineClawbackFlag{
            "allowTrustLineClawback", asfAllowTrustLineClawback};

        if (features[featureClawback])
        {
            // must use bob's account because alice has noFreeze set
            auto const f1 = getAccountFlag(kAllowTrustLineClawbackFlag.first, bob);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());  // NOLINT(bugprone-unchecked-optional-access)

            // Set allowTrustLineClawback
            env(fset(bob, kAllowTrustLineClawbackFlag.second));
            env.close();
            auto const f2 = getAccountFlag(kAllowTrustLineClawbackFlag.first, bob);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());  // NOLINT(bugprone-unchecked-optional-access)
        }
        else
        {
            BEAST_EXPECT(!getAccountFlag(kAllowTrustLineClawbackFlag.first, bob));
        }

        static constexpr std::pair<std::string_view, std::uint32_t> kAllowTrustLineLockingFlag{
            "allowTrustLineLocking", asfAllowTrustLineLocking};

        if (features[featureTokenEscrow])
        {
            auto const f1 = getAccountFlag(kAllowTrustLineLockingFlag.first, bob);
            BEAST_EXPECT(f1.has_value());
            BEAST_EXPECT(!f1.value());  // NOLINT(bugprone-unchecked-optional-access)

            // Set allowTrustLineLocking
            env(fset(bob, kAllowTrustLineLockingFlag.second));
            env.close();
            auto const f2 = getAccountFlag(kAllowTrustLineLockingFlag.first, bob);
            BEAST_EXPECT(f2.has_value());
            BEAST_EXPECT(f2.value());  // NOLINT(bugprone-unchecked-optional-access)
        }
        else
        {
            BEAST_EXPECT(!getAccountFlag(kAllowTrustLineLockingFlag.first, bob));
        }
    }

    void
    run() override
    {
        testErrors();
        testSignerLists();
        testSignerListsApiVersion2();
        testSignerListsV2();

        FeatureBitset const allFeatures{xrpl::test::jtx::testableAmendments()};
        testAccountFlags(allFeatures);
        testAccountFlags(allFeatures - featureClawback);
        testAccountFlags(allFeatures - featureClawback - featureTokenEscrow);
    }
};

BEAST_DEFINE_TESTSUITE(AccountInfo, rpc, xrpl);

}  // namespace xrpl::test
