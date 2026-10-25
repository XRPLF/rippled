#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/contract.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/requires.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class Contract_test : public beast::unit_test::Suite
{
    struct TestLedgerData
    {
        int index;
        std::string txType;
        std::string result;
    };

    static json::Value
    getLastLedger(jtx::Env& env)
    {
        json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        return env.rpc("json", "ledger", to_string(params));
    }

    static json::Value
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

    static json::Value
    getContractCreateTx(json::Value const& jrr)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::TransactionType] == jss::ContractCreate)
                return txn;
        }
        return {};
    }

    static uint256
    getContractHash(std::string const& codeHex)
    {
        auto const wasmBytes = strUnHex(codeHex).value_or(Blob{});
        return xrpl::sha512HalfS(makeSlice(wasmBytes));
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
            {
                BEAST_EXPECT(
                    sPV.isMember("name") && sPV["name"].asString() == ePV["name"].asString());
            }

            if (ePV.isMember("type"))
            {
                BEAST_EXPECT(
                    sPV.isMember("type") && sPV["type"].asString() == ePV["type"].asString());
            }

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
            {
                BEAST_EXPECT(
                    sPV.isMember("type") && sPV["type"].asString() == ePV["type"].asString());
            }

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
        if (!accountID)
            return std::make_tuple(jtx::Account{"invalid"}, contractHash, jt.jv);
        auto const [contractKey, sle] = contractKeyAndSle(
            *env.current(), contractHash, *accountID, jt.jv[sfSequence.jsonName].asUInt());
        if (!sle)
            return std::make_tuple(jtx::Account{"invalid"}, contractHash, jt.jv);
        jtx::Account const contractAccount{
            "Contract pseudo-account",
            sle->getAccountID(sfContractAccount),
        };
        return std::make_tuple(contractAccount, contractHash, jt.jv);
    }

    // Exports set, unset and own (write "count" for the caller, remove it, write it for the
    // contract itself), fill (forty eight-byte values for the caller), pay (emit a 1 XRP
    // payment to the caller), rekey and signers (emit a SetRegularKey or a SignerListSet for
    // its own account), event (emit an event named "ping"), handover (emit a ContractModify
    // for itself that makes the caller its owner), issue (emit an MPTokenIssuanceCreate),
    // mint (pay the caller 5 of that MPT) and receive (accept what its parameter sends);
    // each returns what its last host call answered.
    std::string const storageContractWasm_ =
        "0061736D0100000001290660037F7F7F017F60067F7F7F7F7F7F017F60017F017F60047F7F7F7F017F60027F7F"
        "017F60"
        "00017F02A8010708686F73745F6C69620874785F6669656C64000008686F73745F6C6962157365745F64617461"
        "5F6F62"
        "6A6563745F6669656C64000108686F73745F6C6962096275696C645F74786E000208686F73745F6C69620D6164"
        "645F74"
        "786E5F6669656C64000308686F73745F6C69620E656D69745F6275696C745F74786E000008686F73745F6C6962"
        "08656D"
        "69745F74786E000308686F73745F6C69620A656D69745F6576656E740003030F0E020405050505050505050505"
        "050505"
        "0301000107690D066D656D6F7279020003736574000905756E736574000A036F776E000B0466696C6C000C0370"
        "617900"
        "0D0572656B6579000E077369676E657273000F056576656E7400100868616E646F766572001105697373756500"
        "12046D"
        "696E740013077265636569766500140AF4050E0A0020004100411410000B2301017F2000100721022002410048"
        "044020"
        "020F0B41004114412041054130200110010B0A0041818020410210080B0A0041818020410010080B0A0041A080"
        "204102"
        "10080B6801027F41818020100721012001410048044020010F0B02400340200041284F0D0141C9014130200041"
        "0A6E6A"
        "3A000041CA0141302000410A706A3A00004100411441C801410341C0004109100121012001410048044020010F"
        "0B2000"
        "41016A21000C000B0B41000B7D01027F4181802041AC024114100021012001410048044020010F0B4100100221"
        "002000"
        "410048044020000F0B20004181801841C0024108100321012001410048044020010F0B20004183802041AB0241"
        "151003"
        "21012001410048044020010F0B20004190034104100421012001410048044020010F0B4190032802000B3C0101"
        "7F41A0"
        "8020418E044114100021002000410048044020000F0B41F40341C40041D8044104100521002000410048044020"
        "000F0B"
        "41D8042802000B3C01017F41A0802041DC054114100021002000410048044020000F0B41BC0541D10041D80441"
        "041005"
        "21002000410048044020000F0B41D8042802000B0E0041A006410441AA06410710060B6401027F418180204185"
        "074114"
        "100021012001410048044020010F0B41DD00100221002000410048044020000F0B200041828020418407411510"
        "032101"
        "2001410048044020010F0B200041B6074104100421012001410048044020010F0B41B6072802000B3101027F41"
        "361002"
        "21002000410048044020000F0B200041B6074104100421012001410048044020010F0B41B6072802000B950101"
        "027F41"
        "81802041E9074114100021012001410048044020010F0B41A08020419D08411410002101200141004804402001"
        "0F0B41"
        "00100221002000410048044020000F0B2000418180184190084121100321012001410048044020010F0B200041"
        "838020"
        "41E8074115100321012001410048044020010F0B200041B6074104100421012001410048044020010F0B41B607"
        "280200"
        "0B040041000B0BFC010F0041200B05636F756E740041300B0210010041C0000B090300000000000000010041C8"
        "010B03"
        "6B30300041AB020B01140041C0020B0840000000000F42400041F4030B1A120005224000000024000000006840"
        "000000"
        "00000000730081140041A2040B1688141111111111111111111111111111111111111111004184070B01140041"
        "E8070B"
        "0114004190080B0D600000000000000005000000000041A0060B0470696E670041AA060B070600016E02100100"
        "41BC05"
        "0B2012000C22400000002400000000202300000001684000000000000000730081140041F0050B1DF4EB130001"
        "811411"
        "11111111111111111111111111111111111111E1F100CC01046E616D65015D09000874785F6669656C64010373"
        "657402"
        "096275696C645F74786E030D6164645F74786E5F6669656C64040E656D69745F6275696C745F74786E0508656D"
        "69745F"
        "74786E060A656D69745F6576656E7407046C6F616408057772697465025509070100056669656C640803000566"
        "69656C"
        "6401036C656E02016E0C0200016901016E0D02000374786E01016E0E0100016E0F0100016E1102000374786E01"
        "016E12"
        "02000374786E01016E1302000374786E01016E030F010C020104646F6E6502046E657874";

    std::string const baseContractWasm_ =
        "0061736D01000000010D0260057F7F7F7F7F006000017F02120108686F73745F6C6962"
        "05747261636500000302010105030100110619037F01418080C0000B7F00419E80C000"
        "0B7F0041A080C0000B072C04066D656D6F72790200046261736500010A5F5F64617461"
        "5F656E6403010B5F5F686561705F6261736503020A6B016901017F2380808080004120"
        "6B2200248080808000200041186A410028009080C08000360200200041106A41002900"
        "8880C080003703002000410029008080C08000370308419480C08000410A200041086A"
        "41144101108080808000200041206A24808080800041000B0B270100418080C0000B1E"
        "AE123A8556F3CF91154711376AFB0F894F832B3D20204163636F756E743A";

    std::string const base2ContractWasm_ =
        "0061736D01000000010D0260057F7F7F7F7F006000017F02120108686F73745F6C6962"
        "05747261636500000302010105030100110619037F01418080C0000B7F0041A380C000"
        "0B7F0041B080C0000B072C04066D656D6F72790200046261736500010A5F5F64617461"
        "5F656E6403010B5F5F686561705F6261736503020A1A011800418080C0800041234100"
        "4100410010808080800041000B0B2C0100418080C0000B232424242424205354415254"
        "494E47204241534520455845435554494F4E202424242424";

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

            env(contract::create(alice, baseContractWasm_), Ter(temDISABLED));
        }

        // temINVALID_FLAG: tfContractMask is not allowed.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, baseContractWasm_),
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
            jv[sfFunctions.jsonName] = json::ValueType::Array;  // Empty array

            env(jv, Ter(temARRAY_EMPTY));
        }

        // temARRAY_TOO_LARGE: Functions array too large
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            // One past the limit, built from it rather than from a list that has to be kept
            // in step with it by hand.
            auto jt = env.jt(contract::create(alice, baseContractWasm_));
            for (std::size_t i = 0; i <= xrpl::contract::maxContractFunctions; ++i)
                contract::AddFunction("func" + std::to_string(i), {})(env, jt);

            env(jt.jv, Ter(temARRAY_TOO_LARGE));
        }

        // temREDUNDANT: Duplicate function name
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction("test", {}),
                contract::AddFunction("test", {}),  // Duplicate
                Ter(temREDUNDANT));
        }

        // temARRAY_TOO_LARGE: Function Parameters array too large
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction(
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction("test", {{0xFF000000, "param", "UINT8"}}),  // Invalid flag
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction("test", {}),
                contract::AddInstanceParam(0, "param1", "UINT8", 1),
                contract::AddInstanceParam(0, "param2", "UINT8", 2),
                contract::AddInstanceParam(0, "param3", "UINT8", 3),
                contract::AddInstanceParam(0, "param4", "UINT8", 4),
                contract::AddInstanceParam(0, "param5", "UINT8", 5),
                contract::AddInstanceParam(0, "param6", "UINT8", 6),
                contract::AddInstanceParam(0, "param7", "UINT8", 7),
                contract::AddInstanceParam(0, "param8", "UINT8", 8),
                contract::AddInstanceParam(0, "param9", "UINT8", 9),
                contract::AddInstanceParam(0, "param10", "UINT8", 10),
                contract::AddInstanceParam(0, "param11", "UINT8", 11),
                contract::AddInstanceParam(0, "param12", "UINT8", 12),
                contract::AddInstanceParam(0, "param13", "UINT8", 13),
                contract::AddInstanceParam(0, "param14", "UINT8", 14),
                contract::AddInstanceParam(0, "param15", "UINT8", 15),
                contract::AddInstanceParam(0, "param16", "UINT8", 16),
                contract::AddInstanceParam(0, "param17", "UINT8", 17),
                contract::AddInstanceParam(0, "param18", "UINT8", 18),
                contract::AddInstanceParam(0, "param19", "UINT8", 19),
                contract::AddInstanceParam(0, "param20", "UINT8", 20),
                contract::AddInstanceParam(0, "param21", "UINT8", 21),
                contract::AddInstanceParam(0, "param22", "UINT8", 22),
                contract::AddInstanceParam(0, "param23", "UINT8", 23),
                contract::AddInstanceParam(0, "param24", "UINT8", 24),
                contract::AddInstanceParam(0, "param25", "UINT8", 25),
                contract::AddInstanceParam(0, "param26", "UINT8", 26),
                contract::AddInstanceParam(0, "param27", "UINT8", 27),
                contract::AddInstanceParam(0, "param28", "UINT8", 28),
                contract::AddInstanceParam(0, "param29", "UINT8", 29),
                contract::AddInstanceParam(0, "param30", "UINT8", 30),
                contract::AddInstanceParam(0, "param31", "UINT8", 31),
                contract::AddInstanceParam(0, "param32", "UINT8", 32),
                contract::AddInstanceParam(0, "param33", "UINT8", 33),
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
            jv[sfContractCode.jsonName] = baseContractWasm_;
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
        //         contract::AddFunction("test", {}),
        //         contract::AddInstanceParam(
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
        //         contract::AddFunction("base", {}),
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
        //         contract::AddFunction(
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
        //         contract::AddInstanceParam(0, "uint8", "UINT8", 1),
        //         contract::AddInstanceParam(0, "uint32", "UINT32", 100),
        //         contract::AddFunction("base", {}),
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
                contract::AddFunction("test", {}),
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

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction("base", {}),
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
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Second create with same code (install)
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 2),  // Different value
                contract::AddFunction("base", {}),
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
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Get the hash of the contract
            uint256 const contractHash = getContractHash(baseContractWasm_);

            // Install using ContractHash
            env(contract::create(alice, contractHash),
                contract::AddInstanceParam(0, "uint8", "UINT8", 2),
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
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddInstanceParam(0, "uint32", "UINT32", 100),
                contract::AddFunction("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Get the hash
            uint256 const contractHash = getContractHash(baseContractWasm_);

            // Try to install with mismatched InstanceParameterValues
            // Only providing one parameter when ContractSource expects two
            env(contract::create(alice, contractHash),
                contract::AddInstanceParam(0, "uint8", "UINT8", 2),
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
            uint256 const contractHash = getContractHash(base2ContractWasm_);

            env(contract::create(alice, contractHash), Fee(XRP(200)), Ter(temMALFORMED));
        }

        // tesSUCCESS: ContractCode with InstanceParameters for first
        // creation
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account{"alice"};
            env.fund(XRP(10'000), alice);
            env.close();

            env(contract::create(alice, base2ContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 255),
                contract::AddInstanceParam(tfSendAmount, "amount", "AMOUNT", XRP(100)),
                contract::AddFunction("base", {}),
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
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Bob installs same contract
            env(contract::create(bob, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 2),
                contract::AddFunction("base", {}),
                Fee(XRP(200)),
                Ter(tesSUCCESS));
            env.close();

            // Alice installs another instance
            env(contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 3),
                contract::AddFunction("base", {}),
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
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
                    contract::create(alice, baseContractWasm_),
                    contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                    contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
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
                    contract::create(alice, baseContractWasm_),
                    contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                    contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto jt = env.jt(
                contract::modify(alice, contractAccount, base2ContractWasm_),
                contract::AddInstanceParam(0, "uint16", "UINT16", 1),
                contract::AddFunction("base", {{0, "uint16", "UINT16"}}),
                Fee(XRP(200)));
            env(jt, Ter(tesSUCCESS));
            env.close();

            // old contract source is deleted
            auto const [sourceKey, sourceSle] =
                contractSourceKeyAndSle(*env.current(), contractHash);
            BEAST_EXPECT(!sourceSle);

            // new contract source exists
            uint256 const newContractHash = getContractHash(base2ContractWasm_);
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            auto const [contractAccount2, contractHash2, jv2] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, base2ContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto const jt = env.jt(
                contract::modify(alice, contractAccount, contractHash2),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            // Modify contract
            auto const jt = env.jt(contract::modify(alice, contractAccount, bob), Fee(XRP(200)));
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            env(contract::del(alice, contractAccount), Ter(tesSUCCESS));
            env.close();

            // Pseudo Account is deleted
            auto const pseudoAccountKey = keylet::account(contractAccount);
            BEAST_EXPECT(!env.le(pseudoAccountKey));

            // Contract instance is deleted
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
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            auto const seq2 = env.current()->seq();
            auto const [contractAccount2, contractHash2, jv2] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, baseContractWasm_),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {{0, "uint8", "UINT8"}}),
                Fee(XRP(200)));

            env(contract::del(alice, contractAccount), Ter(tesSUCCESS));
            env.close();

            // Pseudo Account is deleted
            auto const pseudoAccountKey = keylet::account(contractAccount);
            BEAST_EXPECT(!env.le(pseudoAccountKey));

            // Contract instance is deleted
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

            env(contract::userDelete(alice, baseContractWasm_), Ter(temDISABLED));
        }
    }

    // A validator's config that votes for `value` on the fee setting `key`.
    static std::unique_ptr<Config>
    votingFor(std::string const& key, std::uint32_t value)
    {
        auto cfg = jtx::envconfig(jtx::validator, "");
        cfg->section(Sections::kVoting).set(key, std::to_string(value));
        // The node's own fee setup follows the vote, as Config derives both from [voting].
        auto const vote = setupFeeVote(cfg->section(Sections::kVoting));
        cfg->fees.gasLimit = vote.gasLimit;
        cfg->fees.bytecodeSizeLimit = vote.bytecodeSizeLimit;
        cfg->fees.gasPrice = vote.gasPrice;
        return cfg;
    }

    // Close past the flag ledger, so a fee vote lands in FeeSettings.
    static void
    closePastFlagLedger(jtx::Env& env)
    {
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();
    }

    // What ContractCreate charges for `codeHex`: the base fee, one owner increment, and
    // `createByteMultiplier` drops per byte of code.
    static XRPAmount
    createFeeFor(jtx::Env const& env, std::string const& codeHex)
    {
        auto const& fees = env.current()->fees();
        return fees.base + fees.increment +
            XRPAmount{xrpl::contract::contractCreateFee(codeHex.size() / 2)};
    }

    // What ContractCall charges for `gas`: the base fee plus the gas cost rounded up to a
    // drop.
    static XRPAmount
    callFeeFor(jtx::Env const& env, std::uint64_t gas)
    {
        auto const& fees = env.current()->fees();
        return fees.base +
            XRPAmount{static_cast<XRPAmount::value_type>(
                ((gas * fees.gasPrice) + microDropsPerDrop - 1) / microDropsPerDrop)};
    }

    void
    testCreateUploadFee(FeatureBitset features)
    {
        testcase("create and modify charge per byte of ContractCode");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        auto const fee = createFeeFor(env, baseContractWasm_);
        BEAST_EXPECT(
            fee ==
            env.current()->fees().base + env.current()->fees().increment +
                XRPAmount{
                    static_cast<XRPAmount::value_type>(baseContractWasm_.size() / 2) *
                    xrpl::contract::createByteMultiplier});

        // A drop short of the per-byte charge is refused.
        env(contract::create(alice, baseContractWasm_),
            contract::AddFunction("base", {}),
            Fee(fee - 1),
            Ter(telINSUF_FEE_P));

        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, baseContractWasm_),
            contract::AddFunction("base", {}),
            Fee(fee));

        // ContractModify charges the same per byte of new code, without the increment.
        auto const modifyFee = env.current()->fees().base +
            XRPAmount{xrpl::contract::contractCreateFee(base2ContractWasm_.size() / 2)};
        env(contract::modify(alice, contractAccount, base2ContractWasm_),
            contract::AddFunction("base", {}),
            Fee(modifyFee - 1),
            Ter(telINSUF_FEE_P));
        env(contract::modify(alice, contractAccount, base2ContractWasm_),
            contract::AddFunction("base", {}),
            Fee(modifyFee));
        env.close();
    }

    void
    testCodeSizeLimit(FeatureBitset features)
    {
        testcase("create and modify enforce the bytecode size limit");

        using namespace jtx;

        auto const alice = Account{"alice"};

        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice);
            env.close();

            auto const limit = env.app().getFees().bytecodeSizeLimit;
            BEAST_EXPECT(limit == FeeSetup{}.bytecodeSizeLimit);

            // At the voted limit the code is taken.
            auto const atLimit = contract::wasmOfSize(limit, "base");
            env(contract::create(alice, atLimit),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, atLimit)));
            env.close();

            // One byte past it is refused, for create and for modify.
            auto const overLimit = contract::wasmOfSize(limit + 1, "base");
            env(contract::create(alice, overLimit),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, overLimit)),
                Ter(temMALFORMED));

            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, baseContractWasm_),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, baseContractWasm_)));
            env(contract::modify(alice, contractAccount, overLimit),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, overLimit)),
                Ter(temMALFORMED));

            // Past the protocol ceiling it is malformed, whatever the vote.
            auto const overCeiling = contract::wasmOfSize(kMaxBytecodeSizeLimit + 1, "base");
            env(contract::create(alice, overCeiling),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, overCeiling)),
                Ter(temMALFORMED));
            env(contract::modify(alice, contractAccount, overCeiling),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, overCeiling)),
                Ter(temMALFORMED));
        }

        {
            // A size limit voted to zero stops uploads.
            Env env{*this, votingFor(Keys::kBytecodeSizeLimit, 0), features};
            closePastFlagLedger(env);
            env.fund(XRP(10'000), alice);
            env.close();
            BEAST_EXPECT(env.app().getFees().bytecodeSizeLimit == 0);

            env(contract::create(alice, baseContractWasm_),
                contract::AddFunction("base", {}),
                Fee(createFeeFor(env, baseContractWasm_)),
                Ter(temTEMP_DISABLED));
        }
    }

    void
    testCodeIsScreened(FeatureBitset features)
    {
        testcase("create and modify screen the code");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        // The WASM preamble followed by bytes that are not a module.
        std::string const notAModule = "0061736D01000000" + std::string(112, 'A');
        env(contract::create(alice, notAModule),
            contract::AddFunction("base", {}),
            Fee(createFeeFor(env, notAModule)),
            Ter(temINVALID_BYTECODE));

        // A module that does not export a function its ABI names.
        auto const exportsOther = contract::wasmOfSize(64, "other");
        env(contract::create(alice, exportsOther),
            contract::AddFunction("base", {}),
            Fee(createFeeFor(env, exportsOther)),
            Ter(temINVALID_BYTECODE));

        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, baseContractWasm_),
            contract::AddFunction("base", {}),
            Fee(createFeeFor(env, baseContractWasm_)));

        env(contract::modify(alice, contractAccount, notAModule),
            contract::AddFunction("base", {}),
            Fee(createFeeFor(env, notAModule)),
            Ter(temINVALID_BYTECODE));
    }

    void
    testCallGasLimit(FeatureBitset features)
    {
        testcase("call enforces the gas limit");

        using namespace jtx;

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        {
            // A validator past the flag ledger, so the gas price the fee checks below use is
            // in FeeSettings.
            Env env{*this, votingFor(Keys::kGasPrice, FeeSetup{}.gasPrice), features};
            closePastFlagLedger(env);
            env.fund(XRP(10'000), alice, bob);
            env.close();

            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, contract::wasmOfSize(64, "base")),
                contract::AddFunction("base", {}),
                Fee(XRP(200)));

            auto const gasLimit = env.app().getFees().gasLimit;
            BEAST_EXPECT(gasLimit == FeeSetup{}.gasLimit);

            // Over the voted limit.
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(gasLimit + 1),
                Fee(callFeeFor(env, gasLimit + 1)),
                Ter(temBAD_LIMIT));

            // Zero, and over the protocol ceiling, are malformed.
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(0),
                Fee(callFeeFor(env, 0)),
                Ter(temBAD_LIMIT));
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(kMaxGasLimit + 1),
                Fee(callFeeFor(env, kMaxGasLimit + 1)),
                Ter(temBAD_LIMIT));

            // The gas cost is computed in 64 bits: a million gas at the default price is
            // one XRP, not the 3567 drops a 32-bit product wraps to.
            BEAST_EXPECT(
                callFeeFor(env, gasLimit) == env.current()->fees().base + XRPAmount{1'000'000});
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(gasLimit),
                Fee(env.current()->fees().base + XRPAmount{4'000}),
                Ter(telINSUF_FEE_P));

            // At the limit, with its fee, the call runs.
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(gasLimit),
                Fee(callFeeFor(env, gasLimit)));
            env.close();
        }

        {
            // A gas limit set to zero stops calls to a contract made before the change.
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob);
            env.close();

            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, contract::wasmOfSize(64, "base")),
                contract::AddFunction("base", {}),
                Fee(XRP(200)));
            // The node's fee setup changes, as a restart with a new [voting] section would.
            env.app().config().fees.gasLimit = 0;
            BEAST_EXPECT(env.app().getFees().gasLimit == 0);

            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(1'000),
                Fee(callFeeFor(env, 1'000)),
                Ter(temTEMP_DISABLED));
        }
    }

    void
    testModifyToMissingSource(FeatureBitset features)
    {
        testcase("modify naming a missing ContractSource");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, baseContractWasm_),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));

        // A ContractHash no ContractSource carries is malformed, as it is for ContractCreate.
        env(contract::modify(alice, contractAccount, getContractHash(base2ContractWasm_)),
            Fee(XRP(1)),
            Ter(temMALFORMED));
    }

    // Creates a contract owned by `owner` whose `base` function returns `result`, taking the
    // given function parameters, and returns its pseudo-account.
    jtx::Account
    createCallable(
        jtx::Env& env,
        jtx::Account const& owner,
        std::int32_t result = 1,
        std::vector<std::tuple<std::uint32_t, std::string, std::string>> const& params = {})
    {
        using namespace jtx;
        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(owner, contract::wasmOfSize(64, "base", result)),
            contract::AddFunction("base", params),
            Fee(XRP(200)));
        return contractAccount;
    }

    void
    testCreateFlagsAndParameters(FeatureBitset features)
    {
        testcase("create flags and instance parameter transfers");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        // Only one immutability flag may be set.
        env(contract::create(alice, baseContractWasm_),
            contract::AddFunction("base", {}),
            Txflags(tfCodeImmutable | tfABIImmutable),
            Fee(XRP(200)),
            Ter(temINVALID_FLAG));

        // An instance parameter that sends more XRP than the owner holds is refused once the
        // pseudo-account exists to receive it.
        env(contract::create(alice, baseContractWasm_),
            contract::AddInstanceParam(tfSendAmount, "amount", "AMOUNT", XRP(1'000'000)),
            contract::AddFunction("base", {}),
            Fee(XRP(200)),
            Ter(tecUNFUNDED));

        // One the owner can afford is sent to the contract's pseudo-account.
        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, baseContractWasm_),
            contract::AddInstanceParam(tfSendAmount, "amount", "AMOUNT", XRP(100)),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        BEAST_EXPECT(env.balance(contractAccount) == XRP(100));
    }

    void
    testCallPreflightAndPreclaim(FeatureBitset features)
    {
        testcase("call preflight and preclaim");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createCallable(env, alice);
        auto const fee = callFeeFor(env, 1'000);

        // Only the universal flags are allowed.
        env(contract::call(bob, contractAccount, "base"),
            escrow::Gas(1'000),
            Txflags(tfBurnable),
            Fee(fee),
            Ter(temINVALID_FLAG));

        // The contract account must exist and be a contract's pseudo-account.
        Account const nobody{"nobody"};
        env(contract::call(bob, nobody, "base"), escrow::Gas(1'000), Fee(fee), Ter(tecNO_TARGET));
        env(contract::call(bob, alice, "base"), escrow::Gas(1'000), Fee(fee), Ter(tecNO_TARGET));

        // The function must be in the contract's ABI.
        env(contract::call(bob, contractAccount, "missing"),
            escrow::Gas(1'000),
            Fee(fee),
            Ter(tecNO_ENTRY));

        // A parameter sending more than the caller holds is refused before the call runs.
        env(contract::call(bob, contractAccount, "base"),
            contract::AddParam(tfSendAmount, "amount", "AMOUNT", XRP(1'000'000)),
            escrow::Gas(1'000),
            Fee(fee),
            Ter(tecUNFUNDED));
    }

    void
    testCallApply(FeatureBitset features)
    {
        testcase("call apply");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const fee = callFeeFor(env, 1'000);

        {
            // A parameter sending XRP moves it to the contract, and the call returns.
            auto const contractAccount =
                createCallable(env, alice, 1, {{tfSendAmount, "amount", "AMOUNT"}});
            env(contract::call(bob, contractAccount, "base"),
                contract::AddParam(tfSendAmount, "amount", "AMOUNT", XRP(10)),
                escrow::Gas(1'000),
                Fee(fee));
            env.close();
            BEAST_EXPECT(env.balance(contractAccount) == XRP(10));

            // The parameters must match the ABI in number and in type.
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(tecINVALID_PARAMETERS));
            env(contract::call(bob, contractAccount, "base"),
                contract::AddParam(0, "amount", "UINT8", 1),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(tecINVALID_PARAMETERS));
        }

        {
            // Instance parameters are handed to the contract, which runs.
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, contract::wasmOfSize(65, "base")),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {}),
                Fee(XRP(200)));
            env(contract::call(bob, contractAccount, "base"), escrow::Gas(1'000), Fee(fee));
            env.close();
        }

        {
            // A function that takes no parameters is not handed one. Its own code, so it does
            // not share an earlier contract's source and ABI.
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, contract::wasmOfSize(68, "base")),
                contract::AddFunction("base", {}),
                Fee(XRP(200)));
            env(contract::call(bob, contractAccount, "base"),
                contract::AddParam(0, "value", "UINT8", 1),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(tecINVALID_PARAMETERS));
        }

        {
            // A negative return rejects the call.
            auto const contractAccount = createCallable(env, alice, -1);
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(tecBYTECODE_REJECTED));
        }

        {
            // A run the engine stops is reported with its result.
            auto const contractAccount = createCallable(env, alice, 2);
            env(contract::call(bob, contractAccount, "base"),
                escrow::Gas(1),
                Fee(callFeeFor(env, 1)),
                Ter(tecOUT_OF_GAS));
        }
    }

    void
    testModifyChecks(FeatureBitset features)
    {
        testcase("modify preflight and preclaim");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createCallable(env, alice);
        auto const code = baseContractWasm_;
        auto const fee = XRP(1);

        // Only the universal flags are allowed.
        env(contract::modify(alice, contractAccount, alice),
            Txflags(tfBurnable),
            Fee(fee),
            Ter(temINVALID_FLAG));

        // ContractCode and ContractHash together are malformed.
        {
            auto jv = contract::modify(alice, contractAccount, code);
            jv[sfContractHash] = to_string(getContractHash(code));
            env(jv, contract::AddFunction("base", {}), Fee(XRP(200)), Ter(temMALFORMED));
        }

        // The ABI and the parameters are checked as for ContractCreate.
        env(contract::modify(alice, contractAccount, code), Fee(XRP(200)), Ter(temARRAY_EMPTY));
        {
            auto jv = contract::modify(alice, contractAccount, code);
            jv[sfInstanceParameters] = json::Value(json::ValueType::Array);
            env(jv, contract::AddFunction("base", {}), Fee(XRP(200)), Ter(temARRAY_EMPTY));
        }
        {
            auto jv = contract::modify(alice, contractAccount, alice);
            jv[sfInstanceParameterValues] = json::Value(json::ValueType::Array);
            env(jv, Fee(fee), Ter(temARRAY_EMPTY));
        }

        // The new owner may be neither the submitter nor the contract.
        env(contract::modify(alice, contractAccount, alice), Fee(fee), Ter(temMALFORMED));
        {
            auto jv = contract::modify(alice, contractAccount, alice);
            jv[sfOwner] = contractAccount.human();
            env(jv, Fee(fee), Ter(temMALFORMED));
        }

        // The contract account must exist and be a contract's pseudo-account.
        Account const nobody{"nobody"};
        env(contract::modify(alice, nobody, bob), Fee(fee), Ter(tecNO_TARGET));
        env(contract::modify(alice, bob, nobody), Fee(fee), Ter(tecNO_TARGET));
        {
            // Without ContractAccount the submitter's own account is the contract account.
            auto jv = contract::modify(alice, contractAccount, bob);
            jv.removeMember(sfContractAccount.jsonName);
            env(jv, Fee(fee), Ter(tecNO_TARGET));
        }

        // Only the owner may modify the contract, and only to an owner that exists.
        env(contract::modify(bob, contractAccount, alice), Fee(fee), Ter(tecNO_PERMISSION));
        env(contract::modify(alice, contractAccount, nobody), Fee(fee), Ter(tecNO_TARGET));

        // Installing a source with instance parameters needs values that match them.
        {
            auto const [paramContract, paramHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(bob, contract::wasmOfSize(66, "base")),
                contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                contract::AddFunction("base", {}),
                Fee(XRP(200)));
            env(contract::modify(alice, contractAccount, paramHash), Fee(fee), Ter(temMALFORMED));
            {
                auto const jv2 = contract::modify(alice, contractAccount, paramHash);
                env(jv2,
                    contract::AddInstanceParam(0, "uint8", "UINT8", 1),
                    contract::AddInstanceParam(0, "uint16", "UINT16", 2),
                    Fee(fee),
                    Ter(temMALFORMED));
            }

            // With matching values the contract moves to the shared source.
            env(contract::modify(alice, contractAccount, paramHash),
                contract::AddInstanceParam(0, "uint8", "UINT8", 7),
                Fee(fee));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::contractSource(paramHash))->getFieldU64(sfReferenceCount) == 2);

            // Values alone update the instance.
            {
                auto jv3 = contract::modify(alice, contractAccount, bob);
                jv3.removeMember(sfOwner.jsonName);
                env(jv3, contract::AddInstanceParam(0, "uint8", "UINT8", 8), Fee(fee));
                env.close();
                auto const sle = env.le(keylet::account(contractAccount.id()));
                auto const contractSle = sle
                    ? env.le(keylet::contract(sle->getFieldH256(sfContractID)))
                    : std::shared_ptr<SLE const>{};
                if (BEAST_EXPECT(contractSle))
                {
                    auto const values = contractSle->getFieldArray(sfInstanceParameterValues);
                    BEAST_EXPECT(
                        values.size() == 1 &&
                        values[0].getFieldData(sfParameterValue).getFieldU8() == 8);
                }
            }
        }
    }

    void
    testModifyImmutability(FeatureBitset features)
    {
        testcase("modify immutability");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const fee = XRP(200);
        auto const createWith = [&](std::uint32_t flags, std::size_t size) {
            auto const [contractAccount, contractHash, jv] = setContract(
                env,
                tesSUCCESS,
                contract::create(alice, contract::wasmOfSize(size, "base")),
                contract::AddFunction("base", {}),
                Txflags(flags),
                Fee(fee));
            return contractAccount;
        };

        auto const immutable = createWith(tfImmutable, 70);
        env(contract::modify(alice, immutable, bob), Fee(fee), Ter(tecNO_PERMISSION));

        auto const codeImmutable = createWith(tfCodeImmutable, 71);
        env(contract::modify(alice, codeImmutable, baseContractWasm_),
            contract::AddFunction("base", {}),
            Fee(fee),
            Ter(tecNO_PERMISSION));
        // Nor by naming another contract's source.
        env(contract::modify(
                alice, codeImmutable, getContractHash(contract::wasmOfSize(70, "base"))),
            Fee(fee),
            Ter(tecNO_PERMISSION));
        // Its owner may still change.
        env(contract::modify(alice, codeImmutable, bob), Fee(fee));
        env.close();

        // An immutable ABI lets the code change as long as every function keeps its name and
        // parameters, and lets the owner change.
        auto const abiImmutable = createWith(tfABIImmutable, 72);
        {
            auto const jv = contract::modify(alice, abiImmutable, baseContractWasm_);
            env(jv, Fee(fee), Ter(temARRAY_EMPTY));
        }
        env(contract::modify(alice, abiImmutable, contract::wasmOfSize(73, "other")),
            contract::AddFunction("other", {}),
            Fee(fee),
            Ter(tecNO_PERMISSION));
        env(contract::modify(alice, abiImmutable, contract::wasmOfSize(74, "base")),
            contract::AddFunction("base", {{0, "amount", "AMOUNT"}}),
            Fee(fee),
            Ter(tecNO_PERMISSION));
        env(contract::modify(alice, abiImmutable, contract::wasmOfSize(75, "base")),
            contract::AddFunction("base", {}),
            Fee(fee));
        env.close();

        // The same holds for a source named by ContractHash.
        auto const otherHash = [&] {
            auto const code = contract::wasmOfSize(77, "other");
            setContract(
                env,
                tesSUCCESS,
                contract::create(alice, code),
                contract::AddFunction("other", {}),
                Fee(fee));
            return getContractHash(code);
        }();
        env(contract::modify(alice, abiImmutable, otherHash), Fee(fee), Ter(tecNO_PERMISSION));
        createWith(0, 76);
        env(contract::modify(
                alice, abiImmutable, getContractHash(contract::wasmOfSize(76, "base"))),
            Fee(fee));
        env.close();
        env(contract::modify(alice, abiImmutable, bob), Fee(fee));
        env.close();
    }

    void
    testModifySharedSources(FeatureBitset features)
    {
        testcase("modify keeps shared source reference counts");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // Two contracts share one source.
        auto const shared = contract::wasmOfSize(80, "base");
        auto const sharedHash = getContractHash(shared);
        auto const [one, oneHash, jv1] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, shared),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        auto const [two, twoHash, jv2] = setContract(
            env,
            tesSUCCESS,
            contract::create(bob, shared),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        BEAST_EXPECT(
            env.le(keylet::contractSource(sharedHash))->getFieldU64(sfReferenceCount) == 2);

        // Moving one to new code leaves the shared source with one reference.
        auto const fresh = contract::wasmOfSize(81, "base");
        env(contract::modify(alice, one, fresh), contract::AddFunction("base", {}), Fee(XRP(200)));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::contractSource(sharedHash))->getFieldU64(sfReferenceCount) == 1);
        BEAST_EXPECT(env.le(keylet::contractSource(getContractHash(fresh))));

        // Moving it back by hash takes a reference again and drops the fresh source's only one.
        env(contract::modify(alice, one, sharedHash), Fee(XRP(1)));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::contractSource(sharedHash))->getFieldU64(sfReferenceCount) == 2);
        BEAST_EXPECT(!env.le(keylet::contractSource(getContractHash(fresh))));

        // Moving it by hash to the fresh code again leaves the shared source with one.
        env(contract::modify(alice, one, fresh), contract::AddFunction("base", {}), Fee(XRP(200)));
        env.close();
        env(contract::modify(bob, two, getContractHash(fresh)), Fee(XRP(1)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::contractSource(sharedHash)));
        BEAST_EXPECT(
            env.le(keylet::contractSource(getContractHash(fresh)))->getFieldU64(sfReferenceCount) ==
            2);

        // Moving a third contract to the same code by ContractCode takes a reference too.
        auto const third = createCallable(env, alice);
        env(contract::modify(alice, third, fresh),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::contractSource(getContractHash(fresh)))->getFieldU64(sfReferenceCount) ==
            3);

        // Modifying a contract to the code it already runs leaves the count as it was.
        env(contract::modify(alice, third, fresh),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::contractSource(getContractHash(fresh)))->getFieldU64(sfReferenceCount) ==
            3);
    }

    void
    testDeleteChecks(FeatureBitset features)
    {
        testcase("delete preflight and preclaim");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createCallable(env, alice);
        auto const fee = env.current()->fees().increment;

        env(contract::del(alice, contractAccount),
            Txflags(tfBurnable),
            Fee(fee),
            Ter(temINVALID_FLAG));

        Account const nobody{"nobody"};
        env(contract::del(alice, nobody), Fee(fee), Ter(terNO_ACCOUNT));
        env(contract::del(alice, bob), Fee(fee), Ter(tecNO_PERMISSION));
        env(contract::del(bob, contractAccount), Fee(fee), Ter(tecNO_PERMISSION));

        auto const [undeletable, hash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contract::wasmOfSize(90, "base")),
            contract::AddFunction("base", {}),
            Txflags(tfUndeletable),
            Fee(XRP(200)));
        env(contract::del(alice, undeletable), Fee(fee), Ter(tecNO_PERMISSION));
    }

    void
    testUnimplementedTransactions(FeatureBitset features)
    {
        testcase("clawback and user delete are refused");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        auto const contractAccount = createCallable(env, alice);

        // Neither is implemented, so neither is accepted as a fee-paying no-op.
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ContractClawback;
            jv[jss::Account] = alice.human();
            jv[sfContractAccount] = contractAccount.human();
            jv[jss::Amount] = XRP(1).value().getJson(JsonOptions::Values::None);
            env(jv, Ter(temDISABLED));
            env(jv, Txflags(tfBurnable), Ter(temINVALID_FLAG));
        }
        {
            auto jv = contract::userDelete(alice, contractAccount);
            jv[sfGas] = 1'000;
            env(jv, Ter(temDISABLED));
            env(jv, Txflags(tfBurnable), Ter(temINVALID_FLAG));
        }
    }

    // Creates the storage contract owned by `owner`, sending it `funds`, and returns its
    // pseudo-account.
    jtx::Account
    createStorage(jtx::Env& env, jtx::Account const& owner, jtx::PrettyAmount const& funds)
    {
        using namespace jtx;
        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(owner, storageContractWasm_),
            contract::AddInstanceParam(tfSendAmount, "funds", "AMOUNT", funds),
            contract::AddFunction("set", {}),
            contract::AddFunction("unset", {}),
            contract::AddFunction("own", {}),
            contract::AddFunction("fill", {}),
            contract::AddFunction("pay", {}),
            contract::AddFunction("rekey", {}),
            contract::AddFunction("signers", {}),
            contract::AddFunction("event", {}),
            contract::AddFunction("handover", {}),
            contract::AddFunction("issue", {}),
            contract::AddFunction("mint", {}),
            contract::AddFunction("receive", {{tfSendAmount, "value", "AMOUNT"}}),
            Fee(XRP(200)));
        return contractAccount;
    }

    static std::uint32_t
    ownerCount(jtx::Env const& env, jtx::Account const& account)
    {
        return env.le(account)->getFieldU32(sfOwnerCount);
    }

    void
    testDataDeletion(FeatureBitset features)
    {
        testcase("a contract removes data and the reserve drops");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        auto const fee = callFeeFor(env, 1'000'000);
        auto const dataKeylet = keylet::contractData(bob.id(), contractAccount.id());

        env(contract::call(bob, contractAccount, "set"), escrow::Gas(1'000'000), Fee(fee));
        env.close();
        BEAST_EXPECT(env.le(dataKeylet));
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // An empty value removes the key; with no key left the entry goes and so does its
        // reserve.
        env(contract::call(bob, contractAccount, "unset"), escrow::Gas(1'000'000), Fee(fee));
        env.close();
        BEAST_EXPECT(!env.le(dataKeylet));
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    void
    testDataFinalizeReserve(FeatureBitset features)
    {
        testcase("data the owner cannot reserve is not written");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const fee = callFeeFor(env, 1'000'000);
        auto const fees = env.current()->fees();

        // Bob can afford one more reserve increment but not the two forty values need.
        env.fund(XRP(10'000), alice);
        env.fund(
            STAmount{fees.accountReserve(0, 1) + XRPAmount{fees.increment.drops() * 3 / 2} + fee},
            bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        env(contract::call(bob, contractAccount, "fill"),
            escrow::Gas(1'000'000),
            Fee(fee),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!env.le(keylet::contractData(bob.id(), contractAccount.id())));
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    void
    testDeleteBlockers(FeatureBitset features)
    {
        testcase("delete is blocked by what the contract holds, not by what its owner holds");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const fee = env.current()->fees().increment;

        // An escrow alice owns does not keep her from deleting her contract.
        env(escrow::create(alice, bob, XRP(10)), escrow::kFinishTime(env.now() + 100s));
        env.close();
        auto const plain = createCallable(env, alice);
        env(contract::del(alice, plain), Fee(fee));
        env.close();
        BEAST_EXPECT(!env.le(plain));

        // Data the contract holds for itself does.
        auto const holder = createStorage(env, alice, XRP(1'000));
        env(contract::call(bob, holder, "own"),
            escrow::Gas(1'000'000),
            Fee(callFeeFor(env, 1'000'000)));
        env.close();
        BEAST_EXPECT(env.le(keylet::contractData(holder.id(), holder.id())));
        env(contract::del(alice, holder), Fee(fee), Ter(tecHAS_OBLIGATIONS));
    }

    void
    testEmitOnNetwork(FeatureBitset features)
    {
        testcase("an emitted transaction carries the network's id");

        using namespace jtx;

        // A network id over 1024 makes NetworkID a required field.
        Env env{
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->networkId = 21338;
                return cfg;
            }),
            features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        auto const fee = callFeeFor(env, 1'000'000);
        auto const before = env.balance(bob);
        env(contract::call(bob, contractAccount, "pay"), escrow::Gas(1'000'000), Fee(fee));
        env.close();
        BEAST_EXPECT(env.balance(bob) == before - fee + XRP(1));

        // An event the contract emits is published when the call is applied.
        env(contract::call(bob, contractAccount, "event"), escrow::Gas(1'000'000), Fee(fee));
        env.close();
    }

    void
    testCallParameterChecks(FeatureBitset features)
    {
        testcase("call parameters that move value are checked");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        env.fund(XRP(10'000), alice, bob, gw);
        env.close();

        auto const contractAccount =
            createCallable(env, alice, 1, {{tfSendAmount, "value", "AMOUNT"}});
        auto const fee = callFeeFor(env, 1'000);
        auto const call = [&](std::uint32_t flag,
                              std::string const& type,
                              json::Value const& value,
                              TER const& expected) {
            env(contract::call(bob, contractAccount, "base"),
                contract::AddParam(flag, "value", type, value),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(expected));
        };
        auto const asJson = [](STAmount const& amount) {
            return amount.getJson(JsonOptions::Values::None);
        };

        // A call cannot take value from the contract, or send nothing.
        call(tfSendAmount, "AMOUNT", asJson(XRP(-10)), temBAD_AMOUNT);
        call(tfSendAmount, "AMOUNT", asJson(XRP(0)), temBAD_AMOUNT);
        call(tfSendAmount, "AMOUNT", asJson(gw["USD"](0)), temBAD_AMOUNT);
        call(
            tfSendAmount,
            "AMOUNT",
            asJson(STAmount{MPTIssue{makeMptID(1, gw.id())}, std::int64_t{0}}),
            temBAD_AMOUNT);
        call(tfAuthorizeToken, "AMOUNT", asJson(XRP(1)), temDISABLED);
        call(0x80000000, "AMOUNT", asJson(XRP(1)), temINVALID_FLAG);

        // A token whose currency code is "XRP" is not a token.
        {
            auto value = asJson(gw["USD"](1));
            value[jss::currency] = "0000000000000000000000005852500000000000";
            call(tfSendAmount, "AMOUNT", value, temBAD_CURRENCY);
        }

        // A send names what it sends.
        {
            auto jv = contract::call(bob, contractAccount, "base");
            json::Value param(json::ValueType::Object);
            param[sfParameter][sfParameterFlag] = tfSendAmount;
            jv[sfParameters].append(param);
            env(jv, escrow::Gas(1'000), Fee(fee), Ter(temMALFORMED));
        }

        // A token the caller holds no line for cannot be sent; nor can its issuer send it, as
        // the issuer holds no line to itself.
        call(tfSendAmount, "AMOUNT", asJson(gw["USD"](10)), tecNO_LINE);
        env(contract::call(gw, contractAccount, "base"),
            contract::AddParam(tfSendAmount, "value", "AMOUNT", asJson(gw["USD"](10))),
            escrow::Gas(1'000),
            Fee(fee),
            Ter(tecNO_LINE));
        env.close();

        // A parameter's flag is the ABI's: a send the ABI expects cannot be left out or mixed
        // with another flag, and one it does not expect cannot be added.
        call(0, "AMOUNT", asJson(XRP(1)), tecINVALID_PARAMETERS);
        call(tfSendAmount | tfSendNFToken, "AMOUNT", asJson(XRP(1)), tecINVALID_PARAMETERS);
        // Other code than the first contract's, so it has its own source and ABI.
        auto const plain = createCallable(env, alice, 2, {{0, "value", "AMOUNT"}});
        env(contract::call(bob, plain, "base"),
            contract::AddParam(tfSendAmount, "value", "AMOUNT", asJson(XRP(1))),
            escrow::Gas(1'000),
            Fee(fee),
            Ter(tecINVALID_PARAMETERS));
        call(tfSendAmount, "AMOUNT", asJson(XRP(1)), tesSUCCESS);
    }

    void
    testCallSendsNFToken(FeatureBitset features)
    {
        testcase("a call parameter sends an NFToken to the contract");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const nftID = token::getNextID(env, bob, 0, tfTransferable);
        env(token::mint(bob, 0), Txflags(tfTransferable));
        env.close();

        auto const contractAccount =
            createCallable(env, alice, 1, {{tfSendNFToken, "token", "UINT256"}});
        auto const fee = callFeeFor(env, 1'000);
        auto const send = [&](Account const& caller, TER const& expected) {
            env(contract::call(caller, contractAccount, "base"),
                contract::AddParam(tfSendNFToken, "token", "UINT256", to_string(nftID)),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(expected));
            env.close();
        };

        // Alice does not hold it.
        send(alice, tecNO_ENTRY);

        // Bob does; it moves even to a contract holding no XRP, since a pseudo-account carries
        // no reserve.
        send(bob, tesSUCCESS);

        // The contract holds it now, so bob cannot offer it and the contract cannot be deleted.
        env(token::createOffer(bob, nftID, XRP(1)), Txflags(tfSellNFToken), Ter(tecNO_ENTRY));
        env(contract::del(alice, contractAccount),
            Fee(env.current()->fees().increment),
            Ter(tecHAS_OBLIGATIONS));

        // A token named as an instance parameter moves with the create, which holds the new
        // pseudo-account to the reserve for it: refused alone, accepted with XRP sent first.
        auto const secondID = token::getNextID(env, bob, 0, tfTransferable);
        env(token::mint(bob, 0), Txflags(tfTransferable));
        env.close();
        env(contract::create(bob, contract::wasmOfSize(66, "base")),
            contract::AddInstanceParam(tfSendNFToken, "token", "UINT256", to_string(secondID)),
            contract::AddFunction("base", {}),
            Fee(XRP(200)),
            Ter(tecINSUFFICIENT_RESERVE));
        env(contract::create(bob, contract::wasmOfSize(67, "base")),
            contract::AddInstanceParam(tfSendAmount, "funds", "AMOUNT", XRP(1'000)),
            contract::AddInstanceParam(tfSendNFToken, "token", "UINT256", to_string(secondID)),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        env.close();
        env(token::createOffer(bob, secondID, XRP(1)), Txflags(tfSellNFToken), Ter(tecNO_ENTRY));
    }

    void
    testCallParameterFunds(FeatureBitset features)
    {
        testcase("what a call's parameters send must be spendable together");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), alice, bob, gw);
        env.close();
        env(trust(bob, usd(1'000)));
        env(pay(gw, bob, usd(15)));
        env(rate(gw, 1.5));
        env.close();

        auto const both = createCallable(
            env, alice, 1, {{tfSendAmount, "first", "AMOUNT"}, {tfSendAmount, "second", "AMOUNT"}});
        auto const one = createCallable(
            env, alice, 2, {{tfSendAmount, "first", "AMOUNT"}, {0, "second", "AMOUNT"}});
        auto const fee = callFeeFor(env, 1'000);
        auto const send = [&](std::uint32_t firstFlag,
                              STAmount const& first,
                              std::uint32_t secondFlag,
                              STAmount const& second,
                              TER const& expected) {
            env(contract::call(bob, secondFlag == 0 ? one : both, "base"),
                contract::AddParam(
                    firstFlag, "first", "AMOUNT", first.getJson(JsonOptions::Values::None)),
                contract::AddParam(
                    secondFlag, "second", "AMOUNT", second.getJson(JsonOptions::Values::None)),
                escrow::Gas(1'000),
                Fee(fee),
                Ter(expected));
            env.close();
        };

        // Each send alone is affordable; together they are not.
        send(tfSendAmount, XRP(6'000), tfSendAmount, XRP(6'000), tecUNFUNDED);
        send(tfSendAmount, usd(10), tfSendAmount, usd(10), tecINSUFFICIENT_FUNDS);

        // The issuer's transfer fee is part of what is spent: 12 USD costs bob 18.
        send(tfSendAmount, usd(12), 0, XRP(1), tecINSUFFICIENT_FUNDS);

        // 5 USD costs 7.5, which bob has; the contract receives 5.
        send(tfSendAmount, usd(5), 0, XRP(1), tesSUCCESS);
        BEAST_EXPECT(env.balance(one, usd) == usd(5));
        BEAST_EXPECT(env.balance(bob, usd) == usd(7.5));
    }

    void
    testEmitTxnEmittance(FeatureBitset features)
    {
        testcase("a contract cannot emit a type it may not build");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        env(contract::call(bob, contractAccount, "rekey"),
            escrow::Gas(1'000'000),
            Fee(callFeeFor(env, 1'000'000)),
            Ter(tecBYTECODE_REJECTED));
        env.close();
        BEAST_EXPECT(!env.le(contractAccount)->isFieldPresent(sfRegularKey));

        // Nor a signer list, which no invariant would catch.
        env(contract::call(bob, contractAccount, "signers"),
            escrow::Gas(1'000'000),
            Fee(callFeeFor(env, 1'000'000)),
            Ter(tecBYTECODE_REJECTED));
        env.close();
        BEAST_EXPECT(!env.le(keylet::signerList(contractAccount.id())));
    }

    void
    testInstallFunctions(FeatureBitset features)
    {
        testcase("installing existing code keeps its ABI or is refused");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();

        auto const code = contract::wasmOfSize(69, "base");
        auto const hash = getContractHash(code);
        setContract(
            env,
            tesSUCCESS,
            contract::create(alice, code),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));

        // Functions that differ from the stored ABI are refused, by code or by hash.
        env(contract::create(alice, code),
            contract::AddFunction("base", {{0, "v", "UINT8"}}),
            Fee(XRP(200)),
            Ter(temMALFORMED));
        env(contract::create(alice, hash),
            contract::AddFunction("other", {}),
            Fee(XRP(200)),
            Ter(temMALFORMED));

        // The same Functions, or none by hash, install the code.
        env(contract::create(alice, code), contract::AddFunction("base", {}), Fee(XRP(200)));
        env(contract::create(alice, hash), Fee(XRP(200)));
        env.close();
        BEAST_EXPECT(env.le(keylet::contractSource(hash))->getFieldU64(sfReferenceCount) == 3);

        // A modify that moves a contract to that code follows the same rule.
        auto const other = createCallable(env, alice, 3);
        env(contract::modify(alice, other, code),
            contract::AddFunction("base", {{0, "v", "UINT8"}}),
            Fee(XRP(200)),
            Ter(temMALFORMED));
        env(contract::modify(alice, other, code), contract::AddFunction("base", {}), Fee(XRP(200)));
        env.close();
        BEAST_EXPECT(env.le(keylet::contractSource(hash))->getFieldU64(sfReferenceCount) == 4);
    }

    void
    testParameterShapes(FeatureBitset features)
    {
        testcase("parameters are the shape their flag and ABI say");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const fee = callFeeFor(env, 1'000);
        auto const asJson = [](STAmount const& amount) {
            return amount.getJson(JsonOptions::Values::None);
        };
        auto const callWith = [&](jtx::Account const& contractAccount,
                                  json::Value const& param,
                                  TER const& expected) {
            auto jv = contract::call(bob, contractAccount, "base");
            jv[sfParameters].append(param);
            env(jv, escrow::Gas(1'000), Fee(fee), Ter(expected));
        };
        auto const param = [](std::optional<std::uint32_t> flag,
                              std::string const& type,
                              std::optional<json::Value> const& value) {
            json::Value p(json::ValueType::Object);
            if (flag)
                p[sfParameter][sfParameterFlag] = *flag;
            if (value)
            {
                p[sfParameter][sfParameterValue][jss::type] = type;
                p[sfParameter][sfParameterValue][jss::value] = *value;
            }
            else
            {
                p[sfParameter] = json::Value(json::ValueType::Object);
                if (flag)
                    p[sfParameter][sfParameterFlag] = *flag;
            }
            return p;
        };

        // A send holds what it sends: an amount, or an NFToken id.
        auto const sends = createCallable(env, alice, 1, {{tfSendAmount, "value", "AMOUNT"}});
        callWith(sends, param(tfSendAmount, "UINT8", json::Value(1)), temMALFORMED);
        auto const gives = createCallable(env, alice, 2, {{tfSendNFToken, "token", "UINT256"}});
        callWith(gives, param(tfSendNFToken, "", std::nullopt), temMALFORMED);
        callWith(gives, param(tfSendNFToken, "AMOUNT", asJson(XRP(1))), temMALFORMED);

        // A parameter without a flag is a plain one, and its type must still be the ABI's.
        auto const plain = createCallable(env, alice, 3, {{0, "value", "AMOUNT"}});
        callWith(plain, param(std::nullopt, "AMOUNT", asJson(XRP(1))), tesSUCCESS);
        callWith(plain, param(0, "UINT8", json::Value(1)), tecINVALID_PARAMETERS);

        // A call may leave Parameters out altogether.
        auto const bare = createCallable(env, alice, 4);
        {
            auto jv = contract::call(bob, bare, "base");
            jv.removeMember(sfParameters.jsonName);
            env(jv, escrow::Gas(1'000), Fee(fee));
        }

        // Instance parameters and their values are checked the same way.
        auto const code = contract::wasmOfSize(70, "base");
        env(contract::create(alice, code),
            contract::AddInstanceParam(0x80000000, "v", "UINT8", 1),
            contract::AddFunction("base", {}),
            Fee(XRP(200)),
            Ter(temINVALID_FLAG));
        env(contract::create(alice, code),
            contract::AddInstanceParam(tfSendAmount, "v", "AMOUNT", asJson(XRP(-1))),
            contract::AddFunction("base", {}),
            Fee(XRP(200)),
            Ter(temBAD_AMOUNT));
        {
            auto jt = env.jt(
                contract::create(alice, code),
                contract::AddInstanceParam(0, "a", "UINT8", 1),
                contract::AddInstanceParam(0, "b", "UINT8", 2),
                contract::AddFunction("base", {}));
            json::Value values(json::ValueType::Array);
            values.append(jt.jv[sfInstanceParameterValues][0u]);
            jt.jv[sfInstanceParameterValues] = values;
            env(jt.jv, Fee(XRP(200)), Ter(temMALFORMED));
        }

        // By hash only the values travel; they are checked on their own.
        auto const [withParams, paramsHash, created] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, contract::wasmOfSize(71, "base")),
            contract::AddInstanceParam(0, "v", "UINT8", 1),
            contract::AddFunction("base", {}),
            Fee(XRP(200)));
        env(contract::create(alice, paramsHash),
            contract::AddInstanceParam(0x80000000, "v", "UINT8", 1),
            Fee(XRP(200)),
            Ter(temINVALID_FLAG));
        {
            auto jt = env.jt(contract::create(alice, paramsHash));
            for (int i = 0; i < 9; ++i)
                contract::AddInstanceParam(0, "v", "UINT8", 1)(env, jt);
            env(jt.jv, Fee(XRP(200)), Ter(temARRAY_TOO_LARGE));
        }
    }

    void
    testContractModifiesItself(FeatureBitset features)
    {
        testcase("a contract modifies itself; an account names no contract of its own");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // A ContractModify without ContractAccount is for the sender's own contract: an
        // ordinary account has none.
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::ContractModify;
            jv[jss::Account] = alice.human();
            jv[sfOwner] = bob.human();
            env(jv, Fee(env.current()->fees().increment), Ter(tecNO_TARGET));
        }

        // A contract's pseudo-account does, so the contract can hand itself to the caller.
        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        auto const contractID = env.le(contractAccount)->getFieldH256(sfContractID);
        BEAST_EXPECT(env.le(keylet::contract(contractID))->getAccountID(sfOwner) == alice.id());
        env(contract::call(bob, contractAccount, "handover"),
            escrow::Gas(1'000'000),
            Fee(callFeeFor(env, 1'000'000)));
        env.close();
        BEAST_EXPECT(env.le(keylet::contract(contractID))->getAccountID(sfOwner) == bob.id());
    }

    void
    testTokenTheContractIssues(FeatureBitset features)
    {
        testcase("a contract's own MPT sent back to it carries no transfer fee");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        auto const fee = callFeeFor(env, 1'000'000);
        auto const callFn = [&](char const* name) {
            env(contract::call(bob, contractAccount, name), escrow::Gas(1'000'000), Fee(fee));
            env.close();
        };

        // The contract issues an MPT with its first sequence and pays bob 5 of it.
        callFn("issue");
        auto const mptID = makeMptID(0, contractAccount.id());
        BEAST_EXPECT(env.le(keylet::mptokenIssuance(mptID)));
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::MPTokenAuthorize;
            jv[jss::Account] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptID);
            env(jv);
            env.close();
        }
        callFn("mint");
        BEAST_EXPECT(env.le(keylet::mptoken(mptID, bob.id()))->getFieldU64(sfMPTAmount) == 5);

        // Sent back to the contract, which issued it, 2 costs bob exactly 2.
        env(contract::call(bob, contractAccount, "receive"),
            contract::AddParam(
                tfSendAmount,
                "value",
                "AMOUNT",
                STAmount{MPTIssue{mptID}, std::int64_t{2}}.getJson(JsonOptions::Values::None)),
            escrow::Gas(1'000'000),
            Fee(fee));
        env.close();
        BEAST_EXPECT(env.le(keylet::mptoken(mptID, bob.id()))->getFieldU64(sfMPTAmount) == 3);
        BEAST_EXPECT(env.le(keylet::mptokenIssuance(mptID))->getFieldU64(sfOutstandingAmount) == 3);
    }

    void
    testContractEntriesOverRpc(FeatureBitset features)
    {
        testcase("contract entries over ledger_entry");

        using namespace jtx;

        Env env{*this, features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const [contractAccount, contractHash, jv] = setContract(
            env,
            tesSUCCESS,
            contract::create(alice, storageContractWasm_),
            contract::AddFunction("set", {}),
            Fee(XRP(200)));
        auto const seq = jv[sfSequence.jsonName].asUInt();
        env(contract::call(bob, contractAccount, "set"),
            escrow::Gas(1'000'000),
            Fee(callFeeFor(env, 1'000'000)));
        env.close();

        auto const ledgerHash = to_string(env.closed()->header().hash);
        auto const ledgerEntry = [&](json::Value params) {
            params[jss::ledger_hash] = ledgerHash;
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };
        auto const expectError = [&](json::Value const& params, std::string const& error) {
            auto const result = ledgerEntry(params);
            BEAST_EXPECTS(
                result[jss::status] == "error" && result[jss::error] == error, to_string(result));
        };
        auto const hashHex = to_string(contractHash);

        // Each entry is found by its fields and again by its index.
        auto const found = [&](json::StaticString field,
                               json::Value const& fields,
                               uint256 const& key,
                               std::string const& type) {
            json::Value params;
            params[field] = fields;
            auto const result = ledgerEntry(params);
            BEAST_EXPECTS(result[jss::node][sfLedgerEntryType.jsonName] == type, to_string(result));
            BEAST_EXPECT(result[jss::index] == to_string(key));

            json::Value byIndex;
            byIndex[field] = to_string(key);
            BEAST_EXPECT(ledgerEntry(byIndex)[jss::node] == result[jss::node]);
        };

        json::Value sourceFields;
        sourceFields[jss::contract_hash] = hashHex;
        found(
            jss::contract_source,
            sourceFields,
            keylet::contractSource(contractHash).key,
            "ContractSource");

        json::Value contractFields;
        contractFields[jss::contract_hash] = hashHex;
        contractFields[jss::owner] = alice.human();
        contractFields[jss::seq] = seq;
        found(
            jss::contract,
            contractFields,
            keylet::contract(contractHash, alice.id(), seq).key,
            "Contract");

        json::Value dataFields;
        dataFields[jss::owner] = bob.human();
        dataFields[jss::contract_account] = contractAccount.human();
        auto const dataKey = keylet::contractData(bob.id(), contractAccount.id()).key;
        found(jss::contract_data, dataFields, dataKey, "ContractData");

        // The stored data decodes from the entry's binary form.
        {
            json::Value params;
            params[jss::contract_data] = dataFields;
            params[jss::binary] = true;
            auto const blob = strUnHex(ledgerEntry(params)[jss::node_binary].asString());
            BEAST_EXPECT(blob.has_value());
            if (blob)
            {
                SerialIter sit(makeSlice(*blob));
                STLedgerEntry const decoded(sit, dataKey);
                BEAST_EXPECT(
                    decoded.getFieldJson(sfContractJson)
                        .isEquivalent(env.le(keylet::contractData(bob.id(), contractAccount.id()))
                                          ->getFieldJson(sfContractJson)));
            }
        }

        // Each field missing, null or malformed.
        for (auto const field : {jss::contract_source, jss::contract, jss::contract_data})
        {
            for (json::Value const& bad : {json::Value(1), json::Value("XYZ"), json::Value(true)})
            {
                json::Value params;
                params[field] = bad;
                expectError(params, "malformedRequest");
            }
        }
        for (auto const& [field, fields] : {
                 std::pair{jss::contract_source, sourceFields},
                 std::pair{jss::contract, contractFields},
                 std::pair{jss::contract_data, dataFields},
             })
        {
            for (auto const& name : fields.getMemberNames())
            {
                json::Value params;
                params[field] = fields;
                params[field].removeMember(name);
                expectError(params, "malformedRequest");
                params[field][name] = json::ValueType::Null;
                expectError(params, "malformedRequest");
                params[field][name] = "XYZ";
                std::string error = "malformedRequest";
                if (name == jss::owner.cStr())
                    error = "malformedOwner";
                if (name == jss::contract_account.cStr())
                    error = "malformedContractAccount";
                expectError(params, error);
            }
        }

        // Data an account does not hold is not found.
        {
            json::Value params;
            params[jss::contract_data] = dataFields;
            params[jss::contract_data][jss::owner] = alice.human();
            expectError(params, "entryNotFound");
        }

        // An index of one kind of entry asked for as another is the wrong type.
        {
            json::Value params;
            params[jss::contract] = to_string(keylet::contractSource(contractHash).key);
            expectError(params, "unexpectedLedgerType");
            params.removeMember(jss::contract);
            params[jss::contract_data] = to_string(keylet::contractSource(contractHash).key);
            expectError(params, "unexpectedLedgerType");
            params.removeMember(jss::contract_data);
            params[jss::contract_source] = to_string(dataKey);
            expectError(params, "unexpectedLedgerType");
        }
    }

    void
    testContractEventStream(FeatureBitset features)
    {
        testcase("contract_events stream");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, singleThreadIo(envconfig()), features};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        auto const contractAccount = createStorage(env, alice, XRP(1'000));
        auto const callEvent = [&] {
            env(contract::call(bob, contractAccount, "event"),
                escrow::Gas(1'000'000),
                Fee(callFeeFor(env, 1'000'000)));
            env.close();
        };
        auto const isPing = [](json::Value const& msg) {
            return msg[jss::type] == "contractEvent" && msg[jss::name] == "ping";
        };

        json::Value stream;
        stream[jss::streams] = json::ValueType::Array;
        stream[jss::streams].append("contract_events");

        auto wsc = makeWSClient(env.app().config());
        BEAST_EXPECT(wsc->invoke("subscribe", stream)[jss::status] == "success");

        // A subscriber that goes away without unsubscribing is dropped on the next event.
        {
            auto gone = makeWSClient(env.app().config());
            BEAST_EXPECT(gone->invoke("subscribe", stream)[jss::status] == "success");
        }

        callEvent();
        auto const msg = wsc->findMsg(5s, isPing);
        BEAST_EXPECT(msg && (*msg)[jss::data].isObject());

        // Once unsubscribed, no event arrives.
        BEAST_EXPECT(wsc->invoke("unsubscribe", stream)[jss::status] == "success");
        while (wsc->getMsg(100ms))
        {
        }
        callEvent();
        BEAST_EXPECT(!wsc->findMsg(1s, isPing));
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
        testCreateUploadFee(features);
        testCodeSizeLimit(features);
        testCodeIsScreened(features);
        testCallGasLimit(features);
        testModifyToMissingSource(features);
        testCreateFlagsAndParameters(features);
        testCallPreflightAndPreclaim(features);
        testCallApply(features);
        testModifyChecks(features);
        testModifyImmutability(features);
        testModifySharedSources(features);
        testDeleteChecks(features);
        testUnimplementedTransactions(features);
        testDataDeletion(features);
        testDataFinalizeReserve(features);
        testDeleteBlockers(features);
        testEmitOnNetwork(features);
        testCallParameterChecks(features);
        testCallSendsNFToken(features);
        testCallParameterFunds(features);
        testEmitTxnEmittance(features);
        testInstallFunctions(features);
        testParameterShapes(features);
        testContractModifiesItself(features);
        testTokenTheContractIssues(features);
        testContractEntriesOverRpc(features);
        testContractEventStream(features);
        // testUserDeletePreflight(features);
        // testUserDeletePreclaim(features);
        // testUserDeleteDoApply(features);
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

}  // namespace xrpl::test
