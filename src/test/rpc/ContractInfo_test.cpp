#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/contract.h>
#include <test/jtx/fee.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <string>

namespace xrpl::test {

class ContractInfo_test : public beast::unit_test::Suite
{
    // The ContractHash of the code `codeHex` holds.
    static uint256
    codeHash(std::string const& codeHex)
    {
        auto const code = strUnHex(codeHex);
        if (!code)
            return uint256{};
        return sha512HalfS(Slice(code->data(), code->size()));
    }

    // Creates a contract owned by `owner` from `codeHex` and returns its pseudo-account.
    static jtx::Account
    createContract(
        jtx::Env& env,
        jtx::Account const& owner,
        std::string const& codeHex,
        bool withUri = true)
    {
        using namespace jtx;

        auto const seq = env.seq(owner);
        auto jv = contract::create(owner, codeHex);
        if (withUri)
            jv[sfURI] = strHex(std::string{"https://example.com/contract"});
        env(jv, contract::AddFunction("base", {{0, "uint8", "UINT8"}}), Fee(XRP(200)));
        env.close();

        auto const sle = env.le(keylet::contract(codeHash(codeHex), owner.id(), seq));
        if (!sle)
            return Account{"missing"};
        return Account{"contract", sle->getAccountID(sfContractAccount)};
    }

    static json::Value
    contractInfo(jtx::Env& env, json::Value const& params)
    {
        return env.rpc("json", "contract_info", to_string(params))[jss::result];
    }

    void
    testErrors()
    {
        testcase("errors");

        using namespace jtx;

        Env env{*this, testableAmendments()};
        Account const alice{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        {
            auto const result = contractInfo(env, json::Value{json::ValueType::Object});
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Missing field 'contract_account'.");
        }

        {
            json::Value params;
            params[jss::contract_account] = 1;
            BEAST_EXPECT(contractInfo(env, params)[jss::error] == "invalidParams");
        }

        {
            json::Value params;
            params[jss::contract_account] = alice.human();
            params[jss::account] = 1;
            BEAST_EXPECT(contractInfo(env, params)[jss::error] == "invalidParams");
        }

        {
            json::Value params;
            params[jss::contract_account] = alice.human();
            params[jss::ledger_index] = "nonsense";
            BEAST_EXPECT(contractInfo(env, params).isMember(jss::error));
        }

        {
            json::Value params;
            params[jss::contract_account] = "not an account";
            BEAST_EXPECT(contractInfo(env, params)[jss::error] == "actMalformed");
        }

        {
            // An account that is not in the ledger.
            Account const bogie{"bogie"};
            json::Value params;
            params[jss::contract_account] = bogie.human();
            auto const result = contractInfo(env, params);
            BEAST_EXPECT(result[jss::error] == "actNotFound");
            BEAST_EXPECT(result[jss::contract_account] == bogie.human());
        }

        {
            // An account that is not a contract's pseudo-account.
            json::Value params;
            params[jss::contract_account] = alice.human();
            auto const result = contractInfo(env, params);
            BEAST_EXPECT(result[jss::error] == "objectNotFound");
            BEAST_EXPECT(result[jss::contract_account] == alice.human());
        }

        {
            // A malformed user account.
            auto const contractAccount =
                createContract(env, alice, jtx::contract::wasmOfSize(64, "base"));
            json::Value params;
            params[jss::contract_account] = contractAccount.human();
            params[jss::account] = "not an account";
            BEAST_EXPECT(contractInfo(env, params)[jss::error] == "actMalformed");
        }
    }

    void
    testContract()
    {
        testcase("contract");

        using namespace jtx;

        Env env{*this, testableAmendments()};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const codeHex = jtx::contract::wasmOfSize(64, "base");
        auto const contractAccount = createContract(env, alice, codeHex);

        json::Value params;
        params[jss::contract_account] = contractAccount.human();
        params[jss::account] = bob.human();
        auto const result = contractInfo(env, params);

        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result[jss::contract_account] == contractAccount.human());
        BEAST_EXPECT(result[jss::code] == codeHex);
        BEAST_EXPECT(result[jss::hash] == to_string(codeHash(codeHex)));

        BEAST_EXPECT(result[jss::functions].size() == 1);
        auto const& function = result[jss::functions][0u];
        BEAST_EXPECT(function[jss::name] == strHex(std::string{"base"}));
        BEAST_EXPECT(function[jss::params].size() == 1);
        BEAST_EXPECT(function[jss::params][0u][jss::flags] == 0);
        BEAST_EXPECT(function[jss::params][0u][jss::type] == "UINT8");

        BEAST_EXPECT(
            result[jss::source_code_uri] == strHex(std::string{"https://example.com/contract"}));
        BEAST_EXPECT(result[jss::account_data][jss::Account] == contractAccount.human());

        // Neither the contract nor bob has written data.
        BEAST_EXPECT(!result.isMember(jss::contract_data));
        BEAST_EXPECT(!result.isMember(jss::user_data));

        // A user account that does not exist is answered without user data.
        Account const carol{"carol"};
        params[jss::account] = carol.human();
        auto const noUser = contractInfo(env, params);
        BEAST_EXPECT(!noUser.isMember(jss::error));
        BEAST_EXPECT(!noUser.isMember(jss::user_data));
    }

    // Puts a ContractData entry for `owner` under the contract into the open ledger, holding
    // `value` under the key "count".
    static bool
    putData(
        jtx::Env& env,
        AccountID const& owner,
        AccountID const& contractAccount,
        std::uint32_t value)
    {
        return env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            STJson json;
            json.setObjectField("count", std::make_shared<STUInt32>(sfNetworkID, value));
            auto const sle = std::make_shared<SLE>(keylet::contractData(owner, contractAccount));
            sle->setAccountID(sfOwner, owner);
            sle->setAccountID(sfContractAccount, contractAccount);
            sle->setFieldU64(sfOwnerNode, 0);
            sle->setFieldJson(sfContractJson, json);
            view.rawInsert(sle);
            return true;
        });
    }

    void
    testData()
    {
        testcase("contract and user data");

        using namespace jtx;

        Env env{*this, testableAmendments()};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount =
            createContract(env, alice, jtx::contract::wasmOfSize(64, "base"), false);
        BEAST_EXPECT(putData(env, contractAccount.id(), contractAccount.id(), 1));
        BEAST_EXPECT(putData(env, bob.id(), contractAccount.id(), 2));

        json::Value params;
        params[jss::contract_account] = contractAccount.human();
        params[jss::account] = bob.human();
        auto const result = contractInfo(env, params);
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(!result.isMember(jss::source_code_uri));
        // Data keys are rendered in hex.
        auto const count = strHex(std::string{"count"});
        BEAST_EXPECT(result[jss::contract_data][count] == 1);
        BEAST_EXPECT(result[jss::user_data][count] == 2);
    }

public:
    void
    run() override
    {
        testErrors();
        testContract();
        testData();
    }
};

BEAST_DEFINE_TESTSUITE(ContractInfo, rpc, xrpl);

}  // namespace xrpl::test
