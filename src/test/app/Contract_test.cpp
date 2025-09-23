//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class Contract_test : public beast::unit_test::suite
{
    struct TestLedgerData
    {
        int index;
        std::string txType;
        std::string result;
    };

    Json::Value
    getLastLedger(jtx::Env& env)
    {
        Json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        return env.rpc("json", "ledger", to_string(params));
    }

    Json::Value
    getTxByIndex(Json::Value const& jrr, int const index)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::metaData][sfTransactionIndex.jsonName] == index)
                return txn;
        }
        return {};
    }

    void
    validateClosedLedger(
        jtx::Env& env,
        std::vector<TestLedgerData> const& ledgerResults)
    {
        auto const jrr = getLastLedger(env);
        auto const transactions =
            jrr[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.size() == ledgerResults.size());
        for (TestLedgerData const& ledgerResult : ledgerResults)
        {
            auto const txn = getTxByIndex(jrr, ledgerResult.index);
            BEAST_EXPECT(txn.isMember(jss::metaData));
            Json::Value const meta = txn[jss::metaData];
            BEAST_EXPECT(
                txn[sfTransactionType.jsonName] == ledgerResult.txType);
            BEAST_EXPECT(
                meta[sfTransactionResult.jsonName] == ledgerResult.result);
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
        std::uint32_t const& seq)
    {
        auto const k = keylet::contract(contractHash, seq);
        return {k.key, view.read(k)};
    }

    Json::Value
    getContractCreateTx(Json::Value const& jrr)
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
        return ripple::sha512Half_s(
            ripple::Slice(wasmBytes.data(), wasmBytes.size()));
    }

    void
    validateFunctions(
        std::shared_ptr<SLE const> const& sle,
        Json::Value const& functions)
    {
        auto const stored = sle->getFieldArray(sfFunctions);
        BEAST_EXPECT(stored.size() == functions.size());
        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            auto const sIPV = stored[i].getJson(JsonOptions::none);
            auto const& eIPV = functions[i]["Function"];

            // Compare function name.
            BEAST_EXPECT(sIPV.isMember("FunctionName"));
            BEAST_EXPECT(eIPV.isMember("FunctionName"));
            BEAST_EXPECT(
                sIPV["FunctionName"].asString() ==
                eIPV["FunctionName"].asString());

            // Compare parameters if present.
            if (eIPV.isMember("Parameters"))
            {
                BEAST_EXPECT(sIPV.isMember("Parameters"));
                BEAST_EXPECT(sIPV["Parameters"].isArray());
                BEAST_EXPECT(eIPV["Parameters"].isArray());
                BEAST_EXPECT(
                    sIPV["Parameters"].size() == eIPV["Parameters"].size());

                for (std::size_t j = 0; j < sIPV["Parameters"].size(); ++j)
                {
                    auto const& sParam = sIPV["Parameters"][j];
                    auto const& eParam = eIPV["Parameters"][j]["Parameter"];

                    // Compare ParameterFlag if present.
                    if (sParam.isMember("ParameterFlag"))
                    {
                        BEAST_EXPECT(eParam.isMember("ParameterFlag"));
                        BEAST_EXPECT(
                            sParam["ParameterFlag"].asUInt() ==
                            eParam["ParameterFlag"].asUInt());
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
        Json::Value const& instanceParamValues)
    {
        // Convert stored SLE array to JSON and compare against expected JSON.
        auto const stored = sle->getFieldArray(sfInstanceParameterValues);
        BEAST_EXPECT(stored.size() == instanceParamValues.size());

        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            // Convert the STObject entry to JSON for easy comparison.
            auto const sIPV = stored[i].getJson(JsonOptions::none);
            auto const& eIPV = instanceParamValues[i]["InstanceParameterValue"];

            // Compare flag if present.
            BEAST_EXPECT(sIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(eIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(
                sIPV["ParameterFlag"].asUInt() ==
                eIPV["ParameterFlag"].asUInt());

            // Compare ParameterValue contents (name/type/value) when present.
            BEAST_EXPECT(sIPV.isMember("ParameterValue"));
            BEAST_EXPECT(eIPV.isMember("ParameterValue"));
            auto const& sPV = sIPV["ParameterValue"];
            auto const& ePV = eIPV["ParameterValue"];

            if (ePV.isMember("name"))
                BEAST_EXPECT(
                    sPV.isMember("name") &&
                    sPV["name"].asString() == ePV["name"].asString());

            if (ePV.isMember("type"))
                BEAST_EXPECT(
                    sPV.isMember("type") &&
                    sPV["type"].asString() == ePV["type"].asString());

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
        Json::Value const& instanceParamValues)
    {
        // Convert stored SLE array to JSON and compare against expected JSON.
        auto const stored = sle->getFieldArray(sfInstanceParameterValues);
        BEAST_EXPECT(stored.size() == instanceParamValues.size());

        for (std::size_t i = 0; i < stored.size(); ++i)
        {
            // Convert the STObject entry to JSON for easy comparison.
            auto const sIPV = stored[i].getJson(JsonOptions::none);
            auto const& eIPV = instanceParamValues[i]["InstanceParameterValue"];

            // Compare flag if present.
            BEAST_EXPECT(sIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(eIPV.isMember("ParameterFlag"));
            BEAST_EXPECT(
                sIPV["ParameterFlag"].asUInt() ==
                eIPV["ParameterFlag"].asUInt());

            // Compare ParameterValue contents (name/type/value) when present.
            BEAST_EXPECT(sIPV.isMember("ParameterValue"));
            BEAST_EXPECT(eIPV.isMember("ParameterValue"));
            auto const& sPV = sIPV["ParameterValue"];
            auto const& ePV = eIPV["ParameterValue"];

            if (ePV.isMember("type"))
                BEAST_EXPECT(
                    sPV.isMember("type") &&
                    sPV["type"].asString() == ePV["type"].asString());

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
        AccountID const& contractAccount,
        AccountID const& owner,
        std::uint32_t const& flags,
        std::uint32_t const& seq,
        uint256 const& contractHash,
        std::optional<Json::Value> const& instanceParamValues = std::nullopt,
        std::optional<std::string> const& uri = std::nullopt)
    {
        auto const [id, sle] =
            contractKeyAndSle(*env.current(), contractHash, seq);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getAccountID(sfContractAccount) == contractAccount);
        BEAST_EXPECT(sle->getAccountID(sfOwner) == owner);
        BEAST_EXPECT(sle->getFieldU32(sfFlags) == flags);
        BEAST_EXPECT(sle->getFieldU32(sfSequence) == seq);
        BEAST_EXPECT(sle->getFieldH256(sfContractHash) == contractHash);
        if (instanceParamValues)
            validateInstanceParamValues(sle, *instanceParamValues);
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
        Json::Value const& functions,
        std::optional<Json::Value> const& instanceParams = std::nullopt)
    {
        auto const [id, sle] =
            contractSourceKeyAndSle(*env.current(), contractHash);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldVL(sfContractCode) == wasmBytes);
        BEAST_EXPECT(sle->getFieldH256(sfContractHash) == contractHash);
        BEAST_EXPECT(sle->getFieldU64(sfReferenceCount) == referenceCount);
        validateFunctions(sle, functions);
    }

    template <typename... Args>
    std::tuple<jtx::Account, uint256, Json::Value>
    setContract(jtx::Env& env, TER const& result, Args&&... args)
    {
        auto jt = env.jt(std::forward<Args>(args)...);
        env(jt, jtx::ter(result));
        env.close();

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

        auto const wasmBytes =
            strUnHex(jt.jv[sfContractCode.jsonName].asString());
        std::cout << "WASM Size: " << wasmBytes->size() << std::endl;
        uint256 const contractHash = ripple::sha512Half_s(
            ripple::Slice(wasmBytes->data(), wasmBytes->size()));
        auto const [contractKey, sle] = contractKeyAndSle(
            *env.current(), contractHash, jt.jv[sfSequence.jsonName].asUInt());
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

            env(contract::create(alice, BaseContractWasm), ter(temDISABLED));
        }

        // temINVALID_FLAG: tfContractMask is not allowed.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                txflags(tfBurnable),
                ter(temINVALID_FLAG));
        }

        // temMALFORMED: Neither ContractCode nor ContractHash present
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            // Missing both ContractCode and ContractHash

            env(jv, ter(temMALFORMED));
        }

        // temMALFORMED: Both ContractCode and ContractHash present
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfContractHash.jsonName] =
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25";

            env(jv, ter(temMALFORMED));
        }

        // temARRAY_EMPTY: ContractCode present but Functions missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            // Missing Functions array

            env(jv, ter(temARRAY_EMPTY));
        }

        // temARRAY_EMPTY: ContractCode present but Functions missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;  // Empty array

            env(jv, ter(temARRAY_EMPTY));
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
                ter(temARRAY_TOO_LARGE));
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
                ter(temREDUNDANT));
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
                ter(temARRAY_TOO_LARGE));
        }

        // temMALFORMED: Function Parameter is missing flag
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;

            Json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] =
                strHex(std::string("test"));
            func[sfFunction.jsonName][sfParameters.jsonName] = Json::arrayValue;

            Json::Value param;
            // Missing sfParameterFlag
            param[sfParameter.jsonName][sfParameterName.jsonName] =
                strHex(std::string("param1"));
            param[sfParameter.jsonName][sfParameterType.jsonName]["type"] =
                "UINT8";
            func[sfFunction.jsonName][sfParameters.jsonName].append(param);

            jv[sfFunctions.jsonName].append(func);
            env(jv, ter(temMALFORMED));
        }

        // temMALFORMED: Function Parameter is missing name
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;

            Json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] =
                strHex(std::string("test"));
            func[sfFunction.jsonName][sfParameters.jsonName] = Json::arrayValue;

            Json::Value param;
            param[sfParameter.jsonName][sfParameterFlag.jsonName] = 0;
            // Missing sfParameterName
            param[sfParameter.jsonName][sfParameterType.jsonName]["type"] =
                "UINT8";
            func[sfFunction.jsonName][sfParameters.jsonName].append(param);

            jv[sfFunctions.jsonName].append(func);
            env(jv, ter(temMALFORMED));
        }

        // temMALFORMED: Function Parameter is missing type
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;

            Json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] =
                strHex(std::string("test"));
            func[sfFunction.jsonName][sfParameters.jsonName] = Json::arrayValue;

            Json::Value param;
            param[sfParameter.jsonName][sfParameterFlag.jsonName] = 0;
            param[sfParameter.jsonName][sfParameterName.jsonName] =
                strHex(std::string("param1"));
            // Missing sfParameterType
            func[sfFunction.jsonName][sfParameters.jsonName].append(param);

            jv[sfFunctions.jsonName].append(func);
            env(jv, ter(temMALFORMED));
        }

        // temINVALID_FLAG: Invalid parameter flag in Function.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function(
                    "test", {{0xFF000000, "param", "UINT8"}}),  // Invalid flag
                ter(temINVALID_FLAG));
        }

        // temREDUNDANT: Duplicate Function Parameter name
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, BaseContractWasm),
                contract::add_function(
                    "test",
                    {{0, "param", "UINT8"},
                     {0, "param", "UINT32"}}),  // Duplicate param name
                ter(temREDUNDANT));
        }

        // temARRAY_EMPTY: InstanceParameters empty array
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;
            Json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] =
                strHex(std::string("test"));
            jv[sfFunctions.jsonName].append(func);
            jv[sfInstanceParameters.jsonName] =
                Json::arrayValue;  // Empty array

            env(jv, ter(temARRAY_EMPTY));
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
                ter(temARRAY_TOO_LARGE));
        }

        // temARRAY_EMPTY: InstanceParameterValues is missing
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            Json::Value jv;
            jv[jss::TransactionType] = jss::ContractCreate;
            jv[jss::Account] = alice.human();
            jv[jss::Fee] = to_string(XRP(10).value());
            jv[sfContractCode.jsonName] = BaseContractWasm;
            jv[sfFunctions.jsonName] = Json::arrayValue;
            Json::Value func;
            func[sfFunction.jsonName][sfFunctionName.jsonName] =
                strHex(std::string("test"));
            jv[sfFunctions.jsonName].append(func);
            jv[sfInstanceParameterValues.jsonName] =
                Json::arrayValue;  // Empty array

            env(jv, ter(temARRAY_EMPTY));
        }

        // // Test 18: InstanceParameterValues array is too large.
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     Json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = Json::arrayValue;
        //     Json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     Json::arrayValue; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = Json::arrayValue;

        //     // Add more than maxContractParams
        //     for (int i = 0; i < 257; ++i)
        //     {
        //         Json::Value param;
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterFlag.jsonName] = 0;
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterValue.jsonName]["type"] = "UINT8";
        //         param[sfInstanceParameterValue.jsonName]
        //              [sfParameterValue.jsonName]["value"] = i;
        //         jv[sfInstanceParameterValues.jsonName].append(param);
        //     }

        //     env(jv, ter(temARRAY_TOO_LARGE));
        // }

        // // Test 19: InstanceParameterValue missing flag
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     Json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = Json::arrayValue;
        //     Json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     Json::arrayValue; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = Json::arrayValue;

        //     Json::Value param;
        //     // Missing sfParameterFlag
        //     param[sfInstanceParameterValue.jsonName][sfParameterValue.jsonName]
        //          ["type"] = "UINT8";
        //     param[sfInstanceParameterValue.jsonName][sfParameterValue.jsonName]
        //          ["value"] = 1;
        //     jv[sfInstanceParameterValues.jsonName].append(param);

        //     env(jv, ter(temMALFORMED));
        // }

        // // Test 20: InstanceParameterValue missing value
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     Json::Value jv;
        //     jv[jss::TransactionType] = jss::ContractCreate;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::Fee] = to_string(XRP(10).value());
        //     jv[sfContractCode.jsonName] = BaseContractWasm;
        //     jv[sfFunctions.jsonName] = Json::arrayValue;
        //     Json::Value func;
        //     func[sfFunction.jsonName][sfFunctionName.jsonName] = "test";
        //     func[sfFunction.jsonName][sfParameters.jsonName] =
        //     Json::arrayValue; jv[sfFunctions.jsonName].append(func);
        //     jv[sfInstanceParameterValues.jsonName] = Json::arrayValue;

        //     Json::Value param;
        //     param[sfInstanceParameterValue.jsonName][sfParameterFlag.jsonName]
        //     =
        //         0;
        //     // Missing sfParameterValue
        //     jv[sfInstanceParameterValues.jsonName].append(param);

        //     env(jv, ter(temMALFORMED));
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
        //         ter(temINVALID_FLAG));
        // }

        // // Test 22: Success - ContractCode with Functions
        // {
        //     test::jtx::Env env{*this, features};

        //     auto const alice = Account{"alice"};
        //     env.fund(XRP(10'000), alice);
        //     env.close();

        //     env(contract::create(alice, BaseContractWasm),
        //         contract::add_function("base", {}),
        //         fee(XRP(200)),
        //         ter(tesSUCCESS));
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
        //         fee(XRP(200)),
        //         ter(tesSUCCESS));
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
        //         fee(XRP(200)),
        //         ter(tesSUCCESS));
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
                fee(XRP(200)),
                ter(temMALFORMED));
        }

        // temMALFORMED: ContractCode provided is empty
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, ""),  // Empty code
                contract::add_function("test", {}),
                fee(XRP(200)),
                ter(temMALFORMED));
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
                fee(XRP(200)),
                ter(tesSUCCESS));

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
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // Second create with same code (install)
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(
                    0, "uint8", "UINT8", 2),  // Different value
                contract::add_function("base", {}),
                fee(XRP(200)),
                ter(tesSUCCESS));

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
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // Get the hash of the contract
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // Install using ContractHash
            env(contract::create(alice, contractHash),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                fee(XRP(200)),
                ter(tesSUCCESS));

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
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // Get the hash
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // Try to install with mismatched InstanceParameterValues
            // Only providing one parameter when ContractSource expects two
            env(contract::create(alice, contractHash),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                fee(XRP(200)),
                ter(temMALFORMED));
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

            env(contract::create(alice, contractHash),
                fee(XRP(200)),
                ter(temMALFORMED));
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
                contract::add_instance_param(
                    tfSendAmount, "amount", "AMOUNT", XRP(100)),
                contract::add_function("base", {}),
                fee(XRP(200)),
                ter(tesSUCCESS));
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
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // Bob installs same contract
            env(contract::create(bob, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 2),
                contract::add_function("base", {}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // Alice installs another instance
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 3),
                contract::add_function("base", {}),
                fee(XRP(200)),
                ter(tesSUCCESS));
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

            auto const seq = env.current()->seq();
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                token::uri("https://example.com/contract"),
                fee(XRP(200)));

            // validate contract
            validateContract(
                env,
                contractAccount.id(),
                alice.id(),
                0,
                seq,
                contractHash,
                jv[sfInstanceParameterValues],
                to_string(jv[sfURI]));

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
                auto const seq = env.current()->seq();
                auto const [contractAccount, contractHash, jv] = setContract(
                    env,
                    tesSUCCESS,
                    contract::create(alice, BaseContractWasm),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}),
                    fee(XRP(200)));

                // validate contract
                validateContract(
                    env,
                    contractAccount.id(),
                    alice.id(),
                    0,
                    seq,
                    contractHash,
                    jv[sfInstanceParameterValues]);

                // validate contract source
                // validateContractSource(
                //     env, *wasmBytes, contractHash, 1, jv[sfFunctions]);
            }

            // Install Contract.
            {
                auto const seq = env.current()->seq();

                auto const [contractAccount, contractHash, jv] = setContract(
                    env,
                    tesSUCCESS,
                    contract::create(alice, BaseContractWasm),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}),
                    fee(XRP(200)));

                // validate contract
                validateContract(
                    env,
                    contractAccount.id(),
                    alice.id(),
                    0,
                    seq,
                    contractHash,
                    jv[sfInstanceParameterValues]);

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

            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)));

            env(contract::modify(alice, contractAccount, Base2ContractWasm),
                contract::add_instance_param(0, "uint16", "UINT16", 1),
                contract::add_function("base", {{0, "uint16", "UINT16"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            // {
            //     Json::Value params;
            //     params[jss::ledger_index] = env.current()->seq() - 1;
            //     params[jss::transactions] = true;
            //     params[jss::expand] = true;
            //     auto const jrr = env.rpc("json", "ledger",
            //     to_string(params)); std::cout << jrr << std::endl;
            // }
        }

        //----------------------------------------------------------------------
        // doApply.ContractHash.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)));

            auto const seq = env.current()->seq();
            auto const [contractAccount2, contractHash2, jv2] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, Base2ContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)));

            // auto const wasmBytes = strUnHex(Base2ContractWasm);
            // uint256 const contractHash = ripple::sha512Half_s(
            //     ripple::Slice(wasmBytes->data(), wasmBytes->size()));
            auto const [contractId, contractSle] =
                contractKeyAndSle(*env.current(), contractHash, seq);
            auto const newContractHash =
                contractSle->getFieldH256(sfContractHash);

            env(contract::modify(alice, contractAccount, newContractHash),
                contract::add_instance_param(0, "uint16", "UINT16", 1),
                contract::add_function("base", {{0, "uint16", "UINT16"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            {
                Json::Value params;
                params[jss::ledger_index] = env.current()->seq() - 1;
                params[jss::transactions] = true;
                params[jss::expand] = true;
                auto const jrr = env.rpc("json", "ledger", to_string(params));
                std::cout << jrr << std::endl;
            }
        }
    }

    void
    testDeleteDoApply(FeatureBitset features)
    {
        testcase("delete doApply");

        using namespace jtx;

        //----------------------------------------------------------------------
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
                fee(XRP(200)));

            env(contract::del(alice, contractAccount), ter(tesSUCCESS));
            env.close();

            auto const wasmBytes = strUnHex(BaseContractWasm);
            auto const k = keylet::contract(contractHash, seq);
            BEAST_EXPECT(!env.le(k));

            {
                Json::Value params;
                params[jss::ledger_index] = env.current()->seq() - 1;
                params[jss::transactions] = true;
                params[jss::expand] = true;
                auto const jrr = env.rpc("json", "ledger", to_string(params));
                std::cout << jrr << std::endl;
            }
        }
    }

    // std::string
    // loadContractWasmStr(std::string const& contract_name = "")
    // {
    //     //
    //     /Users/darkmatter/projects/ledger-works/craft/projects/target/wasm32v1-none/release/nft_owner.wasm
    //     std::string const& dir = "dangell7";
    //     std::string name =
    //         "/Users/darkmatter/projects/ledger-works/craft/projects/" + dir +
    //         "/" + contract_name + "/target/wasm32v1-none/release/" +
    //         contract_name + ".wasm";
    //     if (!std::filesystem::exists(name))
    //     {
    //         std::cout << "File does not exist: " << name << "\n";
    //         return "";
    //     }

    //     std::ifstream file(name, std::ios::binary);

    //     if (!file)
    //     {
    //         std::cout << "Failed to open file: " << name << "\n";
    //         return "";
    //     }

    //     // Read the file into a vector
    //     std::vector<char> buffer(
    //         (std::istreambuf_iterator<char>(file)),
    //         std::istreambuf_iterator<char>());

    //     // Check if the buffer is empty
    //     if (buffer.empty())
    //     {
    //         std::cout << "File is empty or could not be read properly.\n";
    //         return "";
    //     }

    //     return strHex(buffer);
    // }

    void
    testContractParameterFlags(FeatureBitset features)
    {
        testcase("contract parameter flags");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr =
        // loadContractWasmStr("contract_data_tests");
        std::string contractWasmStr =
            "0061736D0100000001360760067F7F7F7F7F7F017F60087F7F7F7F7F7F7F7F017F"
            "60037F7F7E017F60057F7F7F7F7F017F60027F7F0060037F7F7F006000017F02CD"
            "010608686F73745F6C69621A6765745F636F6E74726163745F646174615F66726F"
            "6D5F6B6579000008686F73745F6C69621A7365745F636F6E74726163745F646174"
            "615F66726F6D5F6B6579000008686F73745F6C6962217365745F6E65737465645F"
            "636F6E74726163745F646174615F66726F6D5F6B6579000108686F73745F6C6962"
            "0974726163655F6E756D000208686F73745F6C6962216765745F6E65737465645F"
            "636F6E74726163745F646174615F66726F6D5F6B6579000108686F73745F6C6962"
            "05747261636500030305040405060605030100110619037F01418080C0000B7F00"
            "41AC81C0000B7F0041B081C0000B073705066D656D6F7279020006637265617465"
            "00080675706461746500090A5F5F646174615F656E6403010B5F5F686561705F62"
            "61736503020ABB0604860101037F23808080800041106B22022480808080004100"
            "21032002410036020C024020014114419480C0800041052002410C6A4104108080"
            "8080004104470D00200228020C220341187420034180FE03714108747220034108"
            "764180FE0371200341187672722104410121030B20002004360204200020033602"
            "00200241106A2480808080000B4401017F23808080800041106B22032480808080"
            "00200320023A000F2003410236000B20004114200141052003410B6A4105108180"
            "8080001A200341106A2480808080000BB20301027F23808080800041C0006B2200"
            "248080808000200041206A410028009080C08000360200200041186A4100290088"
            "80C080003703002000410029008080C08000370310200041106A419480C0800041"
            "03108780808000200041106A419980C08000410C10878080800020004190183B00"
            "2A200041106A4114419E80C08000410341A180C0800041062000412A6A41021082"
            "808080001A200041346A41002900BA80C080003700002000413C6A41002800C280"
            "C0800036000020004188283B002A200041002900B280C0800037002C200041106A"
            "411441A780C08000410B2000412A6A41161081808080001A200041086A20004110"
            "6A1086808080000240024002402000280208410171450D0041C680C08000411320"
            "0028020CAD1083808080001A200041003A002A0240200041106A4114419E80C080"
            "00410341A180C0800041062000412A6A41011084808080004101470D0041D980C0"
            "8000411A200031002A1083808080001A410021010C030B41F380C0800041204100"
            "410041001085808080001A0C010B419381C0800041194100410041001085808080"
            "001A0B417F21010B200041C0006A24808080800020010BB70101027F2380808080"
            "0041206B220024808080800041002101200041186A410028009080C08000360200"
            "200041106A410029008880C080003703002000410029008080C080003703082000"
            "41086A419480C0800041041087808080002000200041086A108680808000024002"
            "402000280200410171450D0041C680C0800041132000280204AD1083808080001A"
            "0C010B419381C0800041194100410041001085808080001A417F21010B20004120"
            "6A24808080800020010B0BB6010100418080C0000BAC01AE123A8556F3CF911547"
            "11376AFB0F894F832B3D636F756E74746F74616C6B65797375626B657964657374"
            "696E6174696F6E0596915CFDEEE3A695B3EFD6BDA9AC788A368B7B526561642062"
            "61636B20636F756E743A207B7D52656164206261636B206E65737465642076616C"
            "75653A207B7D4661696C656420746F2072656164206261636B206E657374656420"
            "76616C75654661696C656420746F2072656164206261636B20636F756E74";
        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("create", {}),
            contract::add_function("update", {}),
            fee(XRP(2000)));

        {
            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            std::cout << jrr << std::endl;
        }

        env(contract::call(alice, contractAccount, "create"),
            escrow::comp_allowance(1'000'000),
            ter(tesSUCCESS));
        env.close();

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // {
        //     // Get contract info
        //     Json::Value params;
        //     params[jss::contract_account] = contractAccount.human();
        //     params[jss::account] = alice.human();
        //     auto const jrr =
        //         env.rpc("json", "contract_info", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "update"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
        env.close();

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // {
        //     // Get contract info
        //     Json::Value params;
        //     params[jss::contract_account] = contractAccount.human();
        //     params[jss::account] = alice.human();
        //     auto const jrr =
        //         env.rpc("json", "contract_info", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
    }

    void
    testContractData(FeatureBitset features)
    {
        testcase("contract data");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractDataWasmHex),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("create", {}),
            contract::add_function("update", {}),
            fee(XRP(2000)));

        {
            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            std::cout << jrr << std::endl;
        }

        env(contract::call(alice, contractAccount, "create"),
            escrow::comp_allowance(1'000'000),
            ter(tesSUCCESS));
        env.close();

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // {
        //     // Get contract info
        //     Json::Value params;
        //     params[jss::contract_account] = contractAccount.human();
        //     params[jss::account] = alice.human();
        //     auto const jrr =
        //         env.rpc("json", "contract_info", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "update"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
        env.close();

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // {
        //     // Get contract info
        //     Json::Value params;
        //     params[jss::contract_account] = contractAccount.human();
        //     params[jss::account] = alice.human();
        //     auto const jrr =
        //         env.rpc("json", "contract_info", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
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

        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, parametersWasmHex),
            contract::add_instance_param(
                tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_instance_param(0, "uint8", "UINT8", 255),
            contract::add_instance_param(0, "uint16", "UINT16", 65535),
            contract::add_instance_param(
                0, "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
            contract::add_instance_param(
                0, "uint64", "UINT64", "9223372036854775807"),
            contract::add_instance_param(
                0, "uint128", "UINT128", "00000000000000000000000000000001"),
            contract::add_instance_param(
                0,
                "uint160",
                "UINT160",
                "0000000000000000000000000000000000000001"),
            contract::add_instance_param(
                0,
                "uint192",
                "UINT192",
                "000000000000000000000000000000000000000000000001"),
            contract::add_instance_param(
                0,
                "uint256",
                "UINT256",
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25"),
            contract::add_instance_param(0, "vl", "VL", "DEADBEEF"),
            contract::add_instance_param(
                0, "account", "ACCOUNT", alice.human()),
            contract::add_instance_param(
                0,
                "amountXRP",
                "AMOUNT",
                XRP(1).value().getJson(JsonOptions::none)),
            contract::add_instance_param(
                0,
                "amountIOU",
                "AMOUNT",
                USD(1.2).value().getJson(JsonOptions::none)),
            contract::add_instance_param(0, "number", "NUMBER", "1.2"),
            contract::add_function("instance_params", {}),
            fee(XRP(200)));

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "instance_params"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
        env.close();
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

        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, parametersWasmHex),
            contract::add_instance_param(
                tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_instance_param(0, "uint8", "UINT8", 1),
            contract::add_function(
                "function_params",
                {
                    {0, "uint8", "UINT8"},
                    {0, "uint16", "UINT16"},
                    {0, "uint32", "UINT32"},
                    {0, "uint64", "UINT64"},
                    {0, "uint128", "UINT128"},
                    {0, "uint160", "UINT160"},
                    {0, "uint192", "UINT192"},
                    {0, "uint256", "UINT256"},
                    {0, "vl", "VL"},
                    {0, "account", "ACCOUNT"},
                    {0, "amountXRP", "AMOUNT"},
                    {0, "amountIOU", "AMOUNT"},
                    {0, "number", "NUMBER"},
                    //  {0, "issue", "ISSUE"},
                    //  {0, "currency", "CURRENCY"}
                }),
            fee(XRP(200)));

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "function_params"),
            escrow::comp_allowance(1000000),
            contract::add_param(0, "uint8", "UINT8", 255),
            contract::add_param(0, "uint16", "UINT16", 65535),
            contract::add_param(
                0, "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
            contract::add_param(0, "uint64", "UINT64", "9223372036854775807"),
            contract::add_param(
                0, "uint128", "UINT128", "00000000000000000000000000000001"),
            contract::add_param(
                0,
                "uint160",
                "UINT160",
                "0000000000000000000000000000000000000001"),
            contract::add_param(
                0,
                "uint192",
                "UINT192",
                "000000000000000000000000000000000000000000000001"),
            contract::add_param(
                0,
                "uint256",
                "UINT256",
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25"),
            contract::add_param(0, "vl", "VL", "DEADBEEF"),
            contract::add_param(0, "account", "ACCOUNT", alice.human()),
            contract::add_param(
                0,
                "amountXRP",
                "AMOUNT",
                XRP(1).value().getJson(JsonOptions::none)),
            contract::add_param(
                0,
                "amountIOU",
                "AMOUNT",
                USD(1.2).value().getJson(JsonOptions::none)),
            contract::add_param(0, "number", "NUMBER", "1.2"),
            // contract::add_param(0, "issue", "ISSUE",
            // to_json(USD(1).value().issue())), contract::add_param(0,
            // "currency", "CURRENCY", "USD"),
            ter(tesSUCCESS));
        env.close();
    }

    // void
    // testEmitTest(FeatureBitset features)
    // {
    //     testcase("emit test");

    //     using namespace jtx;

    //     test::jtx::Env env{*this, features};

    //     auto const alice = Account{"alice"};
    //     auto const bob = Account{"bob"};
    //     env.fund(XRP(10'000), alice, bob);
    //     env.close();

    //     uint256 const nftId = token::getNextID(env, alice, 0u, 0u);
    //     std::string const uri = "https://example.com/nft";
    //     env(token::mint(alice), token::uri(uri));
    //     env.close();

    //     std::string contractWasmStr = loadContractWasmStr("emit_test");
    //     auto const [contractAccount, contractHash, _] = setContract(
    //         env,
    //         tesSUCCESS,
    //         contract::create(alice, contractWasmStr),
    //         contract::add_instance_param(tfSendAmount, "value", "AMOUNT",
    //         XRP(2000)), contract::add_function("emit", {
    //             {tfSendAmount, "value", "AMOUNT"},
    //             {tfSendNFToken, "nft", "UINT256"},
    //             {0, "nftid", "UINT256"},
    //         }),
    //         fee(XRP(200)));

    //     // {
    //     //     Json::Value params;
    //     //     params[jss::ledger_index] = env.current()->seq() - 1;
    //     //     params[jss::transactions] = true;
    //     //     params[jss::expand] = true;
    //     //     auto const jrr = env.rpc("json", "ledger", to_string(params));
    //     //     std::cout << jrr << std::endl;
    //     // }

    //     env(contract::call(alice, contractAccount, "emit"),
    //         contract::add_param(tfSendAmount, "value", "AMOUNT", XRP(1)),
    //         contract::add_param(tfSellNFToken, "nftid", "UINT256",
    //         strHex(nftId)), contract::add_param(0, "nftid", "UINT256",
    //         strHex(nftId)), escrow::comp_allowance(1000000),
    //         ter(tesSUCCESS));
    //     env.close();
    // }

    void
    testEmitV2(FeatureBitset features)
    {
        testcase("emit v2");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr = loadContractWasmStr("emit_v2");
        std::string contractWasmStr =
            "0061736D0100000001250660037F7F7F017F60017F017F60047F7F7F7F017F6003"
            "7F7F7E017F6000017F60027F7F0002760508686F73745F6C69620C6765745F7478"
            "5F6669656C64000008686F73745F6C6962096275696C645F74786E000108686F73"
            "745F6C69620D6164645F74786E5F6669656C64000208686F73745F6C69620E656D"
            "69745F6275696C745F74786E000108686F73745F6C69620974726163655F6E756D"
            "0003030302040505030100110619037F01418080C0000B7F0041CB81C0000B7F00"
            "41D081C0000B072C04066D656D6F7279020004656D697400050A5F5F646174615F"
            "656E6403010B5F5F686561705F6261736503020AF10502EA0502037F017E238080"
            "80800041B0086B2200248080808000200041C0006A4100360200200041386A4200"
            "37030020004200370330024041818020200041306A411410808080800022014114"
            "470D002000411E6A20002D00323A00002000200029003737032020002000413C6A"
            "290000370025200020002F01303B011C2000200029032037030820002000290025"
            "37000D20002800332102416E2101024041001081808080004100480D0041004181"
            "801841E480C0800041081082808080004100480D002000413D6A200029000D3700"
            "00200041336A2000411E6A2D00003A0000200041143A0030200020002F011C3B00"
            "312000200236003420002000290308370038410041838020200041306A41151082"
            "808080004100480D00200041366A41002800EF80C08000360000200041306A4114"
            "6A41002800FB80C08000360000200041D2006A41002F008781C0800022023B0000"
            "200041073A0032200041EAF9013B0030200041FD183B003A200041FE143B004820"
            "0042E1D5F3A3E0ED9BBAE500370054200041FD3E3B005C200041002800EC80C080"
            "00360033200041002900F380C0800037003C200041002900FF80C0800022033700"
            "4A200041F5006A41002900A081C08000370000200041EE006A410029009981C080"
            "00370000200041E6006A410029009181C0800037000020004187016A20023B0000"
            "20004195016A41002900B081C080003700002000419D016A41002900B881C08000"
            "370000200041A5016A41002900C081C08000370000200041AC016A41002800C781"
            "C08000360000200041FE143B007D200041E1D5F79B023600890120004100290089"
            "81C0800037005E2000200337007F200041002900A881C0800037008D01200041E1"
            "E3033B00B00141004189803C200041306A4182011082808080004100480D004100"
            "1083808080002201411F7520017121010B200041B0086A24808080800020010F0B"
            "418080C08000410B2001417F2001417F481BAC1084808080001A200041306A41D4"
            "80C08000108680808000000B0300000B0BD5010100418080C0000BCB016572726F"
            "725F636F64653D2F55736572732F6461726B6D61747465722F70726F6A65637473"
            "2F6C65646765722D776F726B732F63726166742F7872706C2D7374642F7372632F"
            "686F73742F6D6F642E72730000000B001000470000006000000011000000400000"
            "00000000C0696E766F696365494E562D323032342D303031746578742F706C6169"
            "6E5061796D656E7420666F7220636F6E73756C74696E6720736572766963657341"
            "64646974696F6E616C207265666572656E63653A2050726F6A65637420416C7068"
            "61";

        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("emit", {}),
            fee(XRP(200)));

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "emit"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
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
        Json::Value stream;

        {
            // RPC subscribe to contract events stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("contract_events");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::status] == "success");
        }

        auto const [contractAccount, contractHash, _] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, eventsWasmHex),
            contract::add_instance_param(
                tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_function("events", {}),
            fee(XRP(200)));

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        env(contract::call(alice, contractAccount, "events"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
        env.close();

        // {
        //     // Get contract info
        //     Json::Value params;
        //     params[jss::contract_account] = contractAccount.human();
        //     params[jss::account] = alice.human();
        //     auto const jrr =
        //         env.rpc("json", "contract_info", to_string(params));
        //     std::cout << jrr << std::endl;
        // }

        // Check stream update
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            auto const data = jv[jss::data];
            // std::cout << "Event: " << data << std::endl;
            BEAST_EXPECT(data["amount"] == "192");
            BEAST_EXPECT(data["currency"] == "USD");
            BEAST_EXPECT(
                data["destination"] == "r99mpXDsCPybsGs9XzGJmuxa8gWLTn8aCz");
            BEAST_EXPECT(data["uint128"] == "00000000000000000000000000000000");
            BEAST_EXPECT(data["uint16"] == 16);
            BEAST_EXPECT(
                data["uint160"] == "0000000000000000000000000000000000000000");
            BEAST_EXPECT(
                data["uint192"] ==
                "000000000000000000000000000000000000000000000000");
            BEAST_EXPECT(
                data["uint256"] ==
                "00000000000000000000000000000000000000000000000000000000000000"
                "00");
            BEAST_EXPECT(data["uint32"] == 32);
            BEAST_EXPECT(data["uint64"] == "40");
            BEAST_EXPECT(data["uint8"] == 8);
            BEAST_EXPECT(data["vl"] == "48656C6C6F2C20576F726C6421");
            return jv[jss::type] == "contractEvent" &&
                jv[jss::name] == "event1";
        }));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testCreatePreflight(features);
        // testCreatePreclaim(features);
        // testCreateDoApply(features);
        // testModifyPreflight(features);
        // testModifyPreclaim(features);
        // testModifyDoApply(features);
        // testDeletePreflight(features);
        // testDeletePreclaim(features);
        // testDeleteDoApply(features);
        // testContractParameterFlags(features);
        testContractData(features);
        testInstanceParameters(features);
        testFunctionParameters(features);
        // testEmit(features);
        // testEmitV2(features);
        // testEmitTest(features);
        testEvents(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testable_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Contract, app, ripple);

}  // namespace test
}  // namespace ripple
