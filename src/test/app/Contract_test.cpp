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

#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class Contract_test : public beast::unit_test::suite
{
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

    Json::Value
    getLastLedger(jtx::Env& env)
    {
        Json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        return env.rpc("json", "ledger", to_string(params));
    }

    std::string
    getContractOwner(jtx::Env& env)
    {
        auto const jrr = getLastLedger(env);
        auto const txn = getContractCreateTx(jrr);
        for (auto const& meta : txn[jss::metaData][sfAffectedNodes.fieldName])
        {
            if (meta.isMember(sfCreatedNode.fieldName) &&
                meta[sfCreatedNode.fieldName].isMember(
                    sfLedgerEntryType.fieldName) &&
                meta[sfCreatedNode.fieldName][sfLedgerEntryType.fieldName] ==
                    jss::AccountRoot)
            {
                return meta[sfCreatedNode.fieldName][sfNewFields.fieldName]
                           [sfAccount.fieldName]
                               .asString();
            }
        }
        return "";
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
            auto const jt = env.jt(
                contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                token::uri("https://example.com/contract"));
            env(jt, fee(XRP(200)), ter(tesSUCCESS));
            env.close();

            auto const contractAccount = getContractOwner(env);
            auto const accountID = parseBase58<AccountID>(contractAccount);
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // validate contract
            validateContract(
                env,
                *accountID,
                alice.id(),
                0,
                seq,
                contractHash,
                jt.jv[sfInstanceParameterValues],
                to_string(jt.jv[sfURI]));

            // validate contract source
            validateContractSource(
                env, *wasmBytes, contractHash, 1, jt.jv[sfFunctions]);
        }

        //----------------------------------------------------------------------
        // doApply.ContractHash.tesSUCCESS

        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = getContractHash(*wasmBytes);

            // Create Contract.
            {
                auto const seq = env.current()->seq();
                auto const jt = env.jt(
                    contract::create(alice, BaseContractWasm),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}));
                env(jt, fee(XRP(200)), ter(tesSUCCESS));
                env.close();

                auto const contractAccount = getContractOwner(env);
                auto const accountID = parseBase58<AccountID>(contractAccount);

                // validate contract
                validateContract(
                    env,
                    *accountID,
                    alice.id(),
                    0,
                    seq,
                    contractHash,
                    jt.jv[sfInstanceParameterValues]);

                // validate contract source
                validateContractSource(
                    env, *wasmBytes, contractHash, 1, jt.jv[sfFunctions]);
            }

            // Install Contract.
            {
                auto const seq = env.current()->seq();
                auto const jt = env.jt(
                    contract::create(alice, contractHash),
                    contract::add_instance_param(0, "uint8", "UINT8", 1),
                    contract::add_function("base", {{0, "uint8", "UINT8"}}));
                env(jt, fee(XRP(200)), ter(tesSUCCESS));
                env.close();

                auto const contractAccount = getContractOwner(env);
                auto const accountID = parseBase58<AccountID>(contractAccount);

                // validate contract
                validateContract(
                    env,
                    *accountID,
                    alice.id(),
                    0,
                    seq,
                    contractHash,
                    jt.jv[sfInstanceParameterValues]);

                // validate contract source
                validateContractSource(
                    env, *wasmBytes, contractHash, 2, jt.jv[sfFunctions]);
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

            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            std::string const contractAccount = getContractOwner(env);

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

            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            auto const seq = env.current()->seq();
            env(contract::create(alice, Base2ContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            auto const contractAccount = getContractOwner(env);
            // auto const accountID = parseBase58<AccountID>(contractAccount);

            auto const wasmBytes = strUnHex(Base2ContractWasm);
            uint256 const contractHash = ripple::sha512Half_s(
                ripple::Slice(wasmBytes->data(), wasmBytes->size()));
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
            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            std::string const contractAccount = getContractOwner(env);
            env(contract::del(alice, contractAccount), ter(tesSUCCESS));
            env.close();

            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = ripple::sha512Half_s(
                ripple::Slice(wasmBytes->data(), wasmBytes->size()));
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

    std::string
    loadContractWasmStr(std::string const& contract_name = "")
    {
        std::string name =
            "/Users/darkmatter/projects/ledger-works/craft/projects/" +
            contract_name + "/target/wasm32-unknown-unknown/release/" +
            contract_name + ".wasm";
        if (!std::filesystem::exists(name))
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
        std::vector<char> buffer(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        // Check if the buffer is empty
        if (buffer.empty())
        {
            std::cout << "File is empty or could not be read properly.\n";
            return "";
        }

        return strHex(buffer);
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

        // std::string contractWasmStr = loadContractWasmStr("contract_data");
        std::string contractWasmStr =
            "0061736D01000000013D0A60037F7F7E017F60057F7F7F7F7F017F60047F7F7F7F"
            "017F60037F7F7F017F60027F7F0060037F7F7F0060047F7F7F7F0060017F006000"
            "017F60000002610408686F73745F6C69620974726163655F6E756D000008686F73"
            "745F6C6962057472616365000108686F73745F6C6962116765745F636F6E747261"
            "63745F64617461000208686F73745F6C6962117365745F636F6E74726163745F64"
            "617461000203100F03040405060708040807090405060305030100110619037F01"
            "418080C0000B7F0041E982C0000B7F0041F082C0000B073705066D656D6F727902"
            "0006637265617465000A06757064617465000C0A5F5F646174615F656E6403010B"
            "5F5F686561705F6261736503020AFD310F2B01017F410021030240200220002D00"
            "C804470D0020004188046A200120021092808080004521030B20030B880101027F"
            "024020012D0080A401220241214F0D00200241D0046C2102200141B00B6A210102"
            "40034041002103024020020D000C020B200241B07B6A2102200141D0046A220141"
            "9480C080004105108480808000450D000B20012D00004103470D00200128020421"
            "02410121030B20002002360204200020033602000F0B2002412010868080800000"
            "0B0900108E80808000000B4101017F2380808080004190046B2203248080808000"
            "200341033A00082003200236020C200020014105200341086A1088808080002003"
            "4190046A2480808080000BC10101047F024020002D0080A401220441214F0D0020"
            "0041B00B6A21052000200441D0046C22066A4180106A2107024002400340200645"
            "0D01200641B07B6A2106200541D0046A220520012002108480808000450D000B41"
            "8804450D0120052003418804FC0A00000F0B20044120460D0002402002450D0020"
            "074188046A20012002FC0A00000B200720023A00C8040240418804450D00200720"
            "03418804FC0A00000B200020002D0080A40141016A3A0080A4010B0F0B20044120"
            "108680808000000B870201037F23808080800041D098016B220124808080800041"
            "002102024041C000450D002001419098016A410041C000FC0B000B024003402002"
            "41809401460D01200141086A20026A220341003A0000024041C704450D00200341"
            "016A2001418994016A41C704FC0A00000B200341C8046A41003A0000200241D004"
            "6A21020C000B0B0240418010450D0020004100418010FC0B000B200041013A0081"
            "A4012000410029008080C08000370082A4012000418AA4016A410029008880C080"
            "0037000020004192A4016A410028009080C08000360000024041809401450D0020"
            "004180106A200141086A41809401FC0A00000B200041003A0080A401200141D098"
            "016A2480808080000BA90201027F23808080800041B0A8016B2200248080808000"
            "200041106A108980808000200041106A419480C080004103108780808000200041"
            "106A419980C08000410C108780808000200041B9A4016A41002800B980C0800036"
            "0000200041B1A4016A41002900B180C08000370000200041002900A980C0800037"
            "00A9A4012000410A3A00A8A401200041106A419E80C08000410B200041A8A4016A"
            "108880808000200041086A200041106A108B808080000240024020002802084101"
            "71450D00200028020C21010C010B2000200041106A108580808000024020002802"
            "00410171450D0041BD80C0800041132000280204AD1080808080001A410021010C"
            "010B41D080C0800041194100410041001081808080001A417F21010B200041B0A8"
            "016A24808080800020010B8B1002107F017E23808080800041A0156B2202248080"
            "80800020012D0080A4012103200241D0006A4200370300200241C8006A42003703"
            "00200241C0006A420037030020024200370338410021040340024020032004470D"
            "0020034101200341014B1B210520014180106A2106410021074101210803400240"
            "0240024002400240024020082005460D00200841016A21092008411F4B210A2007"
            "210403402004417F460D06200A0D02200241386A20046A220B41016A220C2D0000"
            "220D41204F0D03200B2D0000210E200241306A2006200D41D0046C6A220F418804"
            "6A200F2D00C804109080808000200E41204F0D042002280234210F200228023021"
            "10200241286A2006200E41D0046C6A22114188046A20112D00C804109080808000"
            "20102002280228200F200228022C2211200F2011491B1092808080002210200F20"
            "116B20101B417F4A0D06200B200D3A0000200C200E3A00002004417F6A21040C00"
            "0B0B4100210F0240418010450D00200241D8006A4100418010FC0B000B41002111"
            "034002400240024002400240024002400240024002400240024002400240024002"
            "40024002400240024002400240024002402003200F460D00200F4120460D012002"
            "41386A200F6A2D0000220441204F0D162006200441D0046C6A220E2D00C8042104"
            "024041C000450D00200241D8106A410041C000FC0B000B200441C1004F0D142002"
            "41206A200E4188046A20041090808080002002280224220D2004470D1502402004"
            "450D00200241D8106A20022802202004FC0A00000B200E41016A210D200E2D0000"
            "210B0240418704450D0020024198116A200D418704FC0A00000B200241186A2002"
            "41D8106A200410908080800020022802182110200241106A200241D8006A201120"
            "0228021C22041091808080002002280214210E20022802104101710D1002402004"
            "450D00200241D8006A20116A200E6A20102004FC0A00000B200420116A200E6A21"
            "04417D210E200441FF0F4B0D10200241D8006A20046A41003A0000200441FF0F46"
            "0D10200441016A21114171210E200B0E0E1002030405060708090A0B0C0D0E100B"
            "0240201141C101490D00201141C1E100490D114174210E201141D989384F0D1020"
            "01201141BF9E7F6A22043A0002200120044108763A00012001200441107641716A"
            "3A00004103210F0C120B200120113A00004101210F0C110B4120108D8080800000"
            "0B200241D8006A20116A220E41103A0000200E20022D0098113A00014102210E0C"
            "0C0B200241D8006A20116A220E41013A0000200E20022F009911220D410874200D"
            "410876723B00014103210E0C0B0B200241D8006A20116A220D41023A0000200D20"
            "0228009B11220E411874200E4180FE037141087472200E4108764180FE0371200E"
            "41187672723600014105210E0C0A0B200241D8006A20116A220E41033A0000200E"
            "200229009F11221242388620124280FE0383422886842012428080FC0783421886"
            "201242808080F80F834208868484201242088842808080F80F8320124218884280"
            "80FC07838420124228884280FE038320124238888484843700014109210E0C090B"
            "200241D8006A20116A220E41043A0000200E200D290000370001200E41096A200D"
            "41086A2900003700004111210E0C080B200241D8006A20116A220E41113A000020"
            "0E200D290000370001200E41096A200D41086A290000370000200E41116A200D41"
            "106A2800003600004115210E0C070B200241D8006A20116A220E41053A0000200E"
            "200D290000370001200E41096A200D41086A290000370000200E41116A200D4110"
            "6A290000370000200E41196A200D41186A2900003700004121210E0C060B200241"
            "D8006A20116A220E41063A0000200E2002290398113700014109210E0C050B2002"
            "41D8006A20116A220B41073A0000200241086A200241D8006A200441026A20022F"
            "009915220D109180808000200228020C210E20022802084101710D05200E41016A"
            "210E0240200D450D00200B200E6A20024198116A200DFC0A00000B200E200D6A21"
            "0E0C040B200241D8006A20116A220E4188283B0000200E200D290000370002200E"
            "410A6A200D41086A290000370000200E41126A200D41106A280000360000411621"
            "0E0C030B200241D8006A20116A220E411A3A0000200E200D290000370001200E41"
            "096A200D41086A290000370000200E41116A200D41106A2800003600004115210E"
            "0C020B200241D8006A20116A220D410E3A0000024020022F009915220E450D0020"
            "0D41016A20024198116A200EFC0A00000B200E41016A210E0C010B200241D8006A"
            "20116A220D410F3A0000024020022F009915220E450D00200D41016A2002419811"
            "6A200EFC0A00000B200E41016A210E0B2002200241D8006A2004200E1091808080"
            "002002280200410171450D072002280204210E0B410121040C020B2001201141BF"
            "7E6A22043A00012001200441087641416A3A00004102210F0B02402011450D0020"
            "01200F6A200241D8006A2011FC0A00000B410121044171210E20012D0081A40141"
            "01470D00200F20116A22044181104F0D0920014182A4016A411420012004108380"
            "8080001A410021040B2000200E36020420002004360200200241A0156A24808080"
            "80000F0B200441C000108680808000000B2004200D108F80808000000B2004108D"
            "80808000000B200F41016A210F200E20116A21110C000B0B2008108D8080800000"
            "0B200D108D80808000000B200E108D80808000000B200441801010868080800000"
            "0B200741016A2107200921080C000B0B024020044120460D00200241386A20046A"
            "20043A0000200441016A21040C010B0B4120108D80808000000B961702127F017E"
            "23808080800041B0B4016B2200248080808000200041106A108980808000024002"
            "4020002D0091A4014101470D0020004192A4016A4114200041106A418010108280"
            "80800022014100480D01200041003A0090A40102400240024020002D0010220241"
            "C101490D0002400240200241F101490D00200241FF01470D010C040B20002D0011"
            "2002413F6A41FF01714108747241C1016A2102410221030C020B20002D00114108"
            "742002410F6A41FF01714110747220002D00127241C1E1006A2102410321030C01"
            "0B410121030B200320026A220420014B21024175210120020D02200041106A4180"
            "106A2105200041B0A8016A41146A2106200041B0AC016A41146A2107200041B0B0"
            "016A41146A21084100210902400340024002400240200320044F0D00200341FF0F"
            "4B0D04200041106A20036A22022D0000220A41C101490D010240200A41F101490D"
            "00200A41FF01460D06200341FD0F4B0D0520022D0001410874200A410F6A41FF01"
            "714110747220022D00027241C1E1006A210A410321020C030B200341FF0F460D04"
            "20022D0001200A413F6A41FF01714108747241C1016A210A410221020C020B4175"
            "210120032004470D06200041106A419480C080004104108780808000200041086A"
            "200041106A108B8080800002402000280208410171450D00200028020C21010C07"
            "0B2000200041106A10858080800002402000280200410171450D0041BD80C08000"
            "41132000280204AD1080808080001A410021010C070B41D080C080004119410041"
            "0041001081808080001A417F21010C060B410121020B200220036A2203200A6A22"
            "0B4180104B0D01200041106A20036A21020240200A450D004100200A41796A2203"
            "2003200A4B1B210C200241036A417C7120026B210D410021030340024002400240"
            "0240200220036A2D0000220EC0220F4100480D00200D20036B4103710D01200320"
            "0C4F0D020340200220036A220141046A280200200128020072418081828478710D"
            "03200341086A2203200C490D000C030B0B41752101024002400240024002400240"
            "024002400240200E41E980C080006A2D0000417E6A0E03000201120B200341016A"
            "2203200A4F0D11200220036A2C000041BF7F4C0D070C110B200341016A2210200A"
            "4F0D10200220106A2C00002110200E41907E6A0E050201010103010B200341016A"
            "2210200A4F0D0F200220106A2C00002110024002400240200E41E001460D00200E"
            "41ED01460D01200F411F6A41FF0171410C490D02200F417E71416E470D12201041"
            "40480D070C120B201041607141A07F460D060C110B2010419F7F4A0D100C050B20"
            "104140480D040C0F0B200F410F6A41FF017141024B0D0E20104140480D020C0E0B"
            "201041F0006A41FF01714130490D010C0D0B2010418F7F4A0D0C0B200341026A22"
            "0E200A4F0D0B2002200E6A2C000041BF7F4A0D0B200341036A2203200A4F0D0B20"
            "0220036A2C000041BF7F4A0D0B0C010B200341026A2203200A4F0D0A200220036A"
            "2C000041BF7F4A0D0A0B200341016A21030C020B200341016A21030C010B200320"
            "0A4F0D000340200220036A2C00004100480D01200A200341016A2203470D000C03"
            "0B0B2003200A490D000B0B200B418010460D0102400240200041106A200B6A2203"
            "2D0000220C41C101490D000240200C41F101490D00200C41FF01460D05200B41FD"
            "0F4B0D0420032D0001410874200C410F6A41FF01714110747220032D00027241C1"
            "E1006A210C410321030C020B200B41FF0F460D0320032D0001200C413F6A41FF01"
            "714108747241C1016A210C410221030C010B410121030B417321012003200B6A22"
            "0E200C6A22034180104B0D04200C450D03200C417F6A210C200E41016A210B416F"
            "210102400240024002400240024002400240024002400240024002400240024002"
            "40024002400240200041106A200E6A220F2D0000417F6A0E1A050607080A0B0001"
            "17171717170203040917171717171717170E170B41732101200E41FE0F4B0D1620"
            "0041106A200B6A220D2D0000220F41C101490D0B0240200F41F101490D00200F41"
            "FF01460D15200E41FC0F4B0D17200D2D0001410874200F410F6A41FF0171411074"
            "72200D2D00027241C1E1006A210F4103210E0C0D0B200B41FF0F460D16200D2D00"
            "01200F413F6A41FF01714108747241C1016A210F4102210E0C0C0B200C4115470D"
            "15200041106A200B6A2D00004114470D152000200F290002370398A8012000200F"
            "41096A29000037009FA801200F2800122111200F2D0011210E410A210B0C0D0B20"
            "0C4180044D0D0D0C0F0B200C4180044B0D0E0240418004450D00200041B0B0016A"
            "4100418004FC0B000B0240200C450D00200041B0B0016A200041106A200B6A200C"
            "FC0A00000B2000200041B0B0016A41076A29000037009FA801200020002900B0B0"
            "01370398A80120002D00BFB001210E20002800C0B0012111024041EC03450D0020"
            "0041A8A4016A200841EC03FC0A00000B410D210B200C210F0C0D0B200C4101470D"
            "122000200041106A200B6A2D00003A0098A8014101210B0C0A0B200C4102470D11"
            "2000200041106A200B6A2F000022014108742001410876723B0099A8014102210B"
            "0C090B200C4104470D102000200041106A200B6A280000220141187420014180FE"
            "03714108747220014108764180FE03712001411876727236009BA8014103210B0C"
            "080B200C4108470D0F2000200041106A200B6A290000221242388620124280FE03"
            "83422886842012428080FC0783421886201242808080F80F834208868484201242"
            "088842808080F80F832012421888428080FC07838420124228884280FE03832012"
            "42388884848437009FA8014104210B0C070B200C4110470D0E2000200041106A20"
            "0B6A2201290000370398A8012000200141076A29000037009FA80120012D000F21"
            "0E4105210B0C060B200C4114470D0D2000200041106A200B6A2201290000370398"
            "A8012000200141076A29000037009FA8012001280010211120012D000F210E4106"
            "210B0C050B200C4120470D0C200041A8A4016A41086A200041106A200B6A220141"
            "1C6A28000036020020002001290000370398A801200020012900143703A8A40141"
            "07210B2000200141076A29000037009FA8012001280010211120012D000F210E0C"
            "040B200C4108470D0B2000200041106A200B6A290000370398A8014108210B0C03"
            "0B4101210E0B41752101200F4180044B0D09200E200F6A200C4B0D090240418004"
            "450D00200041B0A8016A4100418004FC0B000B0240200F450D00200041B0A8016A"
            "200D200E6A200FFC0A00000B2000200041B0A8016A41076A29000037009FA80120"
            "0020002900B0A801370398A80120002D00BFA801210E20002800C0A80121110240"
            "41EC03450D00200041A8A4016A200641EC03FC0A00000B4109210B0C030B200C41"
            "14470D082000200041106A200B6A2201290000370398A8012000200141076A2900"
            "0037009FA8012001280010211120012D000F210E410B210B0B0C010B0240418004"
            "450D00200041B0AC016A4100418004FC0B000B0240200C450D00200041B0AC016A"
            "200041106A200B6A200CFC0A00000B2000200041B0AC016A41076A29000037009F"
            "A801200020002900B0AC01370398A80120002D00BFAC01210E20002800C0AC0121"
            "11024041EC03450D00200041A8A4016A200741EC03FC0A00000B410C210B200C21"
            "0F0B200041ACA8016A41026A220C20002D009AA8013A0000200020002F0198A801"
            "3B01ACA8010240200941FF01712201411F4D0D00417821010C060B200A41C1004F"
            "0D00200028009BA801210D200029009FA80121122005200141D0046C6A21010240"
            "200A450D0020014188046A2002200AFC0A00000B2001200B3A00002001200A3A00"
            "C804200120113600112001200E3A0010200120123703082001200D360204200120"
            "002F01ACA8013B0001200141036A200C2D00003A0000024041ED03450D00200141"
            "156A200041A8A4016A41ED03FC0A00000B2001200F3B018204200020002D0090A4"
            "0141016A22093A0090A4010C010B0B417421010C030B417321010C020B41752101"
            "0C010B417121010B200041B0B4016A24808080800020010B0900108E8080800000"
            "0B0300000B0900108E80808000000B27000240200241C100490D00200241C00010"
            "8680808000000B20002002360204200020013602000BD10101027F41012104417D"
            "21050240200241FF0F4B0D000240200341C101490D000240200341C1E100490D00"
            "0240200341D98938490D00417421050C030B200120026A2201200341BF9E7F6A22"
            "0341107641716A3A0000200241FD0F4B0D022001200341087420034180FE037141"
            "0876723B000141002104410321050C020B200120026A2201200341BF7E6A220341"
            "087641416A3A0000200241FF0F460D01200120033A000141002104410221050C01"
            "0B200120026A20033A000041002104410121050B20002005360204200020043602"
            "000B4A01037F4100210302402002450D000240034020002D0000220420012D0000"
            "2205470D01200041016A2100200141016A21012002417F6A2202450D020C000B0B"
            "200420056B21030B20030B0BF3020100418080C0000BE902AE123A8556F3CF9115"
            "4711376AFB0F894F832B3D636F756E74746F74616C64657374696E6174696F6E05"
            "96915CFDEEE3A695B3EFD6BDA9AC788A368B7B52656164206261636B20636F756E"
            "743A207B7D4661696C656420746F2072656164206261636B20636F756E74010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010100000000000000"
            "000000000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000000000002020202020202"
            "020202020202020202020202020202020202020202020203030303030303030303"
            "03030303030304040404040000000000000000000000";

        env(contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("create", {}),
            contract::add_function("update", {}),
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

        auto const contractAccount = getContractOwner(env);
        env(contract::call(alice, contractAccount, "create"),
            escrow::comp_allowance(1'000'000),
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

        {
            // Get contract info
            Json::Value params;
            params[jss::contract_account] = contractAccount;
            params[jss::account] = alice.human();
            auto const jrr =
                env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }

        env(contract::call(alice, contractAccount, "update"),
            escrow::comp_allowance(1000000),
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

        {
            // Get contract info
            Json::Value params;
            params[jss::contract_account] = contractAccount;
            params[jss::account] = alice.human();
            auto const jrr =
                env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }
    }

    void
    testContractDataV2(FeatureBitset features)
    {
        testcase("contract data v2");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr =
        // loadContractWasmStr("contract_data_v2");
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

        env(contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("create", {}),
            contract::add_function("update", {}),
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

        auto const contractAccount = getContractOwner(env);
        env(contract::call(alice, contractAccount, "create"),
            escrow::comp_allowance(1'000'000),
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

        {
            // Get contract info
            Json::Value params;
            params[jss::contract_account] = contractAccount;
            params[jss::account] = alice.human();
            auto const jrr =
                env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }

        env(contract::call(alice, contractAccount, "update"),
            escrow::comp_allowance(1000000),
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

        {
            // Get contract info
            Json::Value params;
            params[jss::contract_account] = contractAccount;
            params[jss::account] = alice.human();
            auto const jrr =
                env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }
    }

    void
    testParameters(FeatureBitset features)
    {
        testcase("parameters");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr = loadContractWasmStr("parameters");
        std::string contractWasmStr =
            "0061736D0100000001300760047F7F7F7F017F60037F7F7E017F60057F7F7F7F7F"
            "017F60077F7F7F7F7F7F7F017F6000017F60027F7F0060000002750508686F7374"
            "5F6C69620F6F74786E5F63616C6C5F706172616D000008686F73745F6C69620974"
            "726163655F6E756D000108686F73745F6C6962057472616365000208686F73745F"
            "6C69621274726163655F6F70617175655F666C6F6174000008686F73745F6C6962"
            "09666C6F61745F616464000303040304050605030100110619037F01418080C000"
            "0B7F0041DB84C0000B7F0041E084C0000B072C04066D656D6F727902000463616C"
            "6C00050A5F5F646174615F656E6403010B5F5F686561705F6261736503020AD911"
            "03C81103037F017E017F23808080800041B0026B2200248080808000200041003A"
            "0001418080C08000411041004110200041016A4101108080808000AC1081808080"
            "001A419080C08000410C20003100011081808080001A419C80C08000410A200041"
            "016A410141011082808080001A200041003B010241A680C0800041114101410120"
            "0041026A4102108080808000AC1081808080001A41B780C08000410D2000330102"
            "1081808080001A41C480C08000410B200041026A410241011082808080001A2000"
            "410036020441CF80C08000411141024102200041046A4104108080808000AC1081"
            "808080001A41E080C08000410D20003502041081808080001A41ED80C08000410B"
            "200041046A410441011082808080001A2000420037030841F880C0800041114103"
            "4103200041086A4108108080808000AC1081808080001A418981C08000410D2000"
            "2903081081808080001A419681C08000410B200041086A41084101108280808000"
            "1A200042003703182000420037031041A181C08000411241044104200041106A41"
            "10108080808000AC1081808080001A41B381C08000410E20002903101081808080"
            "001A41C181C08000410C200041106A411041011082808080001A200041206A4110"
            "6A4100360200200042003703282000420037032041CD81C0800041124105411120"
            "0041206A4114108080808000AC1081808080001A41DF81C08000410E2000290320"
            "1081808080001A41ED81C08000410C200041206A411441011082808080001A2000"
            "41C0006A41106A4200370300200042003703482000420037034041F981C0800041"
            "1241064115200041C0006A4118108080808000AC1081808080001A418B82C08000"
            "410E20002903401081808080001A419982C08000410C200041C0006A4118410110"
            "82808080001A200041E0006A41186A4200370300200041E0006A41106A42003703"
            "00200041E0006A41086A42003703002000420037036041A582C080004112410741"
            "05200041E0006A4120108080808000AC1081808080001A41B782C08000410C2000"
            "41E0006A412041011082808080001A200041003602840141C382C08000410D4108"
            "410720004184016A4104108080808000AC1081808080001A41D082C08000410720"
            "004184016A410441011082808080001A20004188016A41106A2201410036020020"
            "004188016A41086A22024200370300200042003703880141D782C0800041124109"
            "410820004188016A4114108080808000AC1081808080001A200041A0016A41106A"
            "2001280200360200200041A0016A41086A20022903003703002000200029038801"
            "3703A00141E982C08000410E200041A0016A411441011082808080001A20004200"
            "3703B80141F782C080004111410A4106200041B8016A41081080808080002201AC"
            "1081808080001A0240024020002D00B801220241A00171450D00418883C0800041"
            "13427F1081808080001A0C010B418883C08000411320002903B801220342018342"
            "388620034280FE0383422886842003428080FC0783421886200342808080F80F83"
            "4208868484200342088842808080F80F832003421888428080FC07838420034228"
            "884280FE038320034238888484842203420020037D200241C000711B1081808080"
            "001A0B02400240024002400240200141094F0D00419B83C08000410B200041B801"
            "6A200141011082808080001A02404130450D00200041C0016A41004130FC0B000B"
            "41F782C080004111410B4106200041C0016A41301080808080002201AC10818080"
            "80001A200141314F0D01024020010D0020004283808080703703F0010C040B0240"
            "024020002D00C0012204C02202417F4A0D0020014130460D012000428380808070"
            "3703F0010C050B02402002412071450D00024020014121470D00200041F0016A41"
            "196A200041D1016A290000370000200041F0016A41216A200041C0016A41196A29"
            "0000370000200020002900C90137008102200041023602F0012000200241C00171"
            "4106763A008002200020002900C101220342388620034280FE0383422886842003"
            "428080FC0783421886200342808080F80F834208868484200342088842808080F8"
            "0F832003421888428080FC07838420034228884280FE0383200342388884848437"
            "03F8010C060B20004283808080703703F0010C050B20014108460D032000428380"
            "8080703703F0010C040B200041F0016A412C6A200041C0016A41106A2903003702"
            "00200041A4026A200041C0016A41186A280200360200200041F0016A41186A2000"
            "41C0016A41246A29020037030020004190026A200041C0016A412C6A2802003602"
            "00200020002903C80137029402200020002902DC0137038002200020002903C001"
            "3703F801200041013602F00141A683C080004113200041F0016A41086A22024108"
            "41011082808080001A41B983C08000411E200241081083808080001A41D783C080"
            "00410B200041F0016A41106A411441011082808080001A41E283C08000410D2000"
            "41F0016A41246A411441011082808080001A200042003703A80202400240200241"
            "0841EF83C080004108200041A8026A4108410010848080800022024108470D0041"
            "F783C080004124200041A8026A410841011082808080001A41F783C08000412420"
            "0041A8026A41081083808080001A0C010B419B84C08000412D2002AC1081808080"
            "001A0B20002903F8012103410121020C040B20014108108680808000000B200141"
            "30108680808000000B200041003602F001200020002903C0012203420183423886"
            "20034280FE0383422886842003428080FC0783421886200342808080F80F834208"
            "868484200342088842808080F80F832003421888428080FC078384200342288842"
            "80FE038320034238888484842203420020037D200441C000711B3703F8010B41A6"
            "83C08000411341C884C08000410841011082808080001A410021020B419B83C080"
            "00410B200041C0016A200141011082808080001A024002402002450D0020002003"
            "3703A80241D084C08000410B200041A8026A410841011082808080001A0C010B41"
            "D084C08000410B41C884C08000410841011082808080001A0B200041B0026A2480"
            "8080800041000B0900108780808000000B0300000B0BE5040100418080C0000BDB"
            "0455494E54382056616C7565204C656E3A55494E54382056616C75653A55494E54"
            "38204865783A55494E5431362056616C7565204C656E3A55494E5431362056616C"
            "75653A55494E543136204865783A55494E5433322056616C7565204C656E3A5549"
            "4E5433322056616C75653A55494E543332204865783A55494E5436342056616C75"
            "65204C656E3A55494E5436342056616C75653A55494E543634204865783A55494E"
            "543132382056616C7565204C656E3A55494E543132382056616C75653A55494E54"
            "313238204865783A55494E543136302056616C7565204C656E3A55494E54313630"
            "2056616C75653A55494E54313630204865783A55494E543139322056616C756520"
            "4C656E3A55494E543139322056616C75653A55494E54313932204865783A55494E"
            "543235362056616C7565204C656E3A55494E54323536204865783A564C2056616C"
            "7565204C656E3A564C204865783A4143434F554E542056616C7565204C656E3A41"
            "43434F554E542056616C75653A414D4F554E542056616C7565204C656E3A414D4F"
            "554E542056616C75652028585250293A414D4F554E54204865783A414D4F554E54"
            "2056616C75652028494F55293A414D4F554E542056616C75652028494F5529202D"
            "204F726967696E616C3A494F55204973737565723A494F552043757272656E6379"
            "3AD4838D7EA4C68000414D4F554E542056616C75652028494F5529202D20416674"
            "657220616464696E6720313A4572726F7220616464696E6720464C4F41545F4F4E"
            "4520746F20494F5520616D6F756E742C20726573756C743A000000000000000049"
            "4F5520416D6F756E743A00B903046E616D6500100F706172616D65746572732E77"
            "61736D01FF020800365F5A4E387872706C5F73746434686F737431356F74786E5F"
            "63616C6C5F706172616D3137686264626330346266356630656331643845012F5F"
            "5A4E387872706C5F73746434686F73743974726163655F6E756D31376862396666"
            "61343664323065373166386345022B5F5A4E387872706C5F73746434686F737435"
            "7472616365313768376332613165636536303664316537664503395F5A4E387872"
            "706C5F73746434686F7374313874726163655F6F70617175655F666C6F61743137"
            "683733626632346130353361373561323945042F5F5A4E387872706C5F73746434"
            "686F737439666C6F61745F61646431376864323239336566303766363638313936"
            "45050463616C6C06425F5A4E34636F726535736C69636535696E6465783234736C"
            "6963655F656E645F696E6465785F6C656E5F6661696C3137686164666263376531"
            "61313539373461314507305F5A4E34636F72653970616E69636B696E673970616E"
            "69635F666D743137683431636665643739623264646266313345071201000F5F5F"
            "737461636B5F706F696E746572090A0100072E726F64617461004D0970726F6475"
            "6365727302086C616E6775616765010452757374000C70726F6365737365642D62"
            "79010572757374631D312E38382E30202836623030626333383820323032352D30"
            "362D3233290094010F7461726765745F6665617475726573082B0B62756C6B2D6D"
            "656D6F72792B0F62756C6B2D6D656D6F72792D6F70742B1663616C6C2D696E6469"
            "726563742D6F7665726C6F6E672B0A6D756C746976616C75652B0F6D757461626C"
            "652D676C6F62616C732B136E6F6E7472617070696E672D6670746F696E742B0F72"
            "65666572656E63652D74797065732B087369676E2D657874";

        env(contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_instance_param(0, "uint8", "UINT8", 1),
            contract::add_function(
                "call",
                {{0, "uint8", "UINT8"},
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
                 {0, "amountIOU", "AMOUNT"}}),
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

        auto const contractAccount = getContractOwner(env);
        env(contract::call(alice, contractAccount, "call"),
            escrow::comp_allowance(1000000),
            contract::add_param("uint8", "UINT8", 255),
            contract::add_param("uint16", "UINT16", 65535),
            contract::add_param(
                "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
            contract::add_param("uint64", "UINT64", "9223372036854775807"),
            contract::add_param(
                "uint128", "UINT128", "00000000000000000000000000000001"),
            contract::add_param(
                "uint160",
                "UINT160",
                "0000000000000000000000000000000000000001"),
            contract::add_param(
                "uint192",
                "UINT192",
                "000000000000000000000000000000000000000000000001"),
            contract::add_param(
                "uint256",
                "UINT256",
                "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
                "25"),
            contract::add_param("vl", "VL", "DEADBEEF"),
            contract::add_param("account", "ACCOUNT", alice.human()),
            contract::add_param(
                "amountXRP",
                "AMOUNT",
                XRP(1).value().getJson(JsonOptions::none)),
            contract::add_param(
                "amountIOU",
                "AMOUNT",
                USD(1).value().getJson(JsonOptions::none)),
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

    void
    testSubmit(FeatureBitset features)
    {
        testcase("submit");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr = loadContractWasmStr("submit");
        std::string contractWasmStr =
            "0061736D0100000001430B60057F7F7F7F7F017F60037F7F7F017F60047F7F7F7F"
            "017F60037F7F7E017F60027F7F017F6000017F60027F7F0060047F7F7F7F006003"
            "7F7F7F0060000060017F0002A9010708686F73745F6C6962057472616365000008"
            "686F73745F6C69620C6765745F74785F6669656C64000108686F73745F6C69620E"
            "6163636F756E745F6B65796C6574000208686F73745F6C69621063616368655F6C"
            "65646765725F6F626A000108686F73745F6C6962146765745F6C65646765725F6F"
            "626A5F6669656C64000208686F73745F6C69620974726163655F6E756D00030868"
            "6F73745F6C696208656D69745F74786E00040309080506070804090A0605030100"
            "110619037F01418080C0000B7F0041CC81C0000B7F0041D081C0000B072C04066D"
            "656D6F7279020004656D697400070A5F5F646174615F656E6403010B5F5F686561"
            "705F6261736503020ADC1B08AD1003047F027E017F23808080800041C00C6B2200"
            "248080808000418B80C0800041234100410041001080808080001A200041A8046A"
            "22014100360200200041A0046A2202420037030020004200370398040240024002"
            "4002404181802020004198046A411410818080800022034114470D00200041C600"
            "6A41026A20002D009A043A00002000200029009F043703D002200020004198046A"
            "410C6A22032900003700D502200041C6006A410C6A20002900D502370000200020"
            "002F0198043B01462000200028009B04360049200020002903D00237004D41AE80"
            "C08000410A200041C6006A411441011080808080001A2001410036020020024200"
            "37030020004200370398044199802020004198046A411410818080800022014114"
            "470D01200041DA006A41026A20002D009A043A00002000200029009F043703D002"
            "200020032900003700D502200041DA006A410C6A20002900D50237000020002000"
            "2F0198043B015A2000200028009B0436005D200020002903D00237006141B880C0"
            "80004113200041DA006A411441011080808080001A02404121450D00200041EF00"
            "6A41004121FC0B000B41CB80C080004110200041EF006A41214101108080808000"
            "1A200041A0046A2101024041284522020D00200141004128FC0B000B200042C000"
            "3703980420004190016A20004198046A108880808000024020020D002001410041"
            "28FC0B000B200042C0808080808080804037039804200041C8016A20004198046A"
            "10888080800020004198046A41186A420037030020004198046A41106A42003703"
            "0020014200370300200042003703980402400240200041DA006A41142000419804"
            "6A41201082808080004120460D00410021010C010B200041D2026A20002D009A04"
            "3A0000200041A80C6A20004198046A410F6A2900002204370300200041B00C6A20"
            "004198046A41176A2900002205370300200041A00C6A41186A20004198046A411F"
            "6A2D000022013A0000200041D0026A410F6A2004370000200041D0026A41176A20"
            "05370000200041D0026A411F6A20013A0000200020002F0198043B01D002200020"
            "0029009F0422043703A00C200020043700D7022000200028009B043600D3024100"
            "2101200041D0026A4120410010838080800022024101480D002000410036029804"
            "20024184800820004198046A410410848080800021012000280298044100200141"
            "04461B21010B41EF80C08000410E2001AD1085808080001A2000419C036A200041"
            "EA006A28010036020020004194036A200041E2006A290100370200200020002901"
            "5A37028C03200041A6036A210302404121450D002003200041EF006A4121FC0A00"
            "000B200041C8026A41002800EB80C08000360200200041C0026A41002900E380C0"
            "8000370300200041002900DB80C080003703B802024041384522020D0020004180"
            "026A200041C8016A4138FC0A00000B024020020D00200041D0026A20004190016A"
            "4138FC0A00000B200041003B01A40320004180808080023602A003200020013602"
            "8803200041C8036A2102024041D000450D00200220004180026A41D000FC0A0000"
            "0B0240418008450D0020004198046A4100418008FC0B000B200041013602980C20"
            "0041123A009804200041003B01A00C200041386A20004198046A200041A00C6A41"
            "0210898080800002400240024020002802380D0020002802980C220641FF074B0D"
            "0020004198046A20066A41223A0000200020002802980C41016A3602980C200041"
            "203602A00C200041306A20004198046A200041A00C6A4104108980808000200028"
            "02300D0020002802980C220641FF074B0D0020004198046A20066A41243A000020"
            "0020002802980C41016A3602980C2000200141187420014180FE03714108747220"
            "014108764180FE0371200141187672723602A00C200041286A20004198046A2000"
            "41A00C6A410410898080800020002802280D0020002802980C220141FF074B0D00"
            "20004198046A20016A41E1003A0000200020002802980C41016A3602980C200041"
            "206A200220004198046A108A8080800020002802200D0020002802980C220141FF"
            "074B0D0020004198046A20016A41E8003A0000200020002802980C41016A360298"
            "0C200041186A200041D0026A20004198046A108A8080800020002802180D002000"
            "2802980C220141FF074B0D0020004198046A20016A41F3003A0000200020002802"
            "980C41016A22013602980C0240024020002D00A6030D00200141FF074B0D022000"
            "4198046A20016A41003A0000200020002802980C41016A3602980C0C010B200141"
            "FF074B0D0120004198046A20016A41213A0000200020002802980C41016A360298"
            "0C200041106A20004198046A2003412110898080800020002802100D010B200041"
            "8C036A20004198046A108B808080000D0020002802980C220141FF074B0D002000"
            "4198046A20016A4183013A0000200020002802980C41016A22013602980C200141"
            "FF074B0D0020004198046A20016A41143A0000200020002802980C41016A360298"
            "0C200041086A20004198046A20004180046A41141089808080002000280208450D"
            "010B41A181C08000411C4100410041001080808080001A416E21010C010B200028"
            "02980C22014181084F0D0320004198046A2001108680808000210220002802980C"
            "22014181084F0D0441FD80C08000411620004198046A200141011080808080001A"
            "419381C08000410E2002AC1085808080001A410021010B200041C00C6A24808080"
            "800020010F0B418080C08000410B2003417F2003417F481BAC1085808080001A10"
            "8C80808000000B418080C08000410B2001417F2001417F481BAC1085808080001A"
            "108C80808000000B2001108D80808000000B2001108D80808000000BCA0302037F"
            "017E23808080800041306B21020240024020012D00002203C02204417F4A0D0020"
            "0241236A200141106A2900003700002002412B6A200141186A2800003600002002"
            "41106A200141256A290000370300200241176A2001412C6A280000360000200220"
            "0129000837001B2002200129001D37030820012D001C2104200129000021054101"
            "21010C010B02402004412071450D00200241106A200141116A2900003703002002"
            "41186A200141196A29000037030020022001290009370308200129000122054238"
            "8620054280FE0383422886842005428080FC0783421886200542808080F80F8342"
            "08868484200542088842808080F80F832005421888428080FC0783842005422888"
            "4280FE038320054238888484842105200441C001714106762104410221010C010B"
            "2001290000220542018342388620054280FE0383422886842005428080FC078342"
            "1886200542808080F80F834208868484200542088842808080F80F832005421888"
            "428080FC07838420054228884280FE038320054238888484842205420020057D20"
            "0341C000711B2105410021010B200020043A001020002005370308200020013602"
            "0002404127450D00200041116A200241086A4127FC0A00000B0B6E01037F41BD81"
            "C08000210402400240200128028008220520036A22064180084B0D002006200549"
            "0D0102402003450D00200120056A20022003FC0A00000B20012001280280082003"
            "6A36028008410021040B2000410F360204200020043602000F0B20052006108E80"
            "808000000BC50503037F027E017F23808080800041D0006B220324808080800002"
            "4002400240024020012802000E03010002010B200341286A2002200141086A4108"
            "108980808000410F2104200328022822050D02200341206A2002200141246A4114"
            "108980808000200328022022050D02200341186A2002200141106A411410898080"
            "8000200328021821050C020B0240024020012903082206427F550D002003420020"
            "067D22074280FE038342288620064238867D2007428080FC078342188620074280"
            "8080F80F834208868484200742088842808080F80F832007421888428080FC0783"
            "8420074228884280FE038320074280808080808080803F83423888848484370348"
            "200341106A2002200341C8006A4108108980808000200328021021050C010B2003"
            "200642388620064280FE0383422886842006428080FC0783421886200642808080"
            "F80F834208868484200642088842808080F80F832006421888428080FC07838420"
            "064228884280FE03832006428080808080808080C0008442388884848437034820"
            "0341086A2002200341C8006A4108108980808000200328020821050B410F21040C"
            "010B410F210441BD81C080002105200228028008220841FF074B0D00200220086A"
            "41E000412020012D00101B3A0000200220022802800841016A3602800820032001"
            "290308220642388620064280FE0383422886842006428080FC0783421886200642"
            "808080F80F834208868484200642088842808080F80F832006421888428080FC07"
            "838420064228884280FE03832006423888848484370348200341C0006A20022003"
            "41C8006A4108108980808000200328024022050D00200320012800113602482003"
            "41386A2002200341C8006A4104108980808000200328023822050D00200341306A"
            "2002200141156A4114108980808000200328023022050D00410021050B20002004"
            "36020420002005360200200341D0006A2480808080000B900101037F2380808080"
            "0041106B220224808080800041BD81C0800021030240200128028008220441FF07"
            "4B0D00200120046A4181013A0000200120012802800841016A2204360280082004"
            "41FF074B0D00200120046A41143A0000200120012802800841016A360280082002"
            "41086A200120004114108980808000200228020821030B200241106A2480808080"
            "0020030B0300000B0900108C80808000000B0900108C80808000000B0BD6010100"
            "418080C0000BCC016572726F725F636F64653D2424242424205354415254494E47"
            "205741534D20455845435554494F4E20242424242420204163636F756E743A2020"
            "436F6E7472616374204163636F756E743A20205369676E696E675075624B65793A"
            "AE123A8556F3CF91154711376AFB0F894F832B3D4E6578742053657175656E6365"
            "3A53657269616C697A6564205061796D656E7454786E3A5375626D697420526573"
            "756C743A53657269616C697A6174696F6E206572726F72206F6363757272656442"
            "7566666572206F766572666C6F7700F907046E616D65000C0B7375626D69742E77"
            "61736D01C3070F002B5F5A4E387872706C5F73746434686F737435747261636531"
            "3768376332613165636536303664316537664501335F5A4E387872706C5F737464"
            "34686F737431326765745F74785F6669656C643137683663363966616632346533"
            "33613833344502355F5A4E387872706C5F73746434686F737431346163636F756E"
            "745F6B65796C6574313768656630663037663934356462643061394503375F5A4E"
            "387872706C5F73746434686F7374313663616368655F6C65646765725F6F626A31"
            "37686230363237363231333235316131396645043B5F5A4E387872706C5F737464"
            "34686F737432306765745F6C65646765725F6F626A5F6669656C64313768316436"
            "3130653264326437656237396345052F5F5A4E387872706C5F73746434686F7374"
            "3974726163655F6E756D3137686239666661343664323065373166386345062E5F"
            "5A4E387872706C5F73746434686F737438656D69745F74786E3137683963343635"
            "3664303338663939333230450704656D6974089F015F5A4E3132385F244C542478"
            "72706C5F7374642E2E636F72652E2E74797065732E2E616D6F756E742E2E746F6B"
            "656E5F616D6F756E742E2E546F6B656E416D6F756E742475323024617324753230"
            "24636F72652E2E636F6E766572742E2E46726F6D244C5424247535622475382475"
            "33622424753230243438247535642424475424244754243466726F6D3137683830"
            "336432333365336235306261323945095B5F5A4E387872706C5F73746434636F72"
            "65367375626D697436636F6D6D6F6E313953657269616C697A6174696F6E427566"
            "6665723137657874656E645F66726F6D5F736C6963653137683030656564393937"
            "6164653165326564450A455F5A4E387872706C5F73746434636F7265367375626D"
            "697436636F6D6D6F6E313673657269616C697A655F616D6F756E74313768653034"
            "30323963303564663866306338450B4C5F5A4E387872706C5F73746434636F7265"
            "367375626D697436636F6D6D6F6E323373657269616C697A655F6163636F756E74"
            "5F6669656C6431376864396465343632323665323639633731450C305F5A4E3463"
            "6F72653970616E69636B696E673970616E69635F666D7431376834316366656437"
            "396232646462663133450D425F5A4E34636F726535736C69636535696E64657832"
            "34736C6963655F656E645F696E6465785F6C656E5F6661696C3137686164666263"
            "3765316131353937346131450E405F5A4E34636F726535736C69636535696E6465"
            "783232736C6963655F696E6465785F6F726465725F6661696C3137683837663334"
            "613939663330363339366545071201000F5F5F737461636B5F706F696E74657209"
            "0A0100072E726F64617461004D0970726F64756365727302086C616E6775616765"
            "010452757374000C70726F6365737365642D6279010572757374631D312E38382E"
            "30202836623030626333383820323032352D30362D3233290094010F7461726765"
            "745F6665617475726573082B0B62756C6B2D6D656D6F72792B0F62756C6B2D6D65"
            "6D6F72792D6F70742B1663616C6C2D696E6469726563742D6F7665726C6F6E672B"
            "0A6D756C746976616C75652B0F6D757461626C652D676C6F62616C732B136E6F6E"
            "7472617070696E672D6670746F696E742B0F7265666572656E63652D7479706573"
            "2B087369676E2D657874";

        env(contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "value", "AMOUNT", XRP(2000)),
            contract::add_function("emit", {}),
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

        auto const contractAccount = getContractOwner(env);
        env(contract::call(alice, contractAccount, "emit"),
            escrow::comp_allowance(1000000),
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

        // std::string contractWasmStr = loadContractWasmStr("events");
        std::string contractWasmStr =
            "0061736D0100000001200660047F7F7F7F017F6000017F60017F0060027F7F0060"
            "000060037F7F7F017F02170108686F73745F6C69620A656D69745F6576656E7400"
            "00030605010203040505030100110619037F01418080C0000B7F00419B80C0000B"
            "7F0041A080C0000B072E04066D656D6F72790200066576656E747300010A5F5F64"
            "6174615F656E6403010B5F5F686561705F6261736503020AC91705E31602127F01"
            "7E23808080800041F0EC036B220024808080800041002101024041C000450D0020"
            "0041BFCC026A410041C000FC0B000B200041086A41027221020340200041A0A401"
            "6A20016A220341003A0000024041C704450D00200341016A200041B8C8026A41C7"
            "04FC0A00000B200341C8046A41003A0000200141D0046A220141809401470D000B"
            "0240418010450D00200041B8C8026A4100418010FC0B000B20002D00A1A8012101"
            "20002802A4A80121030240413E450D00200041C2DC026A200041AAA8016A413EFC"
            "0A00000B024041B78F01450D0020004181DD026A200041E9A8016A41B78F01FC0A"
            "00000B200041B1E4CCA1033600B9D802200041093A00B8D802024041FC03450D00"
            "200041BDD8026A410041FC03FC0B000B20004194E1026A41002F008480C080003B"
            "0100200041023B01B8EC03200041023A0080DD02200041E9C8013B01C0DC022000"
            "20033602BCDC02200041043B01BADC02200020013A00B9DC02200041063A00D0E1"
            "02200041882736028CDD022000410028008080C08000360290E102200041033A00"
            "88DD0202404198A401450D00200041A0A4016A200041B8C8026A4198A401FC0A00"
            "000B024041F703450D00200041B8C8026A41096A410041F703FC0B000B200041C0"
            "C8026A41002D009480C080003A00002000410029008C80C080003703B8C8020240"
            "02400240024002400240024020002D00A0C802220441214F0D00200441D0046C21"
            "050240024002402004450D00200041A0B4016A21012005210303400240200141C8"
            "046A2D00004106470D0020014188046A418680C080004106108580808000450D03"
            "0B200141D0046A2101200341B07B6A22030D000B20044120460D020B200041A0A4"
            "016A20056A220141063A00C814200141093A0080102001418C146A41002F008A80"
            "C080003B00002001410028008680C08000360088140240418004450D0020014181"
            "106A200041B8C8026A418004FC0A00000B200141093B018214200020002D00A0C8"
            "0241016A3A00A0C8020C010B200141093A00000240418004450D00200141016A20"
            "0041B8C8026A418004FC0A00000B200141093B0182040B02404198A401450D0020"
            "0041086A200041A0A4016A4198A401FC0A00000B20002D0088A4012106200041E8"
            "EC036A4200370300200041E0EC036A4200370300200041D8EC036A420037030020"
            "0042003703D0EC032006450D03200041086A410372210741002101034020014120"
            "460D02200041D0EC036A20016A20013A00002006200141016A2201470D000B2006"
            "4101460D0220004188106A2108410021094101210A0340200A220B41016A210A20"
            "0B412049210C2009210402400240034002400240200C450D00200041D0EC036A20"
            "046A220D41016A220E2D000022054120490D012005108280808000000B200B1082"
            "80808000000B02402008200541D0046C6A220F2D00C804220141C1004F0D000240"
            "0240200D2D0000221041204F0D002008201041D0046C6A22112D00C804220341C1"
            "004F0D04200F4188046A20114188046A2001200320012003491B10858080800022"
            "0F200120036B200F1B417F4C0D010C050B2010108280808000000B200D20053A00"
            "00200E20103A00002004417F6A2204417F470D010C030B0B200141C00010838080"
            "8000000B200341C000108380808000000B200941016A2109200A2006470D000C03"
            "0B0B20044120108380808000000B4120108280808000000B410021010240418010"
            "450D00200041B8C8026A4100418010FC0B000B200041086A4180106A2111200041"
            "B8C8026A41016A210E410021040240024003400240024020044120460D00200041"
            "D0EC036A20046A2D000022034120490D012003108280808000000B412010828080"
            "8000000B02402011200341D0046C6A22052D00C804220341C1004F0D0020054101"
            "6A210D20052D000021080240418704450D00200041A0A4016A200D418704FC0A00"
            "000B200141FF0F4B0D02200041B8C8026A20016A20033A000002402003450D0020"
            "0E20016A20054188046A2003FC0A00000B200120036A41016A220141FF0F4B0D02"
            "200141FF0F460D02200141016A2105200041B8C8026A20016A21104171210F0240"
            "024002400240024002400240024002400240024002400240024002400240024002"
            "4020080E0E190F000102030405060708090A0B190B200041B8C8026A20056A2201"
            "41013A0000200120002F00A1A40122034108742003410876723B0001410321030C"
            "0F0B200041B8C8026A20056A220341023A0000200320002800A3A4012201411874"
            "20014180FE03714108747220014108764180FE0371200141187672723600014105"
            "21030C0E0B200041B8C8026A20056A220141033A0000200120002900A7A4012212"
            "42388620124280FE0383422886842012428080FC0783421886201242808080F80F"
            "834208868484201242088842808080F80F832012421888428080FC078384201242"
            "28884280FE03832012423888848484370001410921030C0D0B200041B8C8026A20"
            "056A220141043A00002001200D290000370001200141096A200D41086A29000037"
            "0000411121030C0C0B200041B8C8026A20056A220141113A00002001200D290000"
            "370001200141096A200D41086A290000370000200141116A200D41106A28000036"
            "0000411521030C0B0B200041B8C8026A20056A220141053A00002001200D290000"
            "370001200141096A200D41086A290000370000200141116A200D41106A29000037"
            "0000200141196A200D41186A290000370000412121030C0A0B200041B8C8026A20"
            "056A220141063A0000200120002903A0A401370001410921030C090B200041B8C8"
            "026A20056A220841073A0000200541FF0F460D0C200141026A210D20002F00A1A8"
            "01220341C101490D04200041B8C8026A200D6A210F0240200341C1E100490D0020"
            "0F200341BF9E7F6A220D41107641716A3A0000200141FB0F4B0D0D200F200D4108"
            "74200D4180FE0371410876723B00014104210D0C060B200F200341BF7E6A220C41"
            "087641416A3A0000200D41FF0F460D0C200F200C3A00014103210D0C050B200041"
            "B8C8026A20056A22014188283B00002001200D2900003700022001410A6A200D41"
            "086A290000370000200141126A200D41106A280000360000411621030C070B2000"
            "41B8C8026A20056A2201411A3A00002001200D290000370001200141096A200D41"
            "086A290000370000200141116A200D41106A280000360000411521030C060B2000"
            "41B8C8026A20056A220D410E3A0000024020002F00A1A8012203450D00200D4101"
            "6A200041A0A4016A2003FC0A00000B200341016A21030C030B200041B8C8026A20"
            "056A220D410F3A0000024020002F00A1A8012203450D00200D41016A200041A0A4"
            "016A2003FC0A00000B200341016A21030C020B200041B8C8026A200D6A20033A00"
            "004102210D0B02402003450D002008200D6A200041A0A4016A2003FC0A00000B20"
            "0D20036A21030B200341C101490D010240200341C1E100490D00201041F1013A00"
            "00417D210F200141FD0F4B0D0B2010200341BF9E7F6A220141087420014180FE03"
            "71410876723B00010C030B2010200341BF7E6A22013A0001201020014108764141"
            "6A3A00000C020B200041B8C8026A20056A220141103A0000200120002D00A0A401"
            "3A0001410221030B201020033A00000B200320056A21012006200441016A220446"
            "0D030C010B0B200341C000108380808000000B417D210F0C040B200141C101490D"
            "010240200141C0E1004B0D002000200141BF7E6A22033A00092000200341087641"
            "416A3A0008410221030C030B200041F1013A00082000200141BF9E7F6A22033A00"
            "0A200020034108763A000941032103200721020C020B41002101418010450D0020"
            "0041B8C8026A4100418010FC0B000B200041086A4101722102200020013A000841"
            "0121030B02402001450D002002200041B8C8026A2001FC0A00000B419580C08000"
            "4106200041086A200320016A1080808080001A4100210F0B200041F0EC036A2480"
            "80808000200F0B0900108480808000000B0900108480808000000B0300000B4A01"
            "037F4100210302402002450D000240034020002D0000220420012D00002205470D"
            "01200041016A2100200141016A21012002417F6A2202450D020C000B0B20042005"
            "6B21030B20030B0B240100418080C0000B1B616D6F756E74737461747573636F6D"
            "706C657465646576656E743100AC02046E616D65000C0B6576656E74732E776173"
            "6D01F6010600315F5A4E387872706C5F73746434686F73743130656D69745F6576"
            "656E74313768313064636136633663613636636436664501066576656E7473023A"
            "5F5A4E34636F72653970616E69636B696E67313870616E69635F626F756E64735F"
            "636865636B313768376130333738323462396336643630664503425F5A4E34636F"
            "726535736C69636535696E6465783234736C6963655F656E645F696E6465785F6C"
            "656E5F6661696C313768616466626337653161313539373461314504305F5A4E34"
            "636F72653970616E69636B696E673970616E69635F666D74313768343163666564"
            "373962326464626631334505066D656D636D70071201000F5F5F737461636B5F70"
            "6F696E746572090A0100072E726F64617461004D0970726F64756365727302086C"
            "616E6775616765010452757374000C70726F6365737365642D6279010572757374"
            "631D312E38382E30202836623030626333383820323032352D30362D3233290094"
            "010F7461726765745F6665617475726573082B0B62756C6B2D6D656D6F72792B0F"
            "62756C6B2D6D656D6F72792D6F70742B1663616C6C2D696E6469726563742D6F76"
            "65726C6F6E672B0A6D756C746976616C75652B0F6D757461626C652D676C6F6261"
            "6C732B136E6F6E7472617070696E672D6670746F696E742B0F7265666572656E63"
            "652D74797065732B087369676E2D657874";

        env(contract::create(alice, contractWasmStr),
            contract::add_instance_param(
                tfSendAmount, "amount", "AMOUNT", XRP(2000)),
            contract::add_function("events", {}),
            fee(XRP(200)),
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

        auto const contractAccount = getContractOwner(env);
        env(contract::call(alice, contractAccount, "events"),
            escrow::comp_allowance(1000000),
            ter(tesSUCCESS));
        env.close();

        {
            // Get contract info
            Json::Value params;
            params[jss::contract_account] = contractAccount;
            params[jss::account] = alice.human();
            auto const jrr =
                env.rpc("json", "contract_info", to_string(params));
            std::cout << jrr << std::endl;
        }

        // Check stream update
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
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
        testContractData(features);
        testContractDataV2(features);
        testParameters(features);
        testSubmit(features);
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
