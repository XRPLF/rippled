#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Env_ss.h>
#include <test/jtx/JTx.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/account_txn_id.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/batch.h>
#include <test/jtx/check.h>
#include <test/jtx/contract.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delegate.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/deposit.h>
#include <test/jtx/did.h>
#include <test/jtx/directory.h>
#include <test/jtx/domain.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/invoice_id.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/last_ledger_sequence.h>
#include <test/jtx/ledgerStateFix.h>
#include <test/jtx/memo.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/prop.h>
#include <test/jtx/quality.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/require.h>
#include <test/jtx/requires.h>
#include <test/jtx/rpc.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tag.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/utility.h>

#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {

class Contract_test : public beast::unit_test::Suite
{
    struct TestLedgerData
    {
        int index;
        std::string txType;
        std::string result;
    };

    json::Value
    getLastLedger(jtx::Env& env)
    {
        json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        return env.rpc("json", "ledger", to_string(params));
    }

    json::Value
    getTxByIndex(json::Value const& jrr, int const index)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::metaData][sfTransactionIndex.jsonName] == index)
                return txn;
        }
        return {};
    }

    void
    validateClosedLedger(jtx::Env& env, std::vector<TestLedgerData> const& ledgerResults)
    {
        auto const jrr = getLastLedger(env);
        auto const transactions = jrr[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.size() == ledgerResults.size());
        for (TestLedgerData const& ledgerResult : ledgerResults)
        {
            auto const txn = getTxByIndex(jrr, ledgerResult.index);
            BEAST_EXPECT(txn.isMember(jss::metaData));
            json::Value const meta = txn[jss::metaData];
            BEAST_EXPECT(txn[sfTransactionType.jsonName] == ledgerResult.txType);
            BEAST_EXPECT(meta[sfTransactionResult.jsonName] == ledgerResult.result);
        }
    }

    static std::pair<uint256, std::shared_ptr<SLE const>>
    contractSourceKeyAndSle(ReadView const& view, uint256 const& contractHash)
    {
        auto const k = keylet::contractSource(contractHash);
        return {k.key, view.read(k)};
    }

    static std::pair<uint256, std::shared_ptr<SLE const>>
    contractKeyAndSle(
        ReadView const& view,
        uint256 const& contractHash,
        AccountID const& owner,
        std::uint32_t const& seq)
    {
        auto const k = keylet::contract(contractHash, owner, seq);
        return {k.key, view.read(k)};
    }

    json::Value
    getContractCreateTx(json::Value const& jrr)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::TransactionType] == jss::ContractCreate)
                return txn;
        }
        return {};
    }

    uint256
    getContractHash(Blob const& wasmBytes)
    {
        return xrpl::sha512HalfS(xrpl::Slice(wasmBytes.data(), wasmBytes.size()));
    }

    void
    validateFunctions(std::shared_ptr<SLE const> const& sle, json::Value const& functions)
    {
        auto const stored = sle->getFieldArray(sfFunctions);
        BEAST_EXPECT(stored.size() == functions.size());
        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            auto const sIPV = stored[i].getJson(JsonOptions::Values::None);
            auto const& eIPV = functions[i]["Function"];

            // Compare function name.
            BEAST_EXPECT(sIPV.isMember("FunctionName"));
            BEAST_EXPECT(eIPV.isMember("FunctionName"));
            BEAST_EXPECT(sIPV["FunctionName"].asString() == eIPV["FunctionName"].asString());

            // Compare parameters if present.
            if (eIPV.isMember("Parameters"))
            {
                BEAST_EXPECT(sIPV.isMember("Parameters"));
                BEAST_EXPECT(sIPV["Parameters"].isArray());
                BEAST_EXPECT(eIPV["Parameters"].isArray());
                BEAST_EXPECT(sIPV["Parameters"].size() == eIPV["Parameters"].size());

                for (std::size_t j = 0; j < sIPV["Parameters"].size(); ++j)
                {
                    auto const& sParam = sIPV["Parameters"][j];
                    auto const& eParam = eIPV["Parameters"][j]["Parameter"];

                    // Compare ParameterFlag if present.
                    if (sParam.isMember("ParameterFlag"))
                    {
                        BEAST_EXPECT(eParam.isMember("ParameterFlag"));
                        BEAST_EXPECT(
                            sParam["ParameterFlag"].asUInt() == eParam["ParameterFlag"].asUInt());
                    }

                    // Compare ParameterName if present.
                    if (sParam.isMember("ParameterName"))
                    {
                        BEAST_EXPECT(eParam.isMember("ParameterName"));
                        BEAST_EXPECT(
                            sParam["ParameterName"].asString() ==
                            eParam["ParameterName"].asString());
                    }

                    // Compare ParameterType if present.
                    if (sParam.isMember("ParameterType"))
                    {
                        BEAST_EXPECT(eParam.isMember("ParameterType"));
                        BEAST_EXPECT(
                            sParam["ParameterType"]["type"].asString() ==
                            eParam["ParameterType"]["type"].asString());
                    }
                }
            }
        }
    }

    void
    validateInstanceParams(
        std::shared_ptr<SLE const> const& sle,
        json::Value const& instanceParamValues)
    {
        // Convert stored SLE array to JSON and compare against expected JSON.
        auto const stored = sle->getFieldArray(sfInstanceParameterValues);
        BEAST_EXPECT(stored.size() == instanceParamValues.size());

        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            // Convert the STObject entry to JSON for easy comparison.
            auto const sIPV = stored[i].getJson(JsonOptions::Values::None);
            auto const& eIPV = instanceParamValues[i]["InstanceParameterValue"];

            // Compare flag if present.
            BEAST_EXPECT(sIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(eIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(sIPV["ParameterFlag"].asUInt() == eIPV["ParameterFlag"].asUInt());

            // Compare ParameterValue contents (name/type/value) when present.
            BEAST_EXPECT(sIPV.isMember("ParameterValue"));
            BEAST_EXPECT(eIPV.isMember("ParameterValue"));
            auto const& sPV = sIPV["ParameterValue"];
            auto const& ePV = eIPV["ParameterValue"];

            if (ePV.isMember("name"))
                BEAST_EXPECT(
                    sPV.isMember("name") && sPV["name"].asString() == ePV["name"].asString());

            if (ePV.isMember("type"))
                BEAST_EXPECT(
                    sPV.isMember("type") && sPV["type"].asString() == ePV["type"].asString());

            if (ePV.isMember("value"))
            {
                // value can be number, string, or object; compare generically
                BEAST_EXPECT(sPV.isMember("value"));
                BEAST_EXPECT(sPV["value"] == ePV["value"]);
            }
        }
    }

    void
    validateInstanceParamValues(
        std::shared_ptr<SLE const> const& sle,
        json::Value const& instanceParamValues)
    {
        // Convert stored SLE array to JSON and compare against expected JSON.
        auto const stored = sle->getFieldArray(sfInstanceParameterValues);
        BEAST_EXPECT(stored.size() == instanceParamValues.size());

        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            // Convert the STObject entry to JSON for easy comparison.
            auto const sIPV = stored[i].getJson(JsonOptions::Values::None);
            auto const& eIPV = instanceParamValues[i]["InstanceParameterValue"];

            // Compare flag if present.
            BEAST_EXPECT(sIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(eIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(sIPV["ParameterFlag"].asUInt() == eIPV["ParameterFlag"].asUInt());

            // Compare ParameterValue contents (name/type/value) when present.
            BEAST_EXPECT(sIPV.isMember("ParameterValue"));
            BEAST_EXPECT(eIPV.isMember("ParameterValue"));
            auto const& sPV = sIPV["ParameterValue"];
            auto const& ePV = eIPV["ParameterValue"];

            if (ePV.isMember("type"))
                BEAST_EXPECT(
                    sPV.isMember("type") && sPV["type"].asString() == ePV["type"].asString());

            if (ePV.isMember("value"))
            {
                // value can be number, string, or object; compare generically
                BEAST_EXPECT(sPV.isMember("value"));
                BEAST_EXPECT(sPV["value"] == ePV["value"]);
            }
        }
    }

    void
    validateContract(
        jtx::Env& env,
        Keylet const& k,
        AccountID const& contractAccount,
        AccountID const& owner,
        std::uint32_t const& flags,
        std::uint32_t const& seq,
        uint256 const& contractHash,
        std::optional<json::Value> const& instanceParamValues = std::nullopt,
        std::optional<std::string> const& uri = std::nullopt)
    {
        auto const sle = env.current()->read(k);
        if (!sle)
        {
            fail();
            return;
        }
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getAccountID(sfContractAccount) == contractAccount);
        BEAST_EXPECT(sle->getAccountID(sfOwner) == owner);
        BEAST_EXPECT(sle->getFieldU32(sfFlags) == flags);
        BEAST_EXPECT(sle->getFieldU32(sfSequence) == seq);
        BEAST_EXPECT(sle->getFieldH256(sfContractHash) == contractHash);
        // if (instanceParamValues)
        //     validateInstanceParamValues(sle, *instanceParamValues);
        // if (uri)
        // {
        //     std::cout << "URI: " << *uri << std::endl;
        //     BEAST_EXPECT(sle->getFieldVL(sfURI) == strUnHex(*uri));
        // }
    }

    void
    validateContractSource(
        jtx::Env& env,
        Blob const& wasmBytes,
        uint256 const& contractHash,
        std::uint64_t const& referenceCount,
        json::Value const& functions,
        std::optional<json::Value> const& instanceParams = std::nullopt)
    {
        auto const [id, sle] = contractSourceKeyAndSle(*env.current(), contractHash);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldVL(sfContractCode) == wasmBytes);
        BEAST_EXPECT(sle->getFieldH256(sfContractHash) == contractHash);
        BEAST_EXPECT(sle->getFieldU64(sfReferenceCount) == referenceCount);
        validateFunctions(sle, functions);
    }

    template <typename... Args>
    std::tuple<jtx::Account, uint256, json::Value>
    setContract(jtx::Env& env, TER const& result, Args&&... args)
    {
        auto jt = env.jt(std::forward<Args>(args)...);
        env(jt, jtx::Ter(result));
        env.close();

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // if (jt.jv.isMember(sfContractHash.jsonName))
        // {
        //     auto const accountID =
        //     parseBase58<AccountID>(jt.jv[sfContractAccount].asString());
        //     jtx::Account const contractAccount{
        //         "Contract pseudo-account",
        //         *accountID};
        //     return std::make_pair(contractAccount,
        //     uint256(jt.jv[sfContractHash]));
        // }

        auto const wasmBytes = strUnHex(jt.jv[sfContractCode.jsonName].asString());
        // std::cout << "WASM Size: " << wasmBytes->size() << std::endl;
        if (!wasmBytes || wasmBytes->empty())
            return std::make_tuple(jtx::Account{"invalid"}, uint256{}, jt.jv);
        uint256 const contractHash =
            xrpl::sha512HalfS(xrpl::Slice(wasmBytes->data(), wasmBytes->size()));
        auto const accountID = parseBase58<AccountID>(jt.jv[sfAccount].asString());
        auto const [contractKey, sle] = contractKeyAndSle(
            *env.current(), contractHash, *accountID, jt.jv[sfSequence.jsonName].asUInt());
        if (!sle)
            return std::make_tuple(jtx::Account{"invalid"}, contractHash, jt.jv);
        jtx::Account const contractAccount{
            "Contract pseudo-account", sle->getAccountID(sfContractAccount)};
        return std::make_tuple(contractAccount, contractHash, jt.jv);
    }

    std::string const BaseContractWasm =
        "0061736D01000000010E0260057F7F7F7F7F017F6000017F02120108686F73745F"
        "6C696205747261636500000302010105030100110619037F01418080C0000B7F00"
        "419E80C0000B7F0041A080C0000B072C04066D656D6F7279020004626173650001"
        "0A5F5F646174615F656E6403010B5F5F686561705F6261736503020A6C016A0101"
        "7F23808080800041206B2200248080808000200041186A410028009080C0800036"
        "0200200041106A410029008880C080003703002000410029008080C08000370308"
        "419480C08000410A200041086A411441011080808080001A200041206A24808080"
        "800041000B0B270100418080C0000B1EAE123A8556F3CF91154711376AFB0F894F"
        "832B3D20204163636F756E743A";

    std::string const Base2ContractWasm =
        "0061736D01000000010E0260057F7F7F7F7F017F6000017F02120108686F73745F6C69"
        "6205747261636500000302010105030100110619037F01418080C0000B7F0041A380C0"
        "000B7F0041B080C0000B072C04066D656D6F72790200046261736500010A5F5F646174"
        "615F656E6403010B5F5F686561705F6261736503020A1B011900418080C08000412341"
        "00410041001080808080001A41000B0B2C0100418080C0000B23242424242420535441"
        "5254494E47204241534520455845435554494F4E202424242424";

    void
    testCreatePreflight(FeatureBitset features)
    {
        testcase("create preflight");

        using namespace jtx;

        // temDISABLED: Feature not enabled
        {
            test::jtx::Env env{*this, features - featureSmartContract};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm), Ter(temDISABLED));
        }

        // temINVALID_FLAG: tfContractMask is not allowed.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                Txflags(tfBurnable),
                Ter(temINVALID_FLAG));
        }

        // temMALFORMED: Neither ContractCode nor ContractHash present
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            // Missing both ContractCode and ContractHash

            env(jv, Ter(temMALFORMED));
        }

        // temMALFORMED: Both ContractCode and ContractHash present
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfContractHash.jsonName] =
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25";

            env(jv, Ter(temMALFORMED));
        }

        // temARRAY_EMPTY: ContractCode present but Functions missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            // Missing Functions array

            env(jv, Ter(temARRAY_EMPTY));
        }

        // temARRAY_EMPTY: ContractCode present but Functions missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = json::ValueType::Array;  // Empty array

            env(jv, Ter(temARRAY_EMPTY));
        }

        // temARRAY_TOO_LARGE: Functions array too large
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function("func1", {}),
                contract::add_function("func2", {}),
                contract::add_function("func3", {}),
                contract::add_function("func4", {}),
                contract::add_function("func5", {}),
                contract::add_function("func6", {}),
                contract::add_function("func7", {}),
                contract::add_function("func8", {}),
                contract::add_function("func9", {}),
                contract::add_function("func10", {}),
                contract::add_function("func11", {}),
                contract::add_function("func12", {}),
                contract::add_function("func13", {}),
                Ter(temARRAY_TOO_LARGE));
        }

        // temREDUNDANT: Duplicate function name
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function("test", {}),
                contract::add_function("test", {}),  // Duplicate
                Ter(temREDUNDANT));
        }

        // temARRAY_TOO_LARGE: Function Parameters array too large
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function(
                    "test",
                    {
                        {0, "param1", "UINT8"},  {0, "param2", "UINT8"},
                        {0, "param3", "UINT8"},  {0, "param4", "UINT8"},
                        {0, "param5", "UINT8"},  {0, "param6", "UINT8"},
                        {0, "param7", "UINT8"},  {0, "param8", "UINT8"},
                        {0, "param9", "UINT8"},  {0, "param10", "UINT8"},
                        {0, "param11", "UINT8"}, {0, "param12", "UINT8"},
                        {0, "param13", "UINT8"}, {0, "param14", "UINT8"},
                        {0, "param15", "UINT8"}, {0, "param16", "UINT8"},
                        {0, "param17", "UINT8"}, {0, "param18", "UINT8"},
                        {0, "param19", "UINT8"}, {0, "param20", "UINT8"},
                        {0, "param21", "UINT8"}, {0, "param22", "UINT8"},
                        {0, "param23", "UINT8"}, {0, "param24", "UINT8"},
                        {0, "param25", "UINT8"}, {0, "param26", "UINT8"},
                        {0, "param27", "UINT8"}, {0, "param28", "UINT8"},
                        {0, "param29", "UINT8"}, {0, "param30", "UINT8"},
                        {0, "param31", "UINT8"}, {0, "param32", "UINT8"},
                        {0, "param33", "UINT8"},  // 33rd parameter
                    }),
                Ter(temARRAY_TOO_LARGE));
        }

        // temMALFORMED: Function Parameter is missing flag
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = json::ValueType::Array;

            json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] = strHex(std::string("test"));
            func[sfFunction.jsonName][sfParameters.jsonName] = json::ValueType::Array;

            json::Value param;
            param[sfParameter.jsonName][sfParameterType.jsonName]["type"] = "UINT8";
            func[sfFunction.jsonName][sfParameters.jsonName].append(param);

            jv[sfFunctions.jsonName].append(func);
            env(jv, Ter(temMALFORMED));
        }

        // temMALFORMED: Function Parameter is missing type
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = json::ValueType::Array;

            json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] = strHex(std::string("test"));
            func[sfFunction.jsonName][sfParameters.jsonName] = json::ValueType::Array;

            json::Value param;
            param[sfParameter.jsonName][sfParameterFlag.jsonName] = 0;
            // Missing sfParameterType
            func[sfFunction.jsonName][sfParameters.jsonName].append(param);

            jv[sfFunctions.jsonName].append(func);
            env(jv, Ter(temMALFORMED));
        }

        // temINVALID_FLAG: Invalid parameter flag in Function.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function("test", {{0xFF000000, "param", "UINT8"}}),  // Invalid flag
                Ter(temINVALID_FLAG));
        }

        // temARRAY_EMPTY: InstanceParameters empty array
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = json::ValueType::Array;
            json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] = strHex(std::string("test"));
            jv[sfFunctions.jsonName].append(func);
            jv[sfInstanceParameters.jsonName] = json::ValueType::Array;  // Empty array

            env(jv, Ter(temARRAY_EMPTY));
        }

        // temARRAY_TOO_LARGE: InstanceParameters array is too large
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function("test", {}),
                contract::add_instance_param(0, "param1", "UINT8", 1),
                contract::add_instance_param(0, "param2", "UINT8", 2),
                contract::add_instance_param(0, "param3", "UINT8", 3),
                contract::add_instance_param(0, "param4", "UINT8", 4),
                contract::add_instance_param(0, "param5", "UINT8", 5),
                contract::add_instance_param(0, "param6", "UINT8", 6),
                contract::add_instance_param(0, "param7", "UINT8", 7),
                contract::add_instance_param(0, "param8", "UINT8", 8),
                contract::add_instance_param(0, "param9", "UINT8", 9),
                contract::add_instance_param(0, "param10", "UINT8", 10),
                contract::add_instance_param(0, "param11", "UINT8", 11),
                contract::add_instance_param(0, "param12", "UINT8", 12),
                contract::add_instance_param(0, "param13", "UINT8", 13),
                contract::add_instance_param(0, "param14", "UINT8", 14),
                contract::add_instance_param(0, "param15", "UINT8", 15),
                contract::add_instance_param(0, "param16", "UINT8", 16),
                contract::add_instance_param(0, "param17", "UINT8", 17),
                contract::add_instance_param(0, "param18", "UINT8", 18),
                contract::add_instance_param(0, "param19", "UINT8", 19),
                contract::add_instance_param(0, "param20", "UINT8", 20),
                contract::add_instance_param(0, "param21", "UINT8", 21),
                contract::add_instance_param(0, "param22", "UINT8", 22),
                contract::add_instance_param(0, "param23", "UINT8", 23),
                contract::add_instance_param(0, "param24", "UINT8", 24),
                contract::add_instance_param(0, "param25", "UINT8", 25),
                contract::add_instance_param(0, "param26", "UINT8", 26),
                contract::add_instance_param(0, "param27", "UINT8", 27),
                contract::add_instance_param(0, "param28", "UINT8", 28),
                contract::add_instance_param(0, "param29", "UINT8", 29),
                contract::add_instance_param(0, "param30", "UINT8", 30),
                contract::add_instance_param(0, "param31", "UINT8", 31),
                contract::add_instance_param(0, "param32", "UINT8", 32),
                contract::add_instance_param(0, "param33", "UINT8", 33),
                Ter(temARRAY_TOO_LARGE));
        }

        // temARRAY_EMPTY: InstanceParameterValues is missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = json::ValueType::Array;
            json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] = strHex(std::string("test"));
            jv[sfFunctions.jsonName].append(func);
            jv[sfInstanceParameterValues.jsonName] = json::ValueType::Array;  // Empty array

            env(jv, Ter(temARRAY_EMPTY));
        }

        // // Test 18: InstanceParameterValues array is too large.
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = json::ValueType::Array;
        //     json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     json::ValueType::Array; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = json::ValueType::Array;

        //     // Add more than maxContractParams
        //     for (int i = 0; i < 257; ++i)
        //     {
        //         json::Value param;
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterFlag.jsonName] = 0;
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterValue.jsonName]["type"] = "UINT8";
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterValue.jsonName]["value"] = i;
        //         jv[sfInstanceParameterValues.jsonName].append(param);
        //     }

        //     env(jv, Ter(temARRAY_TOO_LARGE));
        // }

        // // Test 19: InstanceParameterValue missing flag
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = json::ValueType::Array;
        //     json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     json::ValueType::Array; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = json::ValueType::Array;

        //     json::Value param;
        //     // Missing sfParameterFlag
        //     param[sfInstanceParameterValue.jsonName][sfParameterValue.jsonName]
        //          ["type"] = "UINT8";
        //     param[sfInstanceParameterValue.jsonName][sfParameterValue.jsonName]
        //          ["value"] = 1;
        //     jv[sfInstanceParameterValues.jsonName].append(param);

        //     env(jv, Ter(temMALFORMED));
        // }

        // // Test 20: InstanceParameterValue missing value
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = json::ValueType::Array;
        //     json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     json::ValueType::Array; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = json::ValueType::Array;

        //     json::Value param;
        //     param[sfInstanceParameterValue.jsonName][sfParameterFlag.jsonName]
        //     =
        //         0;
        //     // Missing sfParameterValue
        //     jv[sfInstanceParameterValues.jsonName].append(param);

        //     env(jv, Ter(temMALFORMED));
        // }

        // // Test 21: InstanceParameterValue invalid flag
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     env(contract::create(alice, BaseContractWasm),
        //         contract::add_function("test", {}),
        //         contract::add_instance_param(
        //             0xFF000000, "param", "UINT8", 1),  // Invalid flag
        //         Ter(temINVALID_FLAG));
        // }

        // // Test 22: Success - ContractCode with Functions
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     env(contract::create(alice, BaseContractWasm),
        //         contract::add_function("base", {}),
        //         Fee(XRP(200)),
        //         Ter(tesSUCCESS));
        // }

        // // Test 23: Success - ContractCode with Functions and parameters
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     env(contract::create(alice, BaseContractWasm),
        //         contract::add_function(
        //             "base", {{0, "param1", "UINT8"}, {0, "param2",
        //             "UINT32"}}),
        //         Fee(XRP(200)),
        //         Ter(tesSUCCESS));
        // }

        // // Test 24: Success - with InstanceParameters and
        // // InstanceParameterValues
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     env(contract::create(alice, BaseContractWasm),
        //         contract::add_instance_param(0, "uint8", "UINT8", 1),
        //         contract::add_instance_param(0, "uint32", "UINT32", 100),
        //         contract::add_function("base", {}),
        //         Fee(XRP(200)),
        //         Ter(tesSUCCESS));
        // }
    }

    void
    testCreatePreclaim(FeatureBitset features)
    {
        testcase("create preclaim");

        using namespace jtx;

        // temMALFORMED: ContractHash provided but no corresponding
        // ContractSource exists
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(
                    alice,
                    uint256{"D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F"
                            "1ACA4491902C25"}),
                Fee(XRP(200)),
                Ter(temMALFORMED));
        }

        // temMALFORMED: ContractCode provided is empty
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, ""),  // Empty code
                contract::add_function("test", {}),
                Fee(XRP(200)),
                Ter(temMALFORMED));
        }

        // tesSUCCESS: ContractCode provided, ContractSource doesn't exist yet
        // (first create)
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));

            // TODO: Validate
        }

        // tesSUCCESS: ContractCode provided, ContractSource already exists
        // (install)
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // First create
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Second create with same code (install)
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 2),  // Different value
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));

            // TODO: Validate
        }

        // tesSUCCESS: ContractHash provided with valid ContractSource
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // First create to establish ContractSource
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Get the hash of the contract
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // Install using ContractHash
            env(contract::create(alice, contractHash),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                Fee(XRP(200)),
                Ter(tesSUCCESS));

            // TODO: Validate
        }

        // temMALFORMED: Install with InstanceParameterValues that don't
        // match
        // ContractSource
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // First create with specific InstanceParameters
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_instance_param(0, "uint32", "UINT32", 100),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Get the hash
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // Try to install with mismatched InstanceParameterValues
            // Only providing one parameter when ContractSource expects two
            env(contract::create(alice, contractHash),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                Fee(XRP(200)),
                Ter(temMALFORMED));
        }

        // temMALFORMED: ContractHash provided but ContractSource doesn't
        // exist
        // (should fail in preclaim)
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // Use Base2ContractWasm hash which hasn't been created yet
            auto const wasmBytes = strUnHex(Base2ContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            env(contract::create(alice, contractHash), Fee(XRP(200)), Ter(temMALFORMED));
        }

        // tesSUCCESS: ContractCode with InstanceParameters for first
        // creation
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, Base2ContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 255),
                contract::add_instance_param(tfSendAmount, "amount", "AMOUNT", XRP(100)),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
        }

        // tesSUCCESS: Multiple installs of same contract
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10'000), alice, bob);
            env.close();

            // Alice creates first instance
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Bob installs same contract
            env(contract::create(bob, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Alice installs another instance
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 3),
                contract::add_function("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
        }
    }

    void
    testCreateDoApply(FeatureBitset features)
    {
        testcase("create doApply");

        using namespace jtx;

        //----------------------------------------------------------------------
        // doApply.ContractCode.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // auto const seq = env.current()->seq();
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                token::Uri("https://example.com/contract"),
                Fee(XRP(200)));

            // validate contract
            // validateContract(
            //     env,
            //     contractAccount.id(),
            //     alice.id(),
            //     0,
            //     seq,
            //     contractHash,
            //     jv[sfInstanceParameterValues],
            //     to_string(jv[sfURI]));

            // validate contract source
            // validateContractSource(
            //     env, *wasmBytes, contractHash, 1, jv[sfFunctions]);
        }

        //----------------------------------------------------------------------
        // doApply.ContractHash.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // auto const wasmBytes = strUnHex(BaseContractWasm);
            // uint256 const contractHash = getContractHash(*wasmBytes);

            // Create Contract.
            {
                // auto const seq = env.current()->seq();
                auto const [contractAccount, contractHash, jv] = setContract(
                    env,
                    tesSUCCESS,
                    contract::create(alice, BaseContractWasm),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}),
                    Fee(XRP(200)));

                // validate contract
                // validateContract(
                //     env,
                //     contractAccount.id(),
                //     alice.id(),
                //     0,
                //     seq,
                //     contractHash,
                //     jv[sfInstanceParameterValues]);

                // validate contract source
                // validateContractSource(
                //     env, *wasmBytes, contractHash, 1, jv[sfFunctions]);
            }

            // Install Contract.
            {
                // auto const seq = env.current()->seq();
                auto const [contractAccount, contractHash, jv] = setContract(
                    env,
                    tesSUCCESS,
                    contract::create(alice, BaseContractWasm),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}),
                    Fee(XRP(200)));

                // validate contract
                // validateContract(
                //     env,
                //     contractAccount.id(),
                //     alice.id(),
                //     0,
                //     seq,
                //     contractHash,
                //     jv[sfInstanceParameterValues]);

                // validate contract source
                // validateContractSource(
                //     env, *wasmBytes, contractHash, 2, jv[sfFunctions]);
            }
        }
    }

    void
    testModifyDoApply(FeatureBitset features)
    {
        testcase("modify doApply");

        using namespace jtx;

        //----------------------------------------------------------------------
        // doApply.ContractCode.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            jtx::Account const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // Create initial contract
            auto const seq = env.seq(alice);
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto jt = env.jt(
                contract::modify(alice, contractAccount, Base2ContractWasm),
                contract::add_instance_param(0, "uint16", "UINT16", 1),
                contract::add_function("base", {{0, "uint16", "UINT16"}}),
                Fee(XRP(200)));
            env(jt, Ter(tesSUCCESS));
            env.close();

            // old contract source is deleted
            auto const [sourceKey, sourceSle] =
                contractSourceKeyAndSle(*env.current(), contractHash);
            BEAST_EXPECT(!sourceSle);

            // new contract source exists
            auto const wasmBytes = strUnHex(Base2ContractWasm);
            uint256 const newContractHash =
                xrpl::sha512HalfS(xrpl::Slice(wasmBytes->data(), wasmBytes->size()));
            auto const [contractKey, contractSle] =
                contractSourceKeyAndSle(*env.current(), newContractHash);
            BEAST_EXPECT(contractSle);

            // validate modified contract
            auto const k = keylet::contract(contractHash, alice, seq);
            validateContract(
                env,
                k,
                contractAccount.id(),
                alice.id(),
                0,
                seq,
                newContractHash,
                jt.jv[sfInstanceParameterValues]);
        }

        //----------------------------------------------------------------------
        // doApply.ContractHash.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            jtx::Account const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // Create initial contract
            // auto const seq = env.seq(alice);
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            auto const [contractAccount2, contractHash2, jv2] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, Base2ContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto jt = env.jt(
                contract::modify(alice, contractAccount, contractHash2),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));
            env(jt, Ter(tesSUCCESS));
            env.close();

            // old contract source is deleted
            auto const [oldSourceKey, oldSourceSle] =
                contractSourceKeyAndSle(*env.current(), contractHash);
            BEAST_EXPECT(!oldSourceSle);

            // new contract source exists
            auto const [sourceKey, sourceSle] =
                contractSourceKeyAndSle(*env.current(), contractHash2);
            BEAST_EXPECT(sourceSle);
            BEAST_EXPECT(sourceSle->getFieldU64(sfReferenceCount) == 2);

            // // validate modified contract
            // auto const k = keylet::contract(contractHash, seq);
            // validateContract(
            //     env,
            //     k,
            //     contractAccount.id(),
            //     alice.id(),
            //     0,
            //     seq,
            //     newContractHash,
            //     jt.jv[sfInstanceParameterValues]);
        }

        //----------------------------------------------------------------------
        // doApply.ContractOwner.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            jtx::Account const alice = Account{"alice"};
            jtx::Account const bob = Account{"bob"};
            env.fund(XRP(10'000), alice, bob);
            env.close();

            // Create initial contract
            auto const seq = env.seq(alice);
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto jt = env.jt(contract::modify(alice, contractAccount, bob), Fee(XRP(200)));
            env(jt, Ter(tesSUCCESS));
            env.close();

            // validate modified contract
            auto const k = keylet::contract(contractHash, alice, seq);
            validateContract(
                env,
                k,
                contractAccount.id(),
                bob.id(),
                0,
                seq,
                contractHash,
                jv[sfInstanceParameterValues]);
        }
    }

    void
    testDeleteDoApply(FeatureBitset features)
    {
        testcase("delete doApply");

        using namespace jtx;

        //-------------------------------------------------------------------------
        // doApply.tesSUCCESS - Single Reference
        {
            test::jtx::Env env{*this, features};

            jtx::Account const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            auto const seq = env.current()->seq();
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            env(contract::del(alice, contractAccount), Ter(tesSUCCESS));
            env.close();

            // Pseudo Account is deleted
            auto const pseudoAccountKey = keylet::account(contractAccount);
            BEAST_EXPECT(!env.le(pseudoAccountKey));

            // Contract instance is deleted
            auto const wasmBytes = strUnHex(BaseContractWasm);
            auto const contractKey = keylet::contract(contractHash, alice, seq);
            BEAST_EXPECT(!env.le(contractKey));

            // ContractSource is deleted - because it had a single reference
            auto const contractSourceKey = keylet::contractSource(contractHash);
            BEAST_EXPECT(!env.le(contractSourceKey));
        }

        // doApply.tesSUCCESS - Multiple Reference
        {
            test::jtx::Env env{*this, features};

            jtx::Account const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            auto const seq = env.current()->seq();
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            auto const seq2 = env.current()->seq();
            auto const [contractAccount2, contractHash2, jv2] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            env(contract::del(alice, contractAccount), Ter(tesSUCCESS));
            env.close();

            // Pseudo Account is deleted
            auto const pseudoAccountKey = keylet::account(contractAccount);
            BEAST_EXPECT(!env.le(pseudoAccountKey));

            // Contract instance is deleted
            auto const wasmBytes = strUnHex(BaseContractWasm);
            auto const contractKey = keylet::contract(contractHash, alice, seq);
            BEAST_EXPECT(!env.le(contractKey));

            // Ensure ContractSource still exists
            auto const contractSourceKey = keylet::contractSource(contractHash);
            BEAST_EXPECT(env.le(contractSourceKey));
            BEAST_EXPECT(env.le(contractSourceKey)->getFieldU64(sfReferenceCount) == 1);

            // Pseudo Account of second instance still exists
            auto const pseudoAccountKey2 = keylet::account(contractAccount2);
            BEAST_EXPECT(env.le(pseudoAccountKey2));

            // Ensure second contract instance still exists
            auto const contractKey2 = keylet::contract(contractHash2, alice, seq2);
            BEAST_EXPECT(env.le(contractKey2));
        }
    }

    void
    testUserDeletePreflight(FeatureBitset features)
    {
        testcase("user delete preflight");

        using namespace jtx;

        // temDISABLED: Feature not enabled
        {
            test::jtx::Env env{*this, features - featureSmartContract};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::userDelete(alice, BaseContractWasm), Ter(temDISABLED));
        }
    }

    std::string
    loadContractWasmStr(std::string const& contract_name = "")
    {
        std::string const& dir = "e2e-tests";
        std::string const name = "/Users/darkmatter/projects/ledger-works/xrpl-wasm-std/" + dir +
            "/" + contract_name + "/target/wasm32v1-none/release/" + contract_name + ".wasm";
        if (!boost::filesystem::exists(name))
        {
            std::cout << "File does not exist: " << name << "\n";
            return "";
        }

        std::ifstream file(name, std::ios::binary);

        if (!file)
        {
            std::cout << "Failed to open file: " << name << "\n";
            return "";
        }

        // Read the file into a vector
        std::vector<char> const buffer(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Check if the buffer is empty
        if (buffer.empty())
        {
            std::cout << "File is empty or could not be read properly.\n";
            return "";
        }

        return strHex(buffer);
    }

    void
    testContractDataSimple(FeatureBitset features)
    {
        testcase("contract data simple");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const contractDataWasmHex = loadContractWasmStr("contract_data");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractDataWasmHex),
            contract::add_instance_param(tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("object_simple_create", {}),
            contract::add_function("object_simple_update", {}),
            Fee(XRP(2000)));

        env(contract::call(alice, contractAccount, "object_simple_create"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();

        env(contract::call(alice, contractAccount, "object_simple_update"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testContractDataNested(FeatureBitset features)
    {
        testcase("contract data nested");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const contractDataWasmHex = loadContractWasmStr("contract_data");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractDataWasmHex),
            contract::add_instance_param(tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("object_nested_create", {}),
            contract::add_function("object_nested_update", {}),
            Fee(XRP(2000)));

        env(contract::call(alice, contractAccount, "object_nested_create"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();

        env(contract::call(alice, contractAccount, "object_nested_update"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testContractDataArray(FeatureBitset features)
    {
        testcase("contract data array");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const contractDataWasmHex = loadContractWasmStr("contract_data");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractDataWasmHex),
            contract::add_instance_param(tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("object_with_arrays_create", {}),
            contract::add_function("object_with_arrays_update", {}),
            Fee(XRP(2000)));

        env(contract::call(alice, contractAccount, "object_with_arrays_create"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();

        env(contract::call(alice, contractAccount, "object_with_arrays_update"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testContractDataNestedArray(FeatureBitset features)
    {
        testcase("contract data nested array");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const contractDataWasmHex = loadContractWasmStr("contract_data");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractDataWasmHex),
            contract::add_instance_param(tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("object_with_nested_arrays_create", {}),
            contract::add_function("object_with_nested_arrays_update", {}),
            Fee(XRP(2000)));

        env(contract::call(alice, contractAccount, "object_with_nested_arrays_create"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();

        env(contract::call(alice, contractAccount, "object_with_nested_arrays_update"),
            escrow::Gas(1'000'000),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testInstanceParameters(FeatureBitset features)
    {
        testcase("instance parameters");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // Test Instance Parameter (1 of 2)
        // uint8, uint16, uint32, uint64, uint128, uint160, uint192, uint256
        {
            std::string const wasmHex = loadContractWasmStr("instance_params_uint");
            auto const [contractAccount, contractHash, _] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, wasmHex),
                contract::add_instance_param(0, "uint8", "UINT8", 255),
                contract::add_instance_param(0, "uint16", "UINT16", 65535),
                contract::add_instance_param(
                    0, "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
                contract::add_instance_param(0, "uint64", "UINT64", "FFFFFFFFFFFFFFFF"),
                contract::add_instance_param(
                    0, "uint128", "UINT128", "00000000000000000000000000000001"),
                contract::add_instance_param(
                    0, "uint160", "UINT160", "0000000000000000000000000000000000000001"),
                contract::add_instance_param(
                    0, "uint192", "UINT192", "000000000000000000000000000000000000000000000001"),
                contract::add_instance_param(
                    0,
                    "uint256",
                    "UINT256",
                    "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491"
                    "902C"
                    "25"),
                contract::add_function("instance_params_uint", {}),
                Fee(XRP(200)));

            // {
            //     json::Value params;
            //     params[jss::ledger_index] = env.current()->seq() - 1;
            //     params[jss::transactions] = true;
            //     params[jss::expand] = true;
            //     auto const jrr = env.rpc("json", "ledger",
            //     to_string(params)); std::cout << jrr << std::endl;
            // }

            env(contract::call(alice, contractAccount, "instance_params_uint"),
                escrow::Gas(1000000),
                Ter(tesSUCCESS));
            env.close();
        }

        {
            // Test Instance Parameter (2 of 2)
            // vl, account, amount (XRP), amount (IOU), number, currency, issue
            std::string const wasmHex = loadContractWasmStr("instance_params_other");
            auto const [contractAccount, contractHash, _] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, wasmHex),
                contract::add_instance_param(0, "vl", "VL", "DEADBEEF"),
                contract::add_instance_param(0, "account", "ACCOUNT", alice.human()),
                contract::add_instance_param(
                    0, "amountXRP", "AMOUNT", XRP(1).value().getJson(JsonOptions::Values::None)),
                contract::add_instance_param(
                    0, "amountIOU", "AMOUNT", USD(1.2).value().getJson(JsonOptions::Values::None)),
                contract::add_instance_param(0, "number", "NUMBER", "1.2"),
                contract::add_function("instance_params_other", {}),
                Fee(XRP(200)));

            // {
            //     json::Value params;
            //     params[jss::ledger_index] = env.current()->seq() - 1;
            //     params[jss::transactions] = true;
            //     params[jss::expand] = true;
            //     auto const jrr = env.rpc("json", "ledger",
            //     to_string(params)); std::cout << jrr << std::endl;
            // }

            env(contract::call(alice, contractAccount, "instance_params_other"),
                escrow::Gas(1000000),
                Ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testFunctionParameters(FeatureBitset features)
    {
        testcase("function parameters");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const wasmHex = loadContractWasmStr("function_params");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, wasmHex),
            contract::add_instance_param(tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_instance_param(0, "uint8", "UINT8", 1),
            contract::add_function(
                "function_params_uint",
                {
                    {0, "uint8", "UINT8"},
                    {0, "uint16", "UINT16"},
                    {0, "uint32", "UINT32"},
                    {0, "uint64", "UINT64"},
                    {0, "uint128", "UINT128"},
                    {0, "uint160", "UINT160"},
                    {0, "uint192", "UINT192"},
                    {0, "uint256", "UINT256"},
                }),
            contract::add_function(
                "function_params_other",
                {
                    {0, "vl", "VL"},
                    {0, "account", "ACCOUNT"},
                    {0, "amountXRP", "AMOUNT"},
                    {0, "amountIOU", "AMOUNT"},
                    {0, "number", "NUMBER"},
                    //  {0, "issue", "ISSUE"},
                    //  {0, "currency", "CURRENCY"}
                }),
            Fee(XRP(200)));

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "function_params_uint"),
            escrow::Gas(1000000),
            contract::add_param(0, "uint8", "UINT8", 255),
            contract::add_param(0, "uint16", "UINT16", 65535),
            contract::add_param(0, "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
            contract::add_param(0, "uint64", "UINT64", "FFFFFFFFFFFFFFFF"),
            contract::add_param(0, "uint128", "UINT128", "00000000000000000000000000000001"),
            contract::add_param(
                0, "uint160", "UINT160", "0000000000000000000000000000000000000001"),
            contract::add_param(
                0, "uint192", "UINT192", "000000000000000000000000000000000000000000000001"),
            contract::add_param(
                0,
                "uint256",
                "UINT256",
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25"),
            Ter(tesSUCCESS));

        env(contract::call(alice, contractAccount, "function_params_other"),
            escrow::Gas(1000000),
            contract::add_param(0, "vl", "VL", "DEADBEEF"),
            contract::add_param(0, "account", "ACCOUNT", alice.human()),
            contract::add_param(
                0, "amountXRP", "AMOUNT", XRP(1).value().getJson(JsonOptions::Values::None)),
            contract::add_param(
                0, "amountIOU", "AMOUNT", USD(1.2).value().getJson(JsonOptions::Values::None)),
            contract::add_param(0, "number", "NUMBER", "1.2"),
            // contract::add_param(0, "issue", "ISSUE",
            // to_json(USD(1).value().issue())), contract::add_param(0,
            // "currency", "CURRENCY", "USD"),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testEmit(FeatureBitset features)
    {
        testcase("emit");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const emitTxWasmHex = loadContractWasmStr("emit_txn");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, emitTxWasmHex),
            contract::add_instance_param(tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("emit", {}),
            Fee(XRP(200)));

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "emit"), escrow::Gas(1000000), Ter(tesSUCCESS));
        env.close();
    }

    void
    testEvents(FeatureBitset features)
    {
        testcase("events");

        using namespace std::chrono_literals;
        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to contract events stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("contract_events");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::status] == "success");
        }

        std::string const eventsWasmHex = loadContractWasmStr("events");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, eventsWasmHex),
            contract::add_instance_param(tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_function("events", {}),
            Fee(XRP(200)));

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "events"),
            escrow::Gas(1000000),
            Ter(tesSUCCESS));
        env.close();

        {
            // Get contract info
            json::Value params;
            params[jss::contract_account] = contractAccount.human();
            params[jss::account] = alice.human();
            auto const jrr = env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }

        // Check stream update
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            auto const data = jv[jss::data];
            // std::cout << "Event: " << data << std::endl;
            BEAST_EXPECT(data["amount"] == "192");
            BEAST_EXPECT(data["currency"] == "USD");
            BEAST_EXPECT(data["destination"] == "r99mpXDsCPybsGs9XzGJmuxa8gWLTn8aCz");
            BEAST_EXPECT(data["uint128"] == "00000000000000000000000000000000");
            BEAST_EXPECT(data["uint16"] == 16);
            BEAST_EXPECT(data["uint160"] == "0000000000000000000000000000000000000000");
            BEAST_EXPECT(data["uint192"] == "000000000000000000000000000000000000000000000000");
            BEAST_EXPECT(
                data["uint256"] ==
                "00000000000000000000000000000000000000000000000000000000000000"
                "00");
            BEAST_EXPECT(data["uint32"] == 32);
            BEAST_EXPECT(data["uint64"] == "40");
            BEAST_EXPECT(data["uint8"] == 8);
            BEAST_EXPECT(data["vl"] == "48656C6C6F2C20576F726C6421");
            return jv[jss::type] == "contractEvent" && jv[jss::name] == "event1";
        }));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testEasyMode(FeatureBitset features)
    {
        testcase("easy mode");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), alice, bob);
        env.close();

        std::string const wasmHex = loadContractWasmStr("easymode");
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, wasmHex),
            contract::add_instance_param(tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_instance_param(0, "uint8", "UINT8", 1),
            contract::add_function(
                "easymode",
                {
                    {0, "account", "ACCOUNT"},
                    {0, "amount", "AMOUNT"},
                }),
            Fee(XRP(200)));

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "easymode"),
            escrow::Gas(1000000),
            contract::add_param(0, "account", "ACCOUNT", bob.human()),
            contract::add_param(0, "amount", "AMOUNT", XRP(1)),
            Ter(tesSUCCESS));
        env.close();

        // {
        //     json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreatePreflight(features);
        testCreatePreclaim(features);
        testCreateDoApply(features);
        // testModifyPreflight(features);
        // testModifyPreclaim(features);
        testModifyDoApply(features);
        // testDeletePreflight(features);
        // testDeletePreclaim(features);
        testDeleteDoApply(features);
        // testUserDeletePreflight(features);
        // testUserDeletePreclaim(features);
        // testUserDeleteDoApply(features);
        testContractDataSimple(features);
        testContractDataNested(features);
        testContractDataArray(features);
        testContractDataNestedArray(features);
        testInstanceParameters(features);
        testFunctionParameters(features);
        testEmit(features);
        // testEvents(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Contract, app, xrpl);

}  // namespace test
}  // namespace xrpl
