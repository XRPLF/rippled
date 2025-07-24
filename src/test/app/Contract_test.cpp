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
    contractKeyAndSle(ReadView const& view, AccountID const& account)
    {
        auto const sle = view.read(keylet::account(account));
        if (!sle)
            return {};
        auto const k = keylet::contract(account);
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

    Json::Value
    addFuncParam(std::string const& name, std::string const& typeName)
    {
        Json::Value param = Json::Value(Json::objectValue);
        param[sfInstanceParameter][sfParameterName] = strHex(name);
        param[sfInstanceParameter][sfParameterType] = typeName;
        return param;
    };

    Json::Value
    payContract(
        AccountID const& account,
        std::string const& to,
        jtx::AnyAmount amount)
    {
        Json::Value jv;
        jv[jss::Account] = to_string(account);
        jv[jss::Amount] = amount.value.getJson(JsonOptions::none);
        jv[jss::Destination] = to;
        jv[jss::TransactionType] = jss::Payment;
        return jv;
    }

    // template <typename T>
    // Json::Value
    // addFuncParamValue(std::string const& typeName, T value)
    // {
    //     Json::Value param = Json::Value(Json::objectValue);
    //     param[sfInstanceParameter][sfParameterValue][jss::type] = typeName;
    //     param[sfInstanceParameter][sfParameterValue][jss::value] = value;
    //     return param;
    // };

    // void
    // testEnabled(FeatureBitset features)
    // {
    //     testcase("enabled");

    //     using namespace jtx;
    //     Account const alice{"alice"};
    //     Account const bob{"bob"};
    //     // Env env{*this, envconfig(), nullptr, beast::severities::kTrace};
    //     Env env{*this, features};
    //     env.fund(XRP(10000), alice, bob);
    //     env.close();

    //     env(contract::create(
    //             alice,
    //             "0061736d0100000001150360057f7f7f7f7f017f60037f7f7f017f6000017f"
    //             "02120108686f73745f6c696205747261636500000303020102050301001106"
    //             "19037f01418080c0000b7f0041c781c0000b7f0041d081c0000b072e04066d"
    //             "656d6f727902000666696e69736800020a5f5f646174615f656e6403010b5f"
    //             "5f686561705f6261736503020c01010aa506024d01027f0240200028028008"
    //             "220320026a22044180084d047f200320044b0d0120020440200020036a2001"
    //             "2002fc0a00000b200020002802800820026a36028008410005418080c0000b"
    //             "0f0b000bd40502057f027e230041a0096b22002400418f80c0004123410041"
    //             "00410010001a4101410041004100410010001a2000410f6a220241b280c000"
    //             "4121fc0a000041d380c000411020024121410110001a200041406b41f380c0"
    //             "002800002201360200200041386a41eb80c0002900002205370300200041e3"
    //             "80c000290000220637033041f780c000410a200041306a4114410110001a20"
    //             "0041dc006a2001360200200041d4006a20053702002000200637024c200041"
    //             "e2006a220320024121fc0a00002000418c016a418981c00029000037020020"
    //             "004194016a419181c000280000360200200041003b01602000410136024820"
    //             "00418181c0002900003702840120004198016a22024100418008fc0b002000"
    //             "410136029809200041123a009801200041003b019c09024002400240024020"
    //             "022000419c096a2201410210010d00200028029809220441ff074b0d002002"
    //             "20046a41243a0000200020002802980941016a360298092000418080800836"
    //             "029c0920022001410410010d00200028029809220141ff074b0d0020012002"
    //             "6a41f3003a0000200020002802980941016a220136029809200141ff074b0d"
    //             "00200120026a41213a0000200020002802980941016a360298092002200341"
    //             "2110010d00200028029809220141ff074b0d00200120026a4181013a000020"
    //             "0020002802980941016a220136029809200141ff074b0d0020004198016a20"
    //             "016a41143a0000200020002802980941016a360298092002200041cc006a41"
    //             "1410010d00200028029809220141ff074b0d0020004198016a20016a418301"
    //             "3a0000200020002802980941016a220136029809200141ff074b0d00200041"
    //             "98016a20016a41143a0000200020002802980941016a360298092002200041"
    //             "84016a41141001450d010b41ab81c000411c41004100410010001a0c010b20"
    //             "002802980922024181084f0d01419581c000411620004198016a2002410110"
    //             "001a0b200041a0096a240041010f0b000b0bd1010100418080c0000bc70142"
    //             "7566666572206f766572666c6f772424242424205354415254494e47205741"
    //             "534d20455845435554494f4e202424242424edc378ba29574532509b818378"
    //             "fa2bfbdc3c7c3df17dad5d3687bc6ceff8ae170120205369676e696e675075"
    //             "624b65793a162825f91ce674241ca62ca3f05b7e70d2109a4220204163636f"
    //             "756e743af51dfc2a09d62cbba1dfbdd4691dac96ad98b90f53657269616c69"
    //             "7a6564205061796d656e7454786e3a53657269616c697a6174696f6e206572"
    //             "726f72206f63637572726564004d0970726f64756365727302086c616e6775"
    //             "616765010452757374000c70726f6365737365642d6279010572757374631d"
    //             "312e38382e30202836623030626333383820323032352d30362d323329007c"
    //             "0f7461726765745f6665617475726573072b0f6d757461626c652d676c6f62"
    //             "616c732b136e6f6e7472617070696e672d6670746f696e742b0b62756c6b2d"
    //             "6d656d6f72792b087369676e2d6578742b0f7265666572656e63652d747970"
    //             "65732b0a6d756c746976616c75652b0f62756c6b2d6d656d6f72792d6f707"
    //             "4"),
    //         contract::add_function("finish", {{"owner", "ACCOUNT"}}),
    //         fee(XRP(200)),
    //         ter(tesSUCCESS));
    //     env.close();

    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }

    //     auto const contractAccount = getContractOwner(env);
    //     env(contract::call(alice, contractAccount, "finish"),
    //         escrow::comp_allowance(1000000),
    //         ter(tesSUCCESS));
    //     env.close();
    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }
    // }

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

    // template <typename... Args>
    // std::pair<std::vector<std::string>, std::string>
    // submitCreate(jtx::Env& env, TER const& result, Args&&... args)
    // {
    //     auto txn = env.jt(std::forward<Args>(args)...);
    //     env(txn, jtx::ter(result));

    //     auto const ids = txn.stx->getBatchTransactionIDs();
    //     std::vector<std::string> txIDs;
    //     for (auto const& id : ids)
    //         txIDs.push_back(strHex(id));
    //     TxID const batchID = batchTxn.stx->getTransactionID();
    //     return std::make_pair(txIDs, strHex(batchID));
    // }

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

            env(contract::create(alice, BaseContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            auto const contractAccount = getContractOwner(env);
            auto const accountID = parseBase58<AccountID>(contractAccount);
            auto const [contractId, contractSle] =
                contractKeyAndSle(*env.current(), *accountID);
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = ripple::sha512Half_s(
                ripple::Slice(wasmBytes->data(), wasmBytes->size()));

            // validate contract
            BEAST_EXPECT(contractSle);
            BEAST_EXPECT(
                contractSle->getFieldH256(sfContractHash) == contractHash);
            BEAST_EXPECT(contractSle->getAccountID(sfAccount) == alice.id());
            BEAST_EXPECT(contractSle->getAccountID(sfOwner) == accountID);

            // validate contract source
            auto const [contractSourceId, contractSourceSle] =
                contractSourceKeyAndSle(*env.current(), contractHash);
            BEAST_EXPECT(contractSourceSle);
            BEAST_EXPECT(
                contractSourceSle->getFieldVL(sfContractCode) == wasmBytes);
            BEAST_EXPECT(
                contractSourceSle->getFieldH256(sfContractHash) ==
                contractHash);
            BEAST_EXPECT(
                contractSourceSle->getFieldArray(sfFunctions).size() == 1);
            BEAST_EXPECT(contractSourceSle->getFieldU64(sfReferenceCount) == 1);

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

            auto const contractAccount = getContractOwner(env);
            auto const accountID = parseBase58<AccountID>(contractAccount);
            auto const [contractId, contractSle] =
                contractKeyAndSle(*env.current(), *accountID);
            auto const wasmBytes = strUnHex(BaseContractWasm);
            uint256 const contractHash = ripple::sha512Half_s(
                ripple::Slice(wasmBytes->data(), wasmBytes->size()));

            {
                // validate contract
                BEAST_EXPECT(contractSle);
                BEAST_EXPECT(
                    contractSle->getFieldH256(sfContractHash) == contractHash);
                BEAST_EXPECT(
                    contractSle->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(contractSle->getAccountID(sfOwner) == accountID);
                // BEAST_EXPECT(contractSle->getFieldVL(sfURI) ==
                // Blob(uri.begin(), uri.end()));

                // validate contract source
                auto const [contractSourceId, contractSourceSle] =
                    contractSourceKeyAndSle(*env.current(), contractHash);
                BEAST_EXPECT(contractSourceSle);
                BEAST_EXPECT(
                    contractSourceSle->getFieldVL(sfContractCode) == wasmBytes);
                BEAST_EXPECT(
                    contractSourceSle->getFieldH256(sfContractHash) ==
                    contractHash);
                BEAST_EXPECT(
                    contractSourceSle->getFieldArray(sfFunctions).size() == 1);
                BEAST_EXPECT(
                    contractSourceSle->getFieldU64(sfReferenceCount) == 1);
            }

            env(contract::create(alice, contractHash),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            {
                auto const contractAccount = getContractOwner(env);
                auto const accountID = parseBase58<AccountID>(contractAccount);
                auto const [contractId, contractSle] =
                    contractKeyAndSle(*env.current(), *accountID);

                // validate contract
                BEAST_EXPECT(contractSle);
                BEAST_EXPECT(
                    contractSle->getFieldH256(sfContractHash) == contractHash);
                BEAST_EXPECT(
                    contractSle->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(contractSle->getAccountID(sfOwner) == accountID);

                // validate contract source
                auto const [contractSourceId, contractSourceSle] =
                    contractSourceKeyAndSle(*env.current(), contractHash);
                BEAST_EXPECT(contractSourceSle);
                BEAST_EXPECT(
                    contractSourceSle->getFieldVL(sfContractCode) == wasmBytes);
                BEAST_EXPECT(
                    contractSourceSle->getFieldH256(sfContractHash) ==
                    contractHash);
                BEAST_EXPECT(
                    contractSourceSle->getFieldArray(sfFunctions).size() == 1);
                BEAST_EXPECT(
                    contractSourceSle->getFieldU64(sfReferenceCount) == 2);
            }

            {
                Json::Value params;
                params[jss::ledger_index] = env.current()->seq() - 1;
                params[jss::transactions] = true;
                params[jss::expand] = true;
                auto const jrr = env.rpc("json", "ledger", to_string(params));
                std::cout << jrr << std::endl;
            }
            // {
            //     Json::Value params;
            //     params[jss::account] = contractAccount;
            //     auto const jrr = env.rpc("json", "account_objects",
            //     to_string(params)); std::cout << jrr << std::endl;
            // }
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

            env(contract::create(alice, Base2ContractWasm),
                contract::add_instance_param(0, "uint8", "UINT8", 1),
                contract::add_function("base", {{0, "uint8", "UINT8"}}),
                fee(XRP(200)),
                ter(tesSUCCESS));
            env.close();

            auto const contractAccount = getContractOwner(env);
            auto const accountID = parseBase58<AccountID>(contractAccount);
            auto const [contractId, contractSle] =
                contractKeyAndSle(*env.current(), *accountID);
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
    testBase(FeatureBitset features)
    {
        testcase("base");

        using namespace jtx;

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // std::string contractWasmStr = loadContractWasmStr("base");
        std::string contractWasmStr =
            "0061736D01000000010E0260057F7F7F7F7F017F6000017F02120108686F73745F"
            "6C696205747261636500000302010105030100110619037F01418080C0000B7F00"
            "419E80C0000B7F0041A080C0000B072C04066D656D6F7279020004626173650001"
            "0A5F5F646174615F656E6403010B5F5F686561705F6261736503020A6C016A0101"
            "7F23808080800041206B2200248080808000200041186A410028009080C0800036"
            "0200200041106A410029008880C080003703002000410029008080C08000370308"
            "419480C08000410A200041086A411441011080808080001A200041206A24808080"
            "800041000B0B270100418080C0000B1EAE123A8556F3CF91154711376AFB0F894F"
            "832B3D20204163636F756E743A";

        env(contract::create(alice, contractWasmStr),
            contract::add_function("base", {}),
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
        env(contract::call(alice, contractAccount, "base"),
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

    // void
    // testContractData(FeatureBitset features)
    // {
    //     testcase("contract data");

    //     using namespace jtx;

    //     test::jtx::Env env{*this, features};

    //     auto const alice = Account{"alice"};
    //     auto const bob = Account{"bob"};
    //     env.fund(XRP(10'000), alice, bob);
    //     env.close();

    //     // std::string contractWasmStr =
    //     loadContractWasmStr("contract_data"); std::string contractWasmStr =
    //         "0061736D0100000001450B60037F7F7E017F60057F7F7F7F7F017F60047F7F7F7F"
    //         "017F6000017F60017F0060037F7F7F0060047F7F7F7F0060027F7F0060037F7F7F"
    //         "017F60000060057F7F7F7F7F0002610408686F73745F6C69620974726163655F6E"
    //         "756D000008686F73745F6C6962057472616365000108686F73745F6C6962116765"
    //         "745F636F6E74726163745F64617461000208686F73745F6C6962117365745F636F"
    //         "6E74726163745F646174610002031D1C0304050607070305060506070809090707"
    //         "070A06050705060605080805030100110619037F01418080C0000B7F0041E982C0"
    //         "000B7F0041F082C0000B073705066D656D6F727902000663726561746500040675"
    //         "7064617465000A0A5F5F646174615F656E6403010B5F5F686561705F6261736503"
    //         "020AFB3C1CA90201027F23808080800041B0A8016B220024808080800020004110"
    //         "6A108580808000200041106A419480C080004103108680808000200041106A4199"
    //         "80C08000410C108680808000200041B9A4016A41002800B980C080003600002000"
    //         "41B1A4016A41002900B180C08000370000200041002900A980C080003700A9A401"
    //         "2000410A3A00A8A401200041106A419E80C08000410B200041A8A4016A10878080"
    //         "8000200041086A200041106A108880808000024002402000280208410171450D00"
    //         "200028020C21010C010B2000200041106A10898080800002402000280200410171"
    //         "450D0041BD80C0800041132000280204AD1080808080001A410021010C010B41D0"
    //         "80C0800041194100410041001081808080001A417F21010B200041B0A8016A2480"
    //         "8080800020010BFF0101037F23808080800041D098016B22012480808080004100"
    //         "2102024041C000450D002001419098016A410041C000FC0B000B02400340200241"
    //         "809401460D01200141086A20026A220341003A0000024041C704450D0020034101"
    //         "6A2001418994016A41C704FC0A00000B200341C8046A4100360200200241D0046A"
    //         "21020C000B0B0240418010450D002000418094016A4100418010FC0B000B200041"
    //         "90A4016A410028009080C0800036000020004188A4016A410029008880C0800037"
    //         "00002000410029008080C08000370080A401024041809401450D00200020014108"
    //         "6A41809401FC0A00000B20004100360294A401200141D098016A2480808080000B"
    //         "4101017F2380808080004190046B2203248080808000200341033A000820032002"
    //         "36020C200020014105200341086A10878080800020034190046A2480808080000B"
    //         "DA0101057F2000280294A401210441002105200021060240024002400240034020"
    //         "042005460D01200541016A22054121460D03200620012002109E80808000210720"
    //         "0641D0046A220821062007450D000B418804450D01200841B07B6A2003418804FC"
    //         "0A00000F0B2004411F4B0D002000200441D0046C6A200120021090808080004101"
    //         "710D002000280294A4012205411F4B0D020240418804450D002000200541D0046C"
    //         "6A2003418804FC0A00000B20002000280294A40141016A360294A4010B0F0B4120"
    //         "4120108F80808000000B20054120108F80808000000BBD11020E7F017E23808080"
    //         "80004190076B2202248080808000410021030240418001450D00200241C0016A41"
    //         "00418001FC0B000B2001280294A4012104200241C0016A21050340024020042003"
    //         "470D00200241C0016A41046A210641002107034002400240024002400240024002"
    //         "4020072004460D00200741016A2108200241C0016A20074102746A21092007411F"
    //         "4B210A20062105200721030340200341016A220B20044F0D07200A0D0220092802"
    //         "00220C41204F0D03200241B8016A2001200C41D0046C6A1099808080002003411F"
    //         "4F0D042005280200220D41204F0D0520022802BC01210320022802B801210E2002"
    //         "41B0016A2001200D41D0046C6A1099808080000240200E20022802B00120032002"
    //         "2802B401220F2003200F491B109F80808000220E2003200F6B200E1B41004C0D00"
    //         "2009200D3602002005200C3602000B200541046A2105200B21030C000B0B200141"
    //         "8094016A210D20024180036A410172210920024180036A41086A210A4100210C41"
    //         "002103024002400240024003402004450D01200C418001460D03200241C0016A20"
    //         "0C6A2802002105024041C000450D00200241C0026A410041C000FC0B000B200541"
    //         "204F0D04200241A8016A200241C0026A2001200541D0046C6A220B2802C8042205"
    //         "10988080800020022802AC01210F20022802A801210E200241A0016A200B418804"
    //         "6A2005109A80808000200E200F20022802A00120022802A401108E808080002002"
    //         "4198016A200241C0026A2005109A80808000200228029801210E200228029C0121"
    //         "0F0240418804450D0020024180036A200B418804FC0A00000B20024190016A2001"
    //         "2003200F109B808080002002280294012105024002402002280290014101710D00"
    //         "20024188016A200D200520036A22032003200F6A2203109C808080002002280288"
    //         "01200228028C01200E200F108E80808000417D2105200341FF0F4B0D00200D2003"
    //         "6A41003A0000200341FF0F460D00200341016A210F417121050240024002400240"
    //         "024002400240024002400240024002400240024020022D0080030E0E0E01020304"
    //         "0506070800090A0B0C0E0B200D200F6A41073A0000200241D0006A200120034102"
    //         "6A200228028403220B109B808080002002280254210520022802504101710D0D20"
    //         "0241C8006A200D200541016A2205200F6A220E200E200B6A109C80808000200228"
    //         "024C210E20022802482107200241C0006A200A200B109D808080002007200E2002"
    //         "2802402002280244108E808080002005200B6A21050C0C0B200D200F6A22054110"
    //         "3A00000240200F41FF0F460D00200520022D0081033A0001410221050C0C0B4180"
    //         "10418010108F80808000000B200D200F6A41013A0000200220022F018203220541"
    //         "08742005410876723B018807200241086A200D200341026A200341046A109C8080"
    //         "80002002280208200228020C20024188076A4102108E80808000410321050C0A0B"
    //         "200D200F6A41023A00002002200228028403220541187420054180FE0371410874"
    //         "7220054108764180FE03712005411876727236028807200241106A200D20034102"
    //         "6A200341066A109C808080002002280210200228021420024188076A4104108E80"
    //         "808000410521050C090B200D200F6A41033A000020022002290388032210423886"
    //         "20104280FE0383422886842010428080FC0783421886201042808080F80F834208"
    //         "868484201042088842808080F80F832010421888428080FC078384201042288842"
    //         "80FE0383201042388884848437038807200241186A200D200341026A2003410A6A"
    //         "109C808080002002280218200228021C20024188076A4108108E80808000410921"
    //         "050C080B200D200F6A41043A0000200241206A200D200341026A200341126A109C"
    //         "808080002002280220200228022420094110108E80808000411121050C070B200D"
    //         "200F6A41113A0000200241286A200D200341026A200341166A109C808080002002"
    //         "280228200228022C20094114108E80808000411521050C060B200D200F6A41053A"
    //         "0000200241306A200D200341026A200341226A109C808080002002280230200228"
    //         "023420094120108E80808000412121050C050B200D200F6A41063A000020024138"
    //         "6A200D200341026A2003410A6A109C808080002002280238200228023C20094108"
    //         "108E80808000410921050C040B200D200F6A220541083A00000240200F41FF0F46"
    //         "0D00200541143A0001200241D8006A200D200341036A200341176A109C80808000"
    //         "2002280258200228025C20094114108E80808000411621050C040B418010418010"
    //         "108F80808000000B200D200F6A2205411A3A00000240200F41FF0F460D00200541"
    //         "143A0001200241E0006A200D200341036A200341176A109C808080002002280260"
    //         "200228026420094114108E80808000411621050C030B418010418010108F808080"
    //         "00000B200D200F6A410E3A0000200241F0006A200D200341026A220B200B200228"
    //         "02840322056A109C808080002002280274210B2002280270210E200241E8006A20"
    //         "0A2005109D80808000200E200B2002280268200228026C108E8080800020054101"
    //         "6A21050C010B200D200F6A410F3A000020024180016A200D200341026A220B200B"
    //         "20022802840322056A109C80808000200228028401210B200228028001210E2002"
    //         "41F8006A200A2005109D80808000200E200B2002280278200228027C108E808080"
    //         "00200541016A21050B2002200120032005109B808080002002280200410171450D"
    //         "01200228020421050B410121030C030B2004417F6A2104200C41046A210C200520"
    //         "0F6A21030C000B0B20034181104F0D0720014180A4016A4114200D200310838080"
    //         "80001A410021030B200020053602042000200336020020024190076A2480808080"
    //         "000F0B41204120108F80808000000B20054120108F80808000000B20074120108F"
    //         "80808000000B200C4120108F80808000000B200B4120108F80808000000B200D41"
    //         "20108F80808000000B2003418010109380808000000B200641046A210620082107"
    //         "0C000B0B024020034120460D0020052003360200200541046A2105200341016A21"
    //         "030C010B0B41204120108F80808000000B8C0101057F2001280294A40121024100"
    //         "210341002104024002400340024020022004470D000C020B200441016A22044121"
    //         "460D022001419480C080004105109E808080002105200141D0046A220621012005"
    //         "450D000B200641B07B6A22042D00004103470D0020042802042101410121030B20"
    //         "002001360204200020033602000F0B41204120108F80808000000BE11902107F01"
    //         "7E23808080800041B0AE016B220024808080800020004190016A10858080800002"
    //         "40024002400240024020004190A5016A41142000419095016A2201418010108280"
    //         "8080002202417F4C0D00200041003602A4A501200041E0A9016A41146A21032000"
    //         "41BDAA016A210441002105034002400240200520024F0D00200041B0AA016A2000"
    //         "4190016A2005108B8080800020002802B4AA01210620002802B0AA014101460D07"
    //         "20002802B8AA0120056A220720066A22084180104B0D06200041F8006A20012007"
    //         "2008108C8080800020002802782109200028027C220A450D014100200A41796A22"
    //         "072007200A4B1B210B200941036A417C7120096B210C4100210703400240024002"
    //         "400240200920076A2D00002205C0220D4100480D00200C20076B4103710D012007"
    //         "200B4F0D020340200920076A220541046A28020020052802007241808182847871"
    //         "0D03200741086A2207200B490D000C030B0B417521060240024002400240024002"
    //         "40024002400240200541E980C080006A2D0000417E6A0E03000201140B20074101"
    //         "6A2207200A4F0D13200920076A2C000041BF7F4C0D070C130B200741016A220E20"
    //         "0A4F0D122009200E6A2C0000210E200541907E6A0E050201010103010B20074101"
    //         "6A220E200A4F0D112009200E6A2C0000210E024002400240200541E001460D0020"
    //         "0541ED01460D01200D411F6A41FF0171410C490D02200D417E71416E470D14200E"
    //         "4140480D070C140B200E41607141A07F460D060C130B200E419F7F4A0D120C050B"
    //         "200E4140480D040C110B200D410F6A41FF017141024B0D10200E4140480D020C10"
    //         "0B200E41F0006A41FF01714130490D010C0F0B200E418F7F4A0D0E0B200741026A"
    //         "2205200A4F0D0D200920056A2C000041BF7F4A0D0D200741036A2207200A4F0D0D"
    //         "200920076A2C000041BF7F4A0D0D0C010B200741026A2207200A4F0D0C20092007"
    //         "6A2C000041BF7F4A0D0C0B200741016A21070C020B200741016A21070C010B2007"
    //         "200A4F0D000340200920076A2C00004100480D01200A200741016A2207470D000C"
    //         "040B0B2007200A490D000C020B0B20004190016A419480C0800041041086808080"
    //         "0020004188016A20004190016A1088808080000240200028028801410171450D00"
    //         "200028028C0121060C070B20004180016A20004190016A10898080800002402000"
    //         "28028001410171450D0041BD80C080004113200028028401AD1080808080001A41"
    //         "0021060C070B41D080C0800041194100410041001081808080001A417F21060C06"
    //         "0B200041B0AA016A20004190016A2008108B8080800020002802B4AA01210B0240"
    //         "20002802B0AA014101470D00200B21060C060B20002802B8AA0120086A2207200B"
    //         "6A22054180104B0D0441712106200B450D05200741FF0F4B0D05200B417F6A210B"
    //         "200741016A210D416F210602400240024002400240024002400240024002400240"
    //         "024002400240024002400240200120076A22082D0000417F6A0E1A030405060809"
    //         "0A0B16161616160001020716161616161616160C160B200B4180044D0D0C0C130B"
    //         "200B4180044B0D120240418004450D00200041B0AA016A4100418004FC0B000B20"
    //         "0041F0006A200041B0AA016A200B108D8080800020002802742107200028027021"
    //         "06200041E8006A2001200D2005108C80808000200620072000280268200028026C"
    //         "108E80808000200020002903B0AA013700A7A90120002D00B8AA01210620002800"
    //         "B9AA01210F024041F303450D00200041A8A5016A200441F303FC0A00000B200020"
    //         "0B3600A3A901410D210B0C0E0B200B4101470D130240200741FE0F4B0D00200020"
    //         "01200D6A2D00003A00A0A9014101210B0C0E0B418010418010108F80808000000B"
    //         "200B4102470D12200741FE0F4B0D0A0240200741FE0F460D0020002001200D6A2D"
    //         "000041087420082D0002723B00A1A9014102210B0C0D0B418010418010108F8080"
    //         "8000000B200B4104470D11200041003602B0AA01200041086A2001200D20074105"
    //         "6A108C80808000200041B0AA016A41042000280208200028020C108E8080800020"
    //         "0020002802B0AA01220741187420074180FE03714108747220074108764180FE03"
    //         "71200741187672723600A3A9014103210B0C0B0B200B4108470D10200042003703"
    //         "B0AA01200041106A2001200D200741096A108C80808000200041B0AA016A410820"
    //         "002802102000280214108E80808000200020002903B0AA01221042388620104280"
    //         "FE0383422886842010428080FC0783421886201042808080F80F83420886848420"
    //         "1042088842808080F80F832010421888428080FC07838420104228884280FE0383"
    //         "20104238888484843700A7A9014104210B0C0A0B200B4110470D0F200041B8A901"
    //         "6A41086A4200370300200042003703B8A901200041186A2001200D200741116A10"
    //         "8C80808000200041B8A9016A41102000280218200028021C108E80808000200020"
    //         "002900BFA9013700A7A901200020002903B8A9013703A0A9014105210B20002D00"
    //         "C7A90121060C090B200B4114470D0E200041C8A9016A41106A220B410036020020"
    //         "0041C8A9016A41086A4200370300200042003703C8A901200041206A2001200D20"
    //         "0741156A108C80808000200041C8A9016A411420002802202000280224108E8080"
    //         "8000200020002900CFA9013700A7A901200020002903C8A9013703A0A901200B28"
    //         "0200210F4106210B20002D00D7A90121060C080B200B4120470D0D200041E0A901"
    //         "6A41186A4200370300200041E0A9016A41106A220B4200370300200041E0A9016A"
    //         "41086A4200370300200042003703E0A901200041286A2001200D200741216A108C"
    //         "80808000200041E0A9016A41202000280228200028022C108E80808000200041A8"
    //         "A5016A41086A200341086A280000360200200020002900E7A9013700A7A9012000"
    //         "20002903E0A9013703A0A901200020032900003703A8A501200B280200210F4107"
    //         "210B20002D00EFA90121060C070B200B4108470D0C200042003703B0AA01200041"
    //         "306A2001200D200741096A108C808080004108210B200041B0AA016A4108200028"
    //         "02302000280234108E80808000200020002903B0AA013703A0A9010C060B200041"
    //         "B0AA016A20004190016A200D108B8080800020002802B4AA01210720002802B0AA"
    //         "010D084175210620074180044B0D0B20002802B8AA01220820076A200B4B0D0B02"
    //         "40418004450D00200041B0AA016A4100418004FC0B000B200041C0006A200041B0"
    //         "AA016A2007108D808080002000280244210B20002802402106200041386A200120"
    //         "08200D6A220D200D20076A108C808080002006200B2000280238200028023C108E"
    //         "80808000200020002903B0AA013700A7A90120002D00B8AA01210620002800B9AA"
    //         "01210F024041F303450D00200041A8A5016A200441F303FC0A00000B2000200736"
    //         "00A3A9014109210B0C050B200B4115470D0A200741FE0F4B0D032001200D6A2D00"
    //         "004114470D0A20004180AA016A41106A220B410036020020004180AA016A41086A"
    //         "420037030020004200370380AA01200041C8006A2001200741026A200741166A10"
    //         "8C8080800020004180AA016A41142000280248200028024C108E80808000200020"
    //         "00290087AA013700A7A90120002000290380AA013703A0A901200B280200210F41"
    //         "0A210B20002D008FAA0121060C040B200B4114470D0920004198AA016A41106A22"
    //         "0B410036020020004198AA016A41086A420037030020004200370398AA01200041"
    //         "D0006A2001200D200741156A108C8080800020004198AA016A4114200028025020"
    //         "00280254108E808080002000200029009FAA013700A7A90120002000290398AA01"
    //         "3703A0A901200B280200210F410B210B20002D00A7AA0121060C030B0240418004"
    //         "450D00200041B0AA016A4100418004FC0B000B200041E0006A200041B0AA016A20"
    //         "0B108D808080002000280264210720002802602106200041D8006A2001200D2005"
    //         "108C80808000200620072000280258200028025C108E80808000200020002903B0"
    //         "AA013700A7A90120002D00B8AA01210620002800B9AA01210F024041F303450D00"
    //         "200041A8A5016A200441F303FC0A00000B2000200B3600A3A901410C210B0C020B"
    //         "418010418010108F80808000000B418010418010108F80808000000B200041B4A9"
    //         "016A41026A220D20002D00A2A9013A0000200020002F01A0A9013B01B4A9010240"
    //         "20002802A4A5012207411F4D0D00417821060C060B20002800A3A9012108200029"
    //         "00A7A901211020004190016A200741D0046C6A2009200A1090808080004101710D"
    //         "03024020002802A4A501220741204F0D0020004190016A200741D0046C6A220720"
    //         "0B3A0000200720002F01B4A9013B00012007200D2D00003A00032007200F360011"
    //         "200720063A00102007201037030820072008360204024041F303450D0020074115"
    //         "6A200041A8A5016A41F303FC0A00000B200020002802A4A50141016A3602A4A501"
    //         "0C010B0B20074120108F80808000000B109180808000000B200721060C020B4174"
    //         "21060C010B417321060B200041B0AE016A24808080800020060B900101047F4101"
    //         "210341732104410421050240200241FF0F4B0D0002400240200120026A22062D00"
    //         "809401220141C101490D0041752104200241FF0F460D02200141F0014B0D022006"
    //         "418094016A2D00012001413F6A41FF01714108747241C1016A2101410221040C01"
    //         "0B410121040B2000200136020441002103410821050B200020056A200436020020"
    //         "0020033602000B43000240024020032002490D0020034180104B0D012000200320"
    //         "026B3602042000200120026A3602000F0B20022003109480808000000B20034180"
    //         "10109380808000000B4A01017F23808080800041106B2203248080808000200341"
    //         "086A20022001418004109780808000200328020C21012000200328020836020020"
    //         "002001360204200341106A2480808080000B2A00024020012003470D0002402001"
    //         "450D00200020022001FC0A00000B0F0B20012003109580808000000B0900109280"
    //         "808000000B6401027F23808080800041106B220324808080800041012104024020"
    //         "0241C0004B0D00200341086A20004188046A200210988080800020032802082003"
    //         "28020C20012002108E80808000200020023602C804410021040B200341106A2480"
    //         "8080800020040B0900109280808000000B0300000B0900109280808000000B0900"
    //         "109280808000000B0900109280808000000B40000240024020022001490D002002"
    //         "20044D0D0120022004109380808000000B20012002109480808000000B20002002"
    //         "20016B3602042000200320016A3602000B4B01017F23808080800041106B220424"
    //         "8080808000200441086A4100200120022003109680808000200428020C21032000"
    //         "200428020836020020002003360204200441106A2480808080000B4A01017F2380"
    //         "8080800041106B2203248080808000200341086A2002200141C000109780808000"
    //         "200328020C21012000200328020836020020002001360204200341106A24808080"
    //         "80000B4F01017F23808080800041106B2202248080808000200241086A20014188"
    //         "046A20012802C804109A80808000200228020C2101200020022802083602002000"
    //         "2001360204200241106A2480808080000B27000240200241C100490D00200241C0"
    //         "00109380808000000B20002002360204200020013602000BA30101017F41012104"
    //         "02400240200241FF0F4D0D00417D21020C010B0240200341C101490D0002402003"
    //         "41C0E1004D0D00417421020C020B200120026A2204200341BF7E6A220341087641"
    //         "416A3A008094010240200241FF0F460D002004418094016A20033A000141002104"
    //         "410221020C020B418010418010108F80808000000B200120026A20033A00809401"
    //         "41002104410121020B20002002360204200020043602000B4C01017F2380808080"
    //         "0041106B2204248080808000200441086A20022003200141801010968080800020"
    //         "0428020C21012000200428020836020020002001360204200441106A2480808080"
    //         "000B270002402002418104490D002002418004109380808000000B200020023602"
    //         "04200020013602000B6301027F23808080800041106B2203248080808000410021"
    //         "040240200220002802C804470D00200341086A20004188046A2002109A80808000"
    //         "200328020C2002470D00200328020820012002109F808080004521040B20034110"
    //         "6A24808080800020040B4A01037F4100210302402002450D000240034020002D00"
    //         "00220420012D00002205470D01200041016A2100200141016A21012002417F6A22"
    //         "02450D020C000B0B200420056B21030B20030B0BF3020100418080C0000BE902AE"
    //         "123A8556F3CF91154711376AFB0F894F832B3D636F756E74746F74616C64657374"
    //         "696E6174696F6E0596915CFDEEE3A695B3EFD6BDA9AC788A368B7B526561642062"
    //         "61636B20636F756E743A207B7D4661696C656420746F2072656164206261636B20"
    //         "636F756E7401010101010101010101010101010101010101010101010101010101"
    //         "010101010101010101010101010101010101010101010101010101010101010101"
    //         "010101010101010101010101010101010101010101010101010101010101010101"
    //         "010101010101010101010101010101010101010101010101010101010101010101"
    //         "010000000000000000000000000000000000000000000000000000000000000000"
    //         "000000000000000000000000000000000000000000000000000000000000000000"
    //         "000202020202020202020202020202020202020202020202020202020202020303"
    //         "030303030303030303030303030304040404040000000000000000000000";

    //     env(contract::create(alice, contractWasmStr),
    //         contract::add_instance_param(
    //             tfSendAmount, "value", "AMOUNT", XRP(2000)),
    //         contract::add_function("create", {}),
    //         contract::add_function("update", {}),
    //         fee(XRP(200)),
    //         ter(tesSUCCESS));
    //     env.close();

    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }

    //     auto const contractAccount = getContractOwner(env);
    //     env(contract::call(alice, contractAccount, "create"),
    //         escrow::comp_allowance(1000000),
    //         ter(tesSUCCESS));
    //     env.close();

    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }
    //     {
    //         // Get account objects
    //         Json::Value params;
    //         params[jss::account] = to_string(alice);
    //         auto const jrr =
    //             env.rpc("json", "account_objects", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }

    //     env(contract::call(alice, contractAccount, "update"),
    //         escrow::comp_allowance(1000000),
    //         ter(tesSUCCESS));
    //     env.close();
    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }
    //     {
    //         // Get account objects
    //         Json::Value params;
    //         params[jss::account] = to_string(alice);
    //         auto const jrr =
    //             env.rpc("json", "account_objects", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }
    // }

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
            "0061736D01000000011D0460047F7F7F7F017F60037F7F7E017F60057F7F7F7F7F"
            "017F6000017F02420308686F73745F6C69620F6F74786E5F63616C6C5F70617261"
            "6D000008686F73745F6C69620974726163655F6E756D000108686F73745F6C6962"
            "05747261636500020302010305030100110619037F01418080C0000B7F00418A82"
            "C0000B7F00419082C0000B072C04066D656D6F727902000463616C6C00030A5F5F"
            "646174615F656E6403010B5F5F686561705F6261736503020AB40901B10901027F"
            "23808080800041D0016B2200248080808000200041003A0009418080C080004110"
            "41004110200041096A41011080808080002201AC1081808080001A02402001417F"
            "4C0D00419080C080004106200041096A200141004741011082808080001A0B2000"
            "41003B010A419680C080004111410141012000410A6A41021080808080002201AC"
            "1081808080001A02402001417F4C0D0041A780C0800041072000410A6A20014102"
            "20014102491B41011082808080001A0B2000410036020C41AE80C0800041114102"
            "41022000410C6A41041080808080002201AC1081808080001A02402001417F4C0D"
            "0041BF80C0800041072000410C6A2001410420014104491B41011082808080001A"
            "0B2000420037031041C680C08000411141034103200041106A4108108080808000"
            "2201AC1081808080001A02402001417F4C0D0041D780C080004107200041106A20"
            "01410820014108491B41011082808080001A0B200041186A41086A420037030020"
            "00420037031841DE80C08000411241044104200041186A41101080808080002201"
            "AC1081808080001A02402001417F4C0D0041F080C080004108200041186A200141"
            "1020014110491B41011082808080001A0B200041286A41106A4100360200200041"
            "286A41086A42003703002000420037032841F880C0800041124105411120004128"
            "6A41141080808080002201AC1081808080001A02402001417F4C0D00418A81C080"
            "004108200041286A2001411420014114491B41011082808080001A0B200041C000"
            "6A41106A4200370300200041C0006A41086A420037030020004200370340419281"
            "C08000411241064115200041C0006A41181080808080002201AC1081808080001A"
            "02402001417F4C0D0041A481C080004108200041C0006A2001411820014118491B"
            "41011082808080001A0B200041D8006A41186A4200370300200041D8006A41106A"
            "4200370300200041D8006A41086A42003703002000420037035841AC81C0800041"
            "1241074105200041D8006A41201080808080002201AC1081808080001A02402001"
            "417F4C0D0041BE81C080004108200041D8006A2001412020014120491B41011082"
            "808080001A0B2000410036027C41C681C08000410D41084107200041FC006A4104"
            "1080808080002201AC1081808080001A02402001417F4C0D0041D381C080004103"
            "200041FC006A2001410420014104491B41011082808080001A0B20004190016A41"
            "0036020020004180016A41086A4200370300200042003703800141D681C0800041"
            "124109410820004180016A41141080808080002201AC1081808080001A02402001"
            "417F4C0D0041E881C08000410A20004180016A2001411420014114491B41011082"
            "808080001A0B200042003703980141F281C080004111410A410620004198016A41"
            "081080808080002201AC1081808080001A02402001417F4C0D00418382C0800041"
            "0720004198016A2001410820014108491B41011082808080001A0B02404130450D"
            "00200041A0016A41004130FC0B000B41F281C080004111410B4106200041A0016A"
            "41301080808080002201AC1081808080001A02402001417F4C0D00418382C08000"
            "4107200041A0016A2001413020014130491B41011082808080001A0B200041D001"
            "6A24808080800041000B0B94020100418080C0000B8A0255494E54382056616C75"
            "65204C656E3A55494E54383A55494E5431362056616C7565204C656E3A55494E54"
            "31363A55494E5433322056616C7565204C656E3A55494E5433323A55494E543634"
            "2056616C7565204C656E3A55494E5436343A55494E543132382056616C7565204C"
            "656E3A55494E543132383A55494E543136302056616C7565204C656E3A55494E54"
            "3136303A55494E543139322056616C7565204C656E3A55494E543139323A55494E"
            "543235362056616C7565204C656E3A55494E543235363A564C2056616C7565204C"
            "656E3A564C3A4143434F554E542056616C7565204C656E3A20204143434F554E54"
            "3A414D4F554E542056616C7565204C656E3A414D4F554E543A00D701046E616D65"
            "00100F706172616D65746572732E7761736D019D010400365F5A4E387872706C5F"
            "73746434686F737431356F74786E5F63616C6C5F706172616D3137686264626330"
            "346266356630656331643845012F5F5A4E387872706C5F73746434686F73743974"
            "726163655F6E756D3137686239666661343664323065373166386345022B5F5A4E"
            "387872706C5F73746434686F737435747261636531376837633261316563653630"
            "36643165376645030463616C6C071201000F5F5F737461636B5F706F696E746572"
            "090A0100072E726F64617461004D0970726F64756365727302086C616E67756167"
            "65010452757374000C70726F6365737365642D6279010572757374631D312E3838"
            "2E30202836623030626333383820323032352D30362D3233290094010F74617267"
            "65745F6665617475726573082B0B62756C6B2D6D656D6F72792B0F62756C6B2D6D"
            "656D6F72792D6F70742B1663616C6C2D696E6469726563742D6F7665726C6F6E67"
            "2B0A6D756C746976616C75652B0F6D757461626C652D676C6F62616C732B136E6F"
            "6E7472617070696E672D6670746F696E742B0F7265666572656E63652D74797065"
            "732B087369676E2D657874";

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

        // auto const contractAccount = getContractOwner(env);
        // env(contract::call(alice, contractAccount, "call"),
        //     escrow::comp_allowance(1000000),
        //     contract::add_param("uint8", "UINT8", 255),
        //     contract::add_param("uint16", "UINT16", 65535),
        //     contract::add_param(
        //         "uint32", "UINT32", static_cast<std::uint32_t>(4294967295)),
        //     contract::add_param("uint64", "UINT64", "18446744073709551615"),
        //     contract::add_param(
        //         "uint128", "UINT128", "00000000000000000000000000000001"),
        //     contract::add_param(
        //         "uint160",
        //         "UINT160",
        //         "0000000000000000000000000000000000000001"),
        //     contract::add_param(
        //         "uint192",
        //         "UINT192",
        //         "000000000000000000000000000000000000000000000001"),
        //     contract::add_param(
        //         "uint256",
        //         "UINT256",
        //         "D955DAC2E77519F05AD151A5D3C99FC8125FB39D58FF9F106F1ACA4491902C"
        //         "25"),
        //     contract::add_param("vl", "VL", "DEADBEEF"),
        //     contract::add_param("account", "ACCOUNT", alice.human()),
        //     contract::add_param(
        //         "amountXRP",
        //         "AMOUNT",
        //         XRP(1).value().getJson(JsonOptions::none)),
        //     contract::add_param(
        //         "amountIOU",
        //         "AMOUNT",
        //         USD(1).value().getJson(JsonOptions::none)),
        //     ter(tesSUCCESS));
        // env.close();
        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
    }

    // void
    // testDevelop(FeatureBitset features)
    // {
    //     testcase("develop");

    //     using namespace jtx;

    //     test::jtx::Env env{*this, features};

    //     auto const alice = Account{"alice"};
    //     auto const bob = Account{"bob"};
    //     env.fund(XRP(10'000), alice, bob);
    //     env.close();

    //     std::string contractWasmStr = loadContractWasmStr("develop");

    //     env(contract::create(alice, contractWasmStr),
    //         contract::add_instance_param(
    //             tfSendAmount,
    //             "value",
    //             "AMOUNT",
    //             XRP(2000)),
    //         contract::add_function(
    //             "emit",
    //             {}),
    //         fee(XRP(200)),
    //         ter(tesSUCCESS));
    //     env.close();

    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }

    //     auto const contractAccount = getContractOwner(env);
    //     env(contract::call(alice, contractAccount, "emit"),
    //         escrow::comp_allowance(1000000),
    //         ter(tesSUCCESS));
    //     env.close();
    //     {
    //         Json::Value params;
    //         params[jss::ledger_index] = env.current()->seq() - 1;
    //         params[jss::transactions] = true;
    //         params[jss::expand] = true;
    //         auto const jrr = env.rpc("json", "ledger", to_string(params));
    //         std::cout << jrr << std::endl;
    //     }
    // }

    void
    testWithFeats(FeatureBitset features)
    {
        // testEnabled(features);
        // testCreatePreflight(features);
        // testCreatePreclaim(features);
        // testCreateDoApply(features);
        // testModifyPreflight(features);
        // testModifyPreclaim(features);
        testModifyDoApply(features);
        // testBase(features);
        // testContractData(features);
        // testParameters(features);
        // testDevelop(features);
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
