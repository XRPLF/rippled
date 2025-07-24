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

    template <typename T>
    Json::Value
    addFuncParamValue(std::string const& typeName, T value)
    {
        Json::Value param = Json::Value(Json::objectValue);
        param[sfInstanceParameter][sfParameterValue][jss::type] = typeName;
        param[sfInstanceParameter][sfParameterValue][jss::value] = value;
        return param;
    };

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};
        Env env{*this, features};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(contract::create(
                alice,
                "0061736d0100000001280560037f7f7f017f60037f7f7e017f60087f7f7f7f"
                "7f7f7f7f017f60057f7f7f7f7f017f6000017f0288010508686f73745f6c69"
                "621c6765745f63757272656e745f6c65646765725f6f626a5f6669656c6400"
                "0008686f73745f6c69620974726163655f6e756d000108686f73745f6c6962"
                "1163726564656e7469616c5f6b65796c6574000208686f73745f6c69620574"
                "72616365000308686f73745f6c69621063616368655f6c65646765725f6f62"
                "6a00000302010405030100110619037f01418080c0000b7f0041c080c0000b"
                "7f0041c080c0000b072e04066d656d6f727902000666696e69736800050a5f"
                "5f646174615f656e6403010b5f5f686561705f6261736503020c01010ae203"
                "01df0302057f027e23004180016b22002400200041f0006a22034100360200"
                "200041e8006a2204420037030020004200370360024041838020200041e000"
                "6a411410002201411447044041a880c0004118417f20012001417f4e1bac10"
                "011a0c010b2000410e6a20002d00623a000020002000290067370340200020"
                "0041ec006a290000370045200041186a2000290045370000200020002f0160"
                "3b010c2000200028006336000f20002000290340370013200041f8006a4200"
                "3703002003420037030020044200370300200042003703602000410c6a2201"
                "411420014114418080c0004112200041e0006a412010022201412046220245"
                "044041a880c0004118417f20012001417f4e1bac10011a0c010b200041c200"
                "6a20002d00623a0000200041286a200041ef006a2900002205370300200041"
                "306a200041f7006a2900002206370300200041386a200041ff006a2d000022"
                "013a0000200041cf006a2005370000200041d7006a2006370000200041df00"
                "6a20013a0000200020002f01603b0140200020002900672205370320200020"
                "0028006336004320002005370047419280c000410b200041406b2201412041"
                "0110031a2001412041001004220141004e0d00419d80c000410b2001ac1001"
                "1a410021020b20004180016a240020020b0b490100418080c0000b40746572"
                "6d73616e64636f6e646974696f6e73637265645f6b65796c65744341434845"
                "204552524f524572726f722067657474696e67206163636f756e745f696400"
                "4d0970726f64756365727302086c616e6775616765010452757374000c7072"
                "6f6365737365642d6279010572757374631d312e38382e3020283662303062"
                "6333383820323032352d30362d323329007c0f7461726765745f6665617475"
                "726573072b0f6d757461626c652d676c6f62616c732b136e6f6e7472617070"
                "696e672d6670746f696e742b0b62756c6b2d6d656d6f72792b087369676e2d"
                "6578742b0f7265666572656e63652d74797065732b0a6d756c746976616c75"
                "652b0f62756c6b2d6d656d6f72792d6f7074"),
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
            escrow::comp_allowance(1000),
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
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
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
