#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/credentials.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/token.h>

#include <xrpld/app/rdb/backend/SQLiteDatabase.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

class Simulate_test : public beast::unit_test::Suite
{
    void
    checkBasicReturnValidity(
        json::Value const& result,
        json::Value const& tx,
        int const expectedSequence,
        std::string const& expectedFee)
    {
        BEAST_EXPECT(result[jss::applied] == false);
        BEAST_EXPECT(result.isMember(jss::engine_result));
        BEAST_EXPECT(result.isMember(jss::engine_result_code));
        BEAST_EXPECT(result.isMember(jss::engine_result_message));
        BEAST_EXPECT(result.isMember(jss::tx_json) || result.isMember(jss::tx_blob));

        json::Value txJson;
        if (result.isMember(jss::tx_json))
        {
            txJson = result[jss::tx_json];
        }
        else
        {
            auto const unHexed = strUnHex(result[jss::tx_blob].asString());
            SerialIter sitTrans(makeSlice(*unHexed));  // NOLINT(bugprone-unchecked-optional-access)
            txJson = STObject(std::ref(sitTrans), sfGeneric).getJson(JsonOptions::Values::None);
        }
        BEAST_EXPECT(txJson[jss::TransactionType] == tx[jss::TransactionType]);
        BEAST_EXPECT(txJson[jss::Account] == tx[jss::Account]);
        BEAST_EXPECT(txJson[jss::SigningPubKey] == tx.get(jss::SigningPubKey, ""));
        BEAST_EXPECT(txJson[jss::TxnSignature] == tx.get(jss::TxnSignature, ""));
        BEAST_EXPECT(txJson[jss::Fee] == tx.get(jss::Fee, expectedFee));
        BEAST_EXPECT(txJson[jss::Sequence] == tx.get(jss::Sequence, expectedSequence));
    }

    void
    checkBasicReturnValidity(
        json::Value const& result,
        json::Value const& tx,
        int const expectedSequence,
        XRPAmount const& expectedFee)
    {
        checkBasicReturnValidity(
            result, tx, expectedSequence, expectedFee.jsonClipped().asString());
    }

    void
    testTx(
        jtx::Env& env,
        json::Value const& tx,
        std::function<void(json::Value const&, json::Value const&)> const& validate,
        bool testSerialized = true)
    {
        env.close();

        json::Value params;
        params[jss::tx_json] = tx;
        validate(env.rpc("json", "simulate", to_string(params)), tx);

        params[jss::binary] = true;
        validate(env.rpc("json", "simulate", to_string(params)), tx);
        validate(env.rpc("simulate", to_string(tx)), tx);
        validate(env.rpc("simulate", to_string(tx), "binary"), tx);

        if (testSerialized)
        {
            // This cannot be tested in the multisign autofill scenario
            // It is technically not a valid STObject, so the following line
            // will crash
            STParsedJSONObject const parsed(std::string(jss::tx_json), tx);
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            auto const txBlob = strHex(parsed.object->getSerializer().peekData());
            if (BEAST_EXPECT(parsed.object.has_value()))
            {
                json::Value params;
                params[jss::tx_blob] = txBlob;
                validate(env.rpc("json", "simulate", to_string(params)), tx);
                params[jss::binary] = true;
                validate(env.rpc("json", "simulate", to_string(params)), tx);
            }
            validate(env.rpc("simulate", txBlob), tx);
            validate(env.rpc("simulate", txBlob, "binary"), tx);
        }

        BEAST_EXPECTS(env.current()->txCount() == 0, std::to_string(env.current()->txCount()));
    }

    void
    testTxJsonMetadataField(
        jtx::Env& env,
        json::Value const& tx,
        std::function<void(
            json::Value const&,
            json::Value const&,
            json::Value const&,
            json::Value const&)> const& validate,
        json::Value const& expectedMetadataKey,
        json::Value const& expectedMetadataValue)
    {
        env.close();

        json::Value params;
        params[jss::tx_json] = tx;
        validate(
            env.rpc("json", "simulate", to_string(params)),
            tx,
            expectedMetadataKey,
            expectedMetadataValue);
        validate(
            env.rpc("simulate", to_string(tx)), tx, expectedMetadataKey, expectedMetadataValue);

        BEAST_EXPECTS(env.current()->txCount() == 0, std::to_string(env.current()->txCount()));
    }

    static json::Value
    getJsonMetadata(json::Value txResult)
    {
        if (txResult.isMember(jss::meta_blob))
        {
            auto unHexed = strUnHex(txResult[jss::meta_blob].asString());
            SerialIter sitTrans(makeSlice(*unHexed));  // NOLINT(bugprone-unchecked-optional-access)
            return STObject(std::ref(sitTrans), sfGeneric).getJson(JsonOptions::Values::None);
        }

        return txResult[jss::meta];
    }

    void
    testParamErrors()
    {
        testcase("Test parameter errors");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");

        {
            // No params
            json::Value const params = json::ValueType::Object;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Neither `tx_blob` nor `tx_json` included.");
        }
        {
            // Providing both `tx_json` and `tx_blob`
            json::Value params = json::ValueType::Object;
            params[jss::tx_json] = json::ValueType::Object;
            params[jss::tx_blob] = "1200";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Can only include one of `tx_blob` and `tx_json`.");
        }
        {
            // `binary` isn't a boolean
            json::Value params = json::ValueType::Object;
            params[jss::tx_blob] = "1200";
            params[jss::binary] = "100";
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'binary'.");
        }
        {
            // Invalid `tx_blob`
            json::Value params = json::ValueType::Object;
            params[jss::tx_blob] = "12";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx_blob'.");
        }
        {
            // Empty `tx_json`
            json::Value params = json::ValueType::Object;
            params[jss::tx_json] = json::ValueType::Object;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Missing field 'tx.TransactionType'.");
        }
        {
            // No tx.Account
            json::Value params = json::ValueType::Object;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::Payment;
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Missing field 'tx.Account'.");
        }
        {
            // Empty `tx_blob`
            json::Value params = json::ValueType::Object;
            params[jss::tx_blob] = "";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx_blob'.");
        }
        {
            // Non-string `tx_blob`
            json::Value params;
            params[jss::tx_blob] = 1.1;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx_blob'.");
        }
        {
            // Non-object `tx_json`
            json::Value params = json::ValueType::Object;
            params[jss::tx_json] = "";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Invalid field 'tx_json', not object.");
        }
        {
            // `seed` field included
            json::Value params = json::ValueType::Object;
            params[jss::seed] = "random_data";
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            params[jss::tx_json] = txJson;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'seed'.");
        }
        {
            // `secret` field included
            json::Value params = json::ValueType::Object;
            params[jss::secret] = "random_data";
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            params[jss::tx_json] = txJson;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'secret'.");
        }
        {
            // `seed_hex` field included
            json::Value params = json::ValueType::Object;
            params[jss::seed_hex] = "random_data";
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            params[jss::tx_json] = txJson;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'seed_hex'.");
        }
        {
            // `passphrase` field included
            json::Value params = json::ValueType::Object;
            params[jss::passphrase] = "random_data";
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            params[jss::tx_json] = txJson;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'passphrase'.");
        }
        {
            // Invalid transaction
            json::Value params = json::ValueType::Object;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::Payment;
            txJson[jss::Account] = env.master.human();
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_exception] ==
                "Field 'Destination' is required but missing.");
        }
        {
            // Bad account
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = "badAccount";
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECTS(
                resp[jss::result][jss::error] == "srcActMalformed",
                resp[jss::result][jss::error].toStyledString());
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx.Account'.");
        }
        {
            // Account doesn't exist for Sequence autofill
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = alice.human();
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Source account not found.");
        }
        {
            // Invalid Signers field
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            txJson[sfSigners] = "1";
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx.Signers'.");
        }
        {
            // Invalid Signers field
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            txJson[sfSigners] = json::ValueType::Array;
            txJson[sfSigners].append("1");
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error_message] == "Invalid field 'tx.Signers[0]'.");
        }
        {
            // Invalid transaction
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            txJson["foo"] = "bar";
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Field 'tx_json.foo' is unknown.");
        }
        {
            // non-`"binary"` second param for CLI
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = alice.human();
            auto const resp = env.rpc("simulate", to_string(txJson), "1");
            BEAST_EXPECT(resp[jss::error_message] == "Invalid parameters.");
        }
        {
            // Signed transaction
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            txJson[jss::TxnSignature] = "1200ABCD";
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Transaction should not be signed.");
        }
        {
            // Signed multisig transaction
            json::Value params;
            json::Value txJson = json::ValueType::Object;
            txJson[jss::TransactionType] = jss::AccountSet;
            txJson[jss::Account] = env.master.human();
            txJson[sfSigners] = json::ValueType::Array;
            {
                json::Value signer;
                signer[jss::Account] = alice.human();
                signer[jss::SigningPubKey] = alice.human();
                signer[jss::TxnSignature] = "1200ABCD";
                json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                txJson[sfSigners].append(signerOuter);
            }
            params[jss::tx_json] = txJson;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Transaction should not be signed.");
        }
    }

    void
    testFeeError()
    {
        testcase("Fee failure");

        using namespace jtx;

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section(Sections::kTransactionQueue).set(Keys::kMinimumTxnInLedgerStandalone, "3");
            return cfg;
        }));

        Account const alice{"alice"};
        env.fund(XRP(1000000), alice);
        env.close();

        // fill queue
        auto metrics = env.app().getTxQ().getMetrics(*env.current());
        for (int i = metrics.txInLedger; i <= metrics.txPerLedger; ++i)
            env(noop(alice));

        {
            json::Value params;
            params[jss::tx_json] = noop(alice);

            auto const resp = env.rpc("json", "simulate", to_string(params));
            auto const result = resp[jss::result];
            if (BEAST_EXPECT(result.isMember(jss::error)))
            {
                BEAST_EXPECT(result[jss::error] == "highFee");
                BEAST_EXPECT(result[jss::error_code] == RpcHighFee);
            }
        }
    }

    void
    testInvalidTransactionType()
    {
        testcase("Invalid transaction type");

        using namespace jtx;

        Env env(*this);

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1000000), alice, bob);
        env.close();

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        auto const seq = env.seq(alice);
        auto jt = env.jtnofill(
            batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
            batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
            batch::Inner(pay(alice, bob, XRP(10)), seq + 1));

        jt.jv.removeMember(jss::TxnSignature);
        json::Value params;
        params[jss::tx_json] = jt.jv;
        auto const resp = env.rpc("json", "simulate", to_string(params));
        BEAST_EXPECT(resp[jss::result][jss::error] == "notImpl");
        BEAST_EXPECT(resp[jss::result][jss::error_message] == "Not implemented.");
    }

    void
    testSuccessfulTransaction()
    {
        testcase("Successful transaction");

        using namespace jtx;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->networkId = 0;
                    return cfg;
                })};
        static auto const kNewDomain = "123ABC";

        {
            auto validateOutput = [&](json::Value const& resp, json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(result, tx, 1, env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                {
                    json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(modifiedNode[sfLedgerEntryType] == "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == kNewDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;

            // test with autofill
            testTx(env, tx, validateOutput);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = 1;
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();

            // test without autofill
            testTx(env, tx, validateOutput);
        }
    }

    void
    testTransactionNonTecFailure()
    {
        testcase("Transaction non-tec failure");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");

        {
            std::function<void(json::Value const&, json::Value const&)> const& testSimulation =
                [&](json::Value const& resp, json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(result, tx, 1, env.current()->fees().base);

                    BEAST_EXPECT(result[jss::engine_result] == "temBAD_AMOUNT");
                    BEAST_EXPECT(result[jss::engine_result_code] == -298);
                    BEAST_EXPECT(result[jss::engine_result_message] == "Malformed: Bad amount.");

                    BEAST_EXPECT(!result.isMember(jss::meta) && !result.isMember(jss::meta_blob));
                };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::Payment;
            tx[sfDestination] = alice.human();
            tx[sfAmount] = "0";  // invalid amount

            // test with autofill
            testTx(env, tx, testSimulation);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = 1;
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();

            // test without autofill
            testTx(env, tx, testSimulation);
        }
    }

    void
    testTransactionTecFailure()
    {
        testcase("Transaction tec failure");

        using namespace jtx;
        Env env(*this);
        Account const alice("alice");

        {
            std::function<void(json::Value const&, json::Value const&)> const& testSimulation =
                [&](json::Value const& resp, json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(result, tx, 1, env.current()->fees().base);

                    BEAST_EXPECT(result[jss::engine_result] == "tecNO_DST_INSUF_XRP");
                    BEAST_EXPECT(result[jss::engine_result_code] == 125);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "Destination does not exist. Too little XRP sent to "
                        "create "
                        "it.");

                    if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                    {
                        json::Value const metadata = getJsonMetadata(result);

                        if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                        {
                            BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].size() == 1);
                            auto node = metadata[sfAffectedNodes.jsonName][0u];
                            if (BEAST_EXPECT(node.isMember(sfModifiedNode.jsonName)))
                            {
                                auto modifiedNode = node[sfModifiedNode];
                                BEAST_EXPECT(modifiedNode[sfLedgerEntryType] == "AccountRoot");
                                auto finalFields = modifiedNode[sfFinalFields];
                                BEAST_EXPECT(
                                    finalFields[sfBalance] ==
                                    std::to_string(
                                        100'000'000'000'000'000 -
                                        env.current()->fees().base.drops()));
                            }
                        }
                        BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                        BEAST_EXPECT(
                            metadata[sfTransactionResult.jsonName] == "tecNO_DST_INSUF_XRP");
                    }
                };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::Payment;
            tx[sfDestination] = alice.human();
            tx[sfAmount] = "1";  // not enough to create an account

            // test with autofill
            testTx(env, tx, testSimulation);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = 1;
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();

            // test without autofill
            testTx(env, tx, testSimulation);
        }
    }

    void
    testSuccessfulTransactionMultisigned()
    {
        testcase("Successful multi-signed transaction");

        using namespace jtx;
        Env env(*this);
        static auto const kNewDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));
        env.close();

        {
            auto validateOutput = [&](json::Value const& resp, json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(
                    result,
                    tx,
                    env.seq(alice),
                    tx.isMember(jss::Signers) ? env.current()->fees().base * 2
                                              : env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                {
                    json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(modifiedNode[sfLedgerEntryType] == "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == kNewDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            json::Value tx;

            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;

            // test with autofill
            testTx(env, tx, validateOutput, false);

            tx[sfSigners] = json::ValueType::Array;
            {
                json::Value signer;
                signer[jss::Account] = becky.human();
                json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with just signer accounts
            testTx(env, tx, validateOutput, false);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(alice);
            // transaction requires a non-base fee
            tx[sfFee] = (env.current()->fees().base * 2).jsonClipped().asString();
            tx[sfSigners][0u][sfSigner][jss::SigningPubKey] = "";
            tx[sfSigners][0u][sfSigner][jss::TxnSignature] = "";

            // test without autofill
            testTx(env, tx, validateOutput);
        }
    }

    void
    testTransactionSigningFailure()
    {
        testcase("Transaction with a key-related failure");

        using namespace jtx;
        Env env(*this);
        static auto const kNewDomain = "123ABC";
        Account const alice{"alice"};
        env(regkey(env.master, alice));
        env(fset(env.master, asfDisableMaster), Sig(env.master));
        env.close();

        {
            std::function<void(json::Value const&, json::Value const&)> const& testSimulation =
                [&](json::Value const& resp, json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(
                        result, tx, env.seq(env.master), env.current()->fees().base);

                    BEAST_EXPECT(result[jss::engine_result] == "tefMASTER_DISABLED");
                    BEAST_EXPECT(result[jss::engine_result_code] == -188);
                    BEAST_EXPECT(result[jss::engine_result_message] == "Master key is disabled.");

                    BEAST_EXPECT(!result.isMember(jss::meta) && !result.isMember(jss::meta_blob));
                };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;
            // master key is disabled, so this is invalid
            tx[jss::SigningPubKey] = strHex(env.master.pk().slice());

            // test with autofill
            testTx(env, tx, testSimulation);

            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(env.master);
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();

            // test without autofill
            testTx(env, tx, testSimulation);
        }
    }

    void
    testInvalidSingleAndMultiSigningTransaction()
    {
        testcase(
            "Transaction with both single-signing SigningPubKey and "
            "multi-signing Signers");

        using namespace jtx;
        Env env(*this);
        static auto const kNewDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));
        env.close();

        {
            std::function<void(json::Value const&, json::Value const&)> const& testSimulation =
                [&](json::Value const& resp, json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(
                        result, tx, env.seq(env.master), env.current()->fees().base * 2);

                    BEAST_EXPECT(result[jss::engine_result] == "temINVALID");
                    BEAST_EXPECT(result[jss::engine_result_code] == -277);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] == "The transaction is ill-formed.");

                    BEAST_EXPECT(!result.isMember(jss::meta) && !result.isMember(jss::meta_blob));
                };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;
            // master key is disabled, so this is invalid
            tx[jss::SigningPubKey] = strHex(env.master.pk().slice());
            tx[sfSigners] = json::ValueType::Array;
            {
                json::Value signer;
                signer[jss::Account] = becky.human();
                json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with autofill
            testTx(env, tx, testSimulation, false);

            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(env.master);
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();
            tx[sfSigners][0u][sfSigner][jss::SigningPubKey] = strHex(becky.pk().slice());
            tx[sfSigners][0u][sfSigner][jss::TxnSignature] = "";

            // test without autofill
            testTx(env, tx, testSimulation);
        }
    }

    void
    testMultisignedBadPubKey()
    {
        testcase("Multi-signed transaction with a bad public key");

        using namespace jtx;
        Env env(*this);
        static auto const kNewDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const dylan("dylan");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));

        {
            auto validateOutput = [&](json::Value const& resp, json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(
                    result, tx, env.seq(alice), env.current()->fees().base * 2);

                BEAST_EXPECTS(
                    result[jss::engine_result] == "tefBAD_SIGNATURE",
                    result[jss::engine_result].toStyledString());
                BEAST_EXPECT(result[jss::engine_result_code] == -186);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "A signature is provided for a non-signer.");

                BEAST_EXPECT(!result.isMember(jss::meta) && !result.isMember(jss::meta_blob));
            };

            json::Value tx;

            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;
            tx[sfSigners] = json::ValueType::Array;
            {
                json::Value signer;
                signer[jss::Account] = becky.human();
                signer[jss::SigningPubKey] = strHex(dylan.pk().slice());
                json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with autofill
            testTx(env, tx, validateOutput, false);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(alice);
            // transaction requires a non-base fee
            tx[sfFee] = (env.current()->fees().base * 2).jsonClipped().asString();
            tx[sfSigners][0u][sfSigner][jss::TxnSignature] = "";

            // test without autofill
            testTx(env, tx, validateOutput);
        }
    }

    void
    testDeleteExpiredCredentials()
    {
        testcase("Credentials aren't actually deleted on `tecEXPIRED`");

        // scenario setup

        using namespace jtx;
        Env env(*this);

        Account const subject{"subject"};
        Account const issuer{"issuer"};

        env.fund(XRP(10000), subject, issuer);
        env.close();

        auto const credType = "123ABC";

        auto jv = credentials::create(subject, issuer, credType);
        uint32_t const t = env.current()->header().parentCloseTime.time_since_epoch().count();
        jv[sfExpiration.jsonName] = t;
        env(jv);
        env.close();

        {
            auto validateOutput = [&](json::Value const& resp, json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(result, tx, env.seq(subject), env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tecEXPIRED");
                BEAST_EXPECT(result[jss::engine_result_code] == 148);
                BEAST_EXPECT(result[jss::engine_result_message] == "Expiration time is passed.");

                if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                {
                    json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].size() == 5);

                        try
                        {
                            bool found = false;
                            for (auto const& node : metadata[sfAffectedNodes.jsonName])
                            {
                                if (node.isMember(sfDeletedNode.jsonName) &&
                                    node[sfDeletedNode.jsonName][sfLedgerEntryType.jsonName]
                                            .asString() == "Credential")
                                {
                                    auto const deleted =
                                        node[sfDeletedNode.jsonName][sfFinalFields.jsonName];
                                    found = deleted[jss::Issuer] == issuer.human() &&
                                        deleted[jss::Subject] == subject.human() &&
                                        deleted["CredentialType"] ==
                                            strHex(std::string_view(credType));
                                    break;
                                }
                            }
                            BEAST_EXPECT(found);
                        }
                        catch (...)
                        {
                            fail();
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(metadata[sfTransactionResult.jsonName] == "tecEXPIRED");
                }
            };

            json::Value tx = credentials::accept(subject, issuer, credType);

            // test with autofill
            testTx(env, tx, validateOutput);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(subject);
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();

            // test without autofill
            testTx(env, tx, validateOutput);
        }

        // check that expired credentials weren't deleted
        auto const jle = credentials::ledgerEntry(env, subject, issuer, credType);
        BEAST_EXPECT(
            jle.isObject() && jle.isMember(jss::result) && !jle[jss::result].isMember(jss::error) &&
            jle[jss::result].isMember(jss::node) &&
            jle[jss::result][jss::node].isMember("LedgerEntryType") &&
            jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
            jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
            jle[jss::result][jss::node][jss::Subject] == subject.human() &&
            jle[jss::result][jss::node]["CredentialType"] == strHex(std::string_view(credType)));

        BEAST_EXPECT(ownerCount(env, issuer) == 1);
        BEAST_EXPECT(ownerCount(env, subject) == 0);
    }

    void
    testSuccessfulTransactionNetworkID()
    {
        testcase("Successful transaction with a custom network ID");

        using namespace jtx;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->networkId = 1025;
                    return cfg;
                })};
        static auto const kNewDomain = "123ABC";

        {
            auto validateOutput = [&](json::Value const& resp, json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(result, tx, 1, env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                {
                    json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(modifiedNode[sfLedgerEntryType] == "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == kNewDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = kNewDomain;

            // test with autofill
            testTx(env, tx, validateOutput);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = 1;
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();
            tx[sfNetworkID] = 1025;

            // test without autofill
            testTx(env, tx, validateOutput);
        }
    }

    void
    testSuccessfulTransactionAdditionalMetadata()
    {
        testcase("Successful transaction with additional metadata");

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->networkId = 1025;
                    return cfg;
                })};

        Account const alice("alice");
        Account const bob("bob");

        env.fund(XRP(10000), alice, bob);
        env.close();
        // deliver_amount is unavailable in the metadata before 2014-02-01
        // so proceed to 2014-02-01
        env.close(NetClock::time_point{446000000s});

        {
            auto validateOutput = [&](json::Value const& resp,
                                      json::Value const& tx,
                                      json::Value const& expectedMetadataKey,
                                      json::Value const& expectedMetadataValue) {
                auto result = resp[jss::result];

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(result.isMember(jss::meta) || result.isMember(jss::meta_blob)))
                {
                    json::Value const metadata = getJsonMetadata(result);

                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                    BEAST_EXPECT(metadata.isMember(expectedMetadataKey.asString()));
                    BEAST_EXPECT(metadata[expectedMetadataKey.asString()] == expectedMetadataValue);
                }
            };

            {
                json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::Payment;
                tx[sfDestination] = bob.human();
                tx[sfAmount] = "100";

                // test delivered amount
                testTxJsonMetadataField(env, tx, validateOutput, jss::delivered_amount, "100");
            }

            {
                json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::NFTokenMint;
                tx[sfNFTokenTaxon] = 1;

                json::Value const nftokenId = to_string(token::getNextID(env, alice, 1));
                // test nft synthetic
                testTxJsonMetadataField(env, tx, validateOutput, jss::nftoken_id, nftokenId);
            }

            {
                json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::MPTokenIssuanceCreate;

                json::Value const mptIssuanceId = to_string(makeMptID(env.seq(alice), alice));
                // test mpt issuance id
                testTxJsonMetadataField(
                    env, tx, validateOutput, jss::mpt_issuance_id, mptIssuanceId);
            }
        }
    }

public:
    void
    run() override
    {
        testParamErrors();
        testFeeError();
        testInvalidTransactionType();
        testSuccessfulTransaction();
        testTransactionNonTecFailure();
        testTransactionTecFailure();
        testSuccessfulTransactionMultisigned();
        testTransactionSigningFailure();
        testInvalidSingleAndMultiSigningTransaction();
        testMultisignedBadPubKey();
        testDeleteExpiredCredentials();
        testSuccessfulTransactionNetworkID();
        testSuccessfulTransactionAdditionalMetadata();
    }
};

BEAST_DEFINE_TESTSUITE(Simulate, rpc, xrpl);

}  // namespace xrpl::test
