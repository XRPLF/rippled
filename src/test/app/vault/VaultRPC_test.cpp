#include <test/app/vault/VaultManagerTestBase.h>
#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultRPC_test : public VaultManagerTestBase
{
private:
    void
    testRPC()
    {
        using namespace test::jtx;

        testcase("RPC");
        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(200)));
        env.close();

        auto const sequence = env.seq(owner);
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        // Set some fields
        {
            auto tx1 = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx1);

            auto tx2 = vault.set({.owner = owner, .id = keylet.key});
            tx2[sfAssetsMaximum] = asset(1000).number();
            env(tx2);
            env.close();
        }

        auto const sleVault = [&env, keylet = keylet, this]() {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return vault;
        }();

        auto const check = [&, keylet = keylet, sle = sleVault, this](
                               json::Value const& vault,
                               json::Value const& issuance = json::ValueType::Null) {
            BEAST_EXPECT(vault.isObject());

            static constexpr auto kCheckString =
                [](auto& node, SField const& field, std::string v) -> bool {
                return node.isMember(field.fieldName) && node[field.fieldName].isString() &&
                    node[field.fieldName] == v;
            };
            static constexpr auto kCheckObject =
                [](auto& node, SField const& field, json::Value v) -> bool {
                return node.isMember(field.fieldName) && node[field.fieldName].isObject() &&
                    node[field.fieldName] == v;
            };
            static constexpr auto kCheckInt = [](auto& node, SField const& field, int v) -> bool {
                return node.isMember(field.fieldName) &&
                    ((node[field.fieldName].isInt() && node[field.fieldName] == json::Int(v)) ||
                     (node[field.fieldName].isUInt() && node[field.fieldName] == json::UInt(v)));
            };

            BEAST_EXPECT(vault["LedgerEntryType"].asString() == "Vault");
            BEAST_EXPECT(vault[jss::index].asString() == strHex(keylet.key));
            BEAST_EXPECT(kCheckInt(vault, sfFlags, 0));
            // Ignore all other standard fields, this test doesn't care

            BEAST_EXPECT(kCheckString(vault, sfAccount, toBase58(sle->at(sfAccount))));
            BEAST_EXPECT(kCheckObject(vault, sfAsset, toJson(sle->at(sfAsset))));
            BEAST_EXPECT(kCheckString(vault, sfAssetsAvailable, "50"));
            BEAST_EXPECT(kCheckString(vault, sfAssetsMaximum, "1000"));
            BEAST_EXPECT(kCheckString(vault, sfAssetsTotal, "50"));
            BEAST_EXPECT(!vault.isMember(sfLossUnrealized.getJsonName()));

            auto const strShareID = strHex(sle->at(sfShareMPTID));
            BEAST_EXPECT(kCheckString(vault, sfShareMPTID, strShareID));
            BEAST_EXPECT(kCheckString(vault, sfOwner, toBase58(owner.id())));
            BEAST_EXPECT(kCheckInt(vault, sfSequence, sequence));
            BEAST_EXPECT(kCheckInt(vault, sfWithdrawalPolicy, kVaultStrategyFirstComeFirstServe));

            if (issuance.isObject())
            {
                BEAST_EXPECT(issuance["LedgerEntryType"].asString() == "MPTokenIssuance");
                BEAST_EXPECT(issuance[jss::mpt_issuance_id].asString() == strShareID);
                BEAST_EXPECT(kCheckInt(issuance, sfSequence, 1));
                BEAST_EXPECT(kCheckInt(
                    issuance, sfFlags, int(lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer)));
                BEAST_EXPECT(kCheckString(issuance, sfOutstandingAmount, "50000000"));
            }
        };

        // An error response must carry a registered token together with the matching code and
        // message, so that clients dispatching on either of them reach the same conclusion.
        auto const checkError = [this](
                                    json::Value const& result,
                                    std::string const& token,
                                    ErrorCodeI const code,
                                    std::string const& message) {
            BEAST_EXPECT(result[jss::error].asString() == token);
            BEAST_EXPECT(result[jss::error_code].asInt() == code);
            BEAST_EXPECT(result[jss::error_message].asString() == message);
        };

        std::string const badSeqMessage = "Invalid field 'seq', not a positive 32-bit integer.";
        std::string const badFieldsMessage =
            "Must specify either 'vault_id' or both 'owner' and 'seq'.";

        {
            testcase("RPC ledger_entry selected by key");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(keylet.key);
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(!jvVault[jss::result].isMember(jss::error));
            BEAST_EXPECT(jvVault[jss::result].isMember(jss::node));
            check(jvVault[jss::result][jss::node]);
        }

        {
            testcase("RPC ledger_entry selected by owner and seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = owner.human();
            jvParams[jss::vault][jss::seq] = sequence;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(!jvVault[jss::result].isMember(jss::error));
            BEAST_EXPECT(jvVault[jss::result].isMember(jss::node));
            check(jvVault[jss::result][jss::node]);
        }

        {
            testcase("RPC ledger_entry cannot find vault by key");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = to_string(uint256(42));
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry cannot find vault by owner and seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1'000'000;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry malformed key");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = 42;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry malformed owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = 42;
            jvParams[jss::vault][jss::seq] = sequence;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedOwner");
        }

        {
            testcase("RPC ledger_entry malformed seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = "foo";
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry negative seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = -1;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry oversized seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1e20;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry bool seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = true;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC account_objects");

            json::Value jvParams;
            jvParams[jss::account] = owner.human();
            jvParams[jss::type] = jss::vault;
            auto jv = env.rpc("json", "account_objects", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jv[jss::account_objects].size() == 1);
            check(jv[jss::account_objects][0u]);
        }

        {
            testcase("RPC ledger_data");

            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::binary] = false;
            jvParams[jss::type] = jss::vault;
            json::Value jv = env.rpc("json", "ledger_data", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::state].size() == 1);
            check(jv[jss::result][jss::state][0u]);
        }

        {
            testcase("RPC vault_info command line");
            json::Value jv = env.rpc("vault_info", strHex(keylet.key), "validated");

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info invalid vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(
                jv[jss::result],
                "invalidParams",
                RpcInvalidParams,
                "Invalid field 'vault_id', not hex string.");
        }

        {
            testcase("RPC vault_info json numeric vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(
                jv[jss::result],
                "invalidParams",
                RpcInvalidParams,
                "Invalid field 'vault_id', not hex string.");
        }

        {
            testcase("RPC vault_info json object vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = json::Value(json::ValueType::Object);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(
                jv[jss::result],
                "invalidParams",
                RpcInvalidParams,
                "Invalid field 'vault_id', not hex string.");
        }

        {
            // An all-zero key is a well-formed request for a vault that cannot exist, not a
            // malformed one. parseHex accepts both the padded form and the short "0".
            testcase("RPC vault_info json all zero vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(uint256(beast::kZero));
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "entryNotFound", RpcEntryNotFound, "Entry not found.");
        }

        {
            testcase("RPC vault_info json short zero vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = "0";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "entryNotFound", RpcEntryNotFound, "Entry not found.");
        }

        {
            testcase("RPC vault_info json by owner and sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json malformed sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badSeqMessage);
        }

        {
            testcase("RPC vault_info json invalid sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badSeqMessage);
        }

        {
            testcase("RPC vault_info json negative sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = -1;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badSeqMessage);
        }

        {
            testcase("RPC vault_info json oversized sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 1e20;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badSeqMessage);
        }

        {
            testcase("RPC vault_info json bool sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = true;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badSeqMessage);
        }

        {
            testcase("RPC vault_info json malformed owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = "foobar";
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(
                jv[jss::result],
                "actMalformed",
                RpcActMalformed,
                "Invalid field 'owner', not AccountID.");
        }

        {
            testcase("RPC vault_info json array owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = json::Value(json::ValueType::Array);
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(
                jv[jss::result],
                "actMalformed",
                RpcActMalformed,
                "Invalid field 'owner', not AccountID.");
        }

        {
            testcase("RPC vault_info json invalid combination only owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase("RPC vault_info json invalid combination only seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase("RPC vault_info json invalid combination seq vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase("RPC vault_info json invalid combination owner vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase(
                "RPC vault_info json invalid combination owner seq "
                "vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase("RPC vault_info json no input");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            checkError(jv[jss::result], "invalidParams", RpcInvalidParams, badFieldsMessage);
        }

        {
            testcase("RPC vault_info command line invalid index");
            json::Value jv = env.rpc("vault_info", "foobar", "validated");
            BEAST_EXPECT(jv[jss::error].asString() == "invalidParams");
        }

        {
            testcase("RPC vault_info command line zero index");
            json::Value jv = env.rpc("vault_info", "0", "validated");
            checkError(jv[jss::result], "entryNotFound", RpcEntryNotFound, "Entry not found.");
        }

        {
            testcase("RPC vault_info command line unknown index");
            json::Value jv = env.rpc("vault_info", strHex(uint256(42)), "validated");
            checkError(jv[jss::result], "entryNotFound", RpcEntryNotFound, "Entry not found.");
        }

        {
            testcase("RPC vault_info command line invalid ledger");
            json::Value jv = env.rpc("vault_info", strHex(keylet.key), "0");
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "lgrNotFound");
        }
    }

    // RPC coverage: closed-ended vaults must return VaultKind, SubscriptionDate and RedemptionDate
    // in both vault_info and ledger_entry responses. Open-ended vaults must not.
    void
    testRPCClosedEnded()
    {
        using namespace test::jtx;

        testcase("RPC closed-ended vault fields");
        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const owner2{"owner2"};
        env.fund(XRP(1000), owner, owner2);
        env.close();

        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);
        Asset const asset = xrpIssue();
        auto const sub = env.now().time_since_epoch().count() + 60;
        auto const red = sub + kMinInvestmentPeriod;

        Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = asset,
             .vaultKind = closedEnded,
             .subscriptionDate = sub,
             .redemptionDate = red});
        env(tx);
        env.close();

        auto [tx2, keylet2] = vault.create({.owner = owner2, .asset = asset});
        env(tx2);
        env.close();

        auto const asUInt = [](json::Value const& jv) -> json::UInt {
            return jv.isUInt() ? jv.asUInt() : json::UInt(jv.asInt());
        };
        auto const checkClosedEnded = [&](json::Value const& v) {
            BEAST_EXPECT(v.isObject());
            BEAST_EXPECT(v.isMember(sfVaultKind.fieldName));
            BEAST_EXPECT(asUInt(v[sfVaultKind.fieldName]) == json::UInt(closedEnded));
            BEAST_EXPECT(v.isMember(sfSubscriptionDate.fieldName));
            BEAST_EXPECT(asUInt(v[sfSubscriptionDate.fieldName]) == json::UInt(sub));
            BEAST_EXPECT(v.isMember(sfRedemptionDate.fieldName));
            BEAST_EXPECT(asUInt(v[sfRedemptionDate.fieldName]) == json::UInt(red));
        };
        auto const checkOpenEnded = [&](json::Value const& v) {
            BEAST_EXPECT(v.isObject());
            BEAST_EXPECT(!v.isMember(sfVaultKind.fieldName));
            BEAST_EXPECT(!v.isMember(sfSubscriptionDate.fieldName));
            BEAST_EXPECT(!v.isMember(sfRedemptionDate.fieldName));
        };

        {
            json::Value jvParams;
            jvParams[jss::vault_id] = strHex(keylet.key);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            checkClosedEnded(jv[jss::result][jss::vault]);
        }
        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(keylet.key);
            auto jv = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            checkClosedEnded(jv[jss::result][jss::node]);
        }
        {
            json::Value jvParams;
            jvParams[jss::vault_id] = strHex(keylet2.key);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            checkOpenEnded(jv[jss::result][jss::vault]);
        }
        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(keylet2.key);
            auto jv = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            checkOpenEnded(jv[jss::result][jss::node]);
        }
    }

    void
    testManagedVaultLedgerEntry()
    {
        testcase("RPC ledger_entry for a managed vault");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();

        auto const entry = [&](json::Value params) {
            params[jss::ledger_index] = jss::validated;
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };
        auto const shares = [&] {
            auto const issuance =
                env.le(keylet::mptokenIssuance(env.le(f.vault)->at(sfShareMPTID)));
            return Number{static_cast<std::int64_t>(issuance->at(sfOutstandingAmount))};
        };

        {
            json::Value params;
            params[jss::vault] = strHex(f.vaultID());
            auto const node = entry(params)[jss::node];
            BEAST_EXPECT(node[jss::accrued_interest].asString() == to_string(Number{}));
            BEAST_EXPECT(node[jss::positions_value].asString() == to_string(Number{1'000}));
            BEAST_EXPECT(node[jss::nav].asString() == to_string(Number{10'000}));
            BEAST_EXPECT(node[jss::share_price].asString() == to_string(Number{10'000} / shares()));
            BEAST_EXPECT(node[jss::dealing_halted_until].asUInt() == 0);
            BEAST_EXPECT(node[jss::positions].size() == 1);
            auto const& p = node[jss::positions][0u];
            BEAST_EXPECT(p[jss::asset] == toJson(Asset{f.eq.raw()}));
            BEAST_EXPECT(p[jss::holding].asString() == to_string(Number{100}));
            BEAST_EXPECT(p[jss::mark_price].asString() == to_string(Number{10}));
            BEAST_EXPECT(
                p[jss::last_mark_time].asUInt() ==
                env.le(positionKeylet(f, f.eq))->at(sfLastMarkTime));
            BEAST_EXPECT(p[jss::cost_basis].asString() == to_string(Number{1'000}));
            BEAST_EXPECT(p[jss::value].asString() == to_string(Number{1'000}));
            BEAST_EXPECT(!p[jss::stale].asBool());
        }

        // Past sfMarkStaleAfter the read reports the position stale.
        env.close(tp{d{env.le(positionKeylet(f, f.eq))->at(sfLastMarkTime) + kStaleAfter + 10}});
        env.close();
        {
            json::Value params;
            params[jss::vault] = strHex(f.vaultID());
            BEAST_EXPECT(entry(params)[jss::node][jss::positions][0u][jss::stale].asBool());
        }

        // The manager entry and the position, by index and by fields.
        {
            json::Value params;
            params[jss::vault_manager][jss::vault_id] = to_string(f.vaultID());
            auto const byFields = entry(params);
            BEAST_EXPECT(byFields[jss::index].asString() == to_string(managerKeylet(f).key));
            json::Value byIndex;
            byIndex[jss::vault_manager] = strHex(managerKeylet(f).key);
            BEAST_EXPECT(entry(byIndex)[jss::node][sfManager.jsonName] == f.manager.human());
        }
        {
            json::Value params;
            params[jss::vault_position][jss::vault_id] = to_string(f.vaultID());
            params[jss::vault_position][jss::asset] = toJson(Asset{f.eq.raw()});
            auto const byFields = entry(params);
            BEAST_EXPECT(byFields[jss::index].asString() == to_string(positionKeylet(f, f.eq).key));
            json::Value byIndex;
            byIndex[jss::vault_position] = strHex(positionKeylet(f, f.eq).key);
            BEAST_EXPECT(entry(byIndex)[jss::node].isMember(sfMarkPrice.jsonName));
        }

        // account_objects filters the owner's entries by the new types.
        for (auto const& type : {jss::vault_manager, jss::vault_position})
        {
            json::Value params;
            params[jss::account] = f.owner.human();
            params[jss::type] = type;
            auto const res = env.rpc("json", "account_objects", to_string(params))[jss::result];
            BEAST_EXPECT(res[jss::account_objects].size() == 1);
        }

        // A vault without a manager has NAV and price, and no positions.
        {
            Env env2{*this, all_};
            auto const g = makeFund(env2, FundArgs{.createManager = false});
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::vault] = strHex(g.vaultID());
            auto const node =
                env2.rpc("json", "ledger_entry", to_string(params))[jss::result][jss::node];
            BEAST_EXPECT(node[jss::nav].asString() == to_string(Number{10'000}));
            BEAST_EXPECT(node.isMember(jss::accrued_interest));
            BEAST_EXPECT(node.isMember(jss::share_price));
            BEAST_EXPECT(!node.isMember(jss::positions));
            BEAST_EXPECT(!node.isMember(jss::positions_value));
        }
    }

public:
    void
    run() override
    {
        testRPC();
        testRPCClosedEnded();
        testManagedVaultLedgerEntry();
    }
};

BEAST_DEFINE_TESTSUITE(VaultRPC, app, xrpl);

}  // namespace xrpl
