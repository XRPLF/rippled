#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/contract.h>
#include <test/jtx/fee.h>
#include <test/jtx/token.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace xrpl::test {

class ContractInfo_test : public beast::unit_test::Suite
{
    // Creates a contract owned by `owner` from `codeHex` and returns its pseudo-account.
    static jtx::Account
    createContract(jtx::Env& env, jtx::Account const& owner, std::string const& codeHex)
    {
        using namespace jtx;

        auto const seq = env.seq(owner);
        env(contract::create(owner, codeHex),
            contract::add_function("base", {{0, "uint8", "UINT8"}}),
            token::Uri("https://example.com/contract"),
            Fee(XRP(200)));
        env.close();

        auto const code = strUnHex(codeHex);
        auto const hash = sha512HalfS(Slice(code->data(), code->size()));
        auto const sle = env.le(keylet::contract(hash, owner.id(), seq));
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
            params[jss::function] = 1;
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
        auto const code = strUnHex(codeHex);
        BEAST_EXPECT(
            result[jss::hash] == to_string(sha512HalfS(Slice(code->data(), code->size()))));

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

public:
    void
    run() override
    {
        testErrors();
        testContract();
    }
};

BEAST_DEFINE_TESTSUITE(ContractInfo, rpc, xrpl);

}  // namespace xrpl::test
