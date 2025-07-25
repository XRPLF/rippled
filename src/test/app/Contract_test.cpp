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
    static uint256
    getContractSource(uint256 const& contractHash)
    {
        return keylet::contractSource(contractHash).key;
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
                    jss::Contract)
            {
                return meta[sfCreatedNode.fieldName][sfNewFields.fieldName]
                           [sfOwner.fieldName]
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

    // template <typename T>
    // Json::Value
    // addFuncParamValue(std::string const& typeName, T value)
    // {
    //     Json::Value param = Json::Value(Json::objectValue);
    //     param[sfInstanceParameter][sfParameterValue][jss::type] = typeName;
    //     param[sfInstanceParameter][sfParameterValue][jss::value] = value;
    //     return param;
    // };

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};
        // Env env{*this, envconfig(), nullptr, beast::severities::kTrace};
        Env env{*this, features};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(contract::create(
                alice,
                "0061736d0100000001150360057f7f7f7f7f017f60037f7f7f017f6000017f"
                "02120108686f73745f6c696205747261636500000303020102050301001106"
                "19037f01418080c0000b7f0041c781c0000b7f0041d081c0000b072e04066d"
                "656d6f727902000666696e69736800020a5f5f646174615f656e6403010b5f"
                "5f686561705f6261736503020c01010aa506024d01027f0240200028028008"
                "220320026a22044180084d047f200320044b0d0120020440200020036a2001"
                "2002fc0a00000b200020002802800820026a36028008410005418080c0000b"
                "0f0b000bd40502057f027e230041a0096b22002400418f80c0004123410041"
                "00410010001a4101410041004100410010001a2000410f6a220241b280c000"
                "4121fc0a000041d380c000411020024121410110001a200041406b41f380c0"
                "002800002201360200200041386a41eb80c0002900002205370300200041e3"
                "80c000290000220637033041f780c000410a200041306a4114410110001a20"
                "0041dc006a2001360200200041d4006a20053702002000200637024c200041"
                "e2006a220320024121fc0a00002000418c016a418981c00029000037020020"
                "004194016a419181c000280000360200200041003b01602000410136024820"
                "00418181c0002900003702840120004198016a22024100418008fc0b002000"
                "410136029809200041123a009801200041003b019c09024002400240024020"
                "022000419c096a2201410210010d00200028029809220441ff074b0d002002"
                "20046a41243a0000200020002802980941016a360298092000418080800836"
                "029c0920022001410410010d00200028029809220141ff074b0d0020012002"
                "6a41f3003a0000200020002802980941016a220136029809200141ff074b0d"
                "00200120026a41213a0000200020002802980941016a360298092002200341"
                "2110010d00200028029809220141ff074b0d00200120026a4181013a000020"
                "0020002802980941016a220136029809200141ff074b0d0020004198016a20"
                "016a41143a0000200020002802980941016a360298092002200041cc006a41"
                "1410010d00200028029809220141ff074b0d0020004198016a20016a418301"
                "3a0000200020002802980941016a220136029809200141ff074b0d00200041"
                "98016a20016a41143a0000200020002802980941016a360298092002200041"
                "84016a41141001450d010b41ab81c000411c41004100410010001a0c010b20"
                "002802980922024181084f0d01419581c000411620004198016a2002410110"
                "001a0b200041a0096a240041010f0b000b0bd1010100418080c0000bc70142"
                "7566666572206f766572666c6f772424242424205354415254494e47205741"
                "534d20455845435554494f4e202424242424edc378ba29574532509b818378"
                "fa2bfbdc3c7c3df17dad5d3687bc6ceff8ae170120205369676e696e675075"
                "624b65793a162825f91ce674241ca62ca3f05b7e70d2109a4220204163636f"
                "756e743af51dfc2a09d62cbba1dfbdd4691dac96ad98b90f53657269616c69"
                "7a6564205061796d656e7454786e3a53657269616c697a6174696f6e206572"
                "726f72206f63637572726564004d0970726f64756365727302086c616e6775"
                "616765010452757374000c70726f6365737365642d6279010572757374631d"
                "312e38382e30202836623030626333383820323032352d30362d323329007c"
                "0f7461726765745f6665617475726573072b0f6d757461626c652d676c6f62"
                "616c732b136e6f6e7472617070696e672d6670746f696e742b0b62756c6b2d"
                "6d656d6f72792b087369676e2d6578742b0f7265666572656e63652d747970"
                "65732b0a6d756c746976616c75652b0f62756c6b2d6d656d6f72792d6f707"
                "4"),
            contract::add_function("finish", {{"owner", "ACCOUNT"}}),
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
        env(contract::call(alice, contractAccount, "finish"),
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

    std::string
    loadContractWasmStr()
    {
        std::string name =
            "/Users/darkmatter/projects/ledger-works/craft/projects/develop/"
            "target/wasm32-unknown-unknown/release/develop.wasm";
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
    testSimple(FeatureBitset features)
    {
        testcase("Test simple");

        using namespace jtx;

        // Env env{*this, features};
        // Env env{*this, envconfig(), features, nullptr,
        //     beast::severities::kTrace
        // };

        test::jtx::Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        std::string contractWasmStr = loadContractWasmStr();

        env(contract::create(alice, contractWasmStr),
            contract::add_function(
                "finish",
                {{"uint8", "UINT8"},
                 {"uint16", "UINT16"},
                 {"owner", "ACCOUNT"}}),
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

        env(contract::call(alice, contractAccount, "finish"),
            escrow::comp_allowance(1000000),
            contract::add_param_value("uint8", "UINT8", 255),
            contract::add_param_value("uint16", "UINT16", 65535),
            contract::add_param_value("owner", "ACCOUNT", alice.human()),
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

        // env(invoke::invoke(alice), M("test simple"), fee(XRP(1)),
        // ter(tecDIR_FULL)); env.close();

        // BEAST_EXPECT(env.le(alice)->getFieldU64(sfOwnerCount) == 8388578);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testEnabled(features);
        testSimple(features);
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
