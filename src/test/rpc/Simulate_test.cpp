#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/CTID.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/serialize.h>

#include <optional>
#include <tuple>

namespace ripple {

namespace test {

class Simulate_test : public beast::unit_test::suite
{
    void
    checkBasicReturnValidity(
        Json::Value const& result,
        Json::Value const& tx,
        int const expectedSequence,
        std::string const& expectedFee)
    {
        BEAST_EXPECT(result[jss::applied] == false);
        BEAST_EXPECT(result.isMember(jss::engine_result));
        BEAST_EXPECT(result.isMember(jss::engine_result_code));
        BEAST_EXPECT(result.isMember(jss::engine_result_message));
        BEAST_EXPECT(
            result.isMember(jss::tx_json) || result.isMember(jss::tx_blob));

        Json::Value tx_json;
        if (result.isMember(jss::tx_json))
        {
            tx_json = result[jss::tx_json];
        }
        else
        {
            auto const unHexed = strUnHex(result[jss::tx_blob].asString());
            SerialIter sitTrans(makeSlice(*unHexed));
            tx_json = STObject(std::ref(sitTrans), sfGeneric)
                          .getJson(JsonOptions::none);
        }
        BEAST_EXPECT(tx_json[jss::TransactionType] == tx[jss::TransactionType]);
        BEAST_EXPECT(tx_json[jss::Account] == tx[jss::Account]);
        BEAST_EXPECT(
            tx_json[jss::SigningPubKey] == tx.get(jss::SigningPubKey, ""));
        BEAST_EXPECT(
            tx_json[jss::TxnSignature] == tx.get(jss::TxnSignature, ""));
        BEAST_EXPECT(tx_json[jss::Fee] == tx.get(jss::Fee, expectedFee));
        BEAST_EXPECT(
            tx_json[jss::Sequence] == tx.get(jss::Sequence, expectedSequence));
    }

    void
    checkBasicReturnValidity(
        Json::Value const& result,
        Json::Value const& tx,
        int const expectedSequence,
        XRPAmount const& expectedFee)
    {
        return checkBasicReturnValidity(
            result, tx, expectedSequence, expectedFee.jsonClipped().asString());
    }

    void
    testTx(
        jtx::Env& env,
        Json::Value const& tx,
        std::function<void(Json::Value const&, Json::Value const&)> const&
            validate,
        bool testSerialized = true)
    {
        env.close();

        Json::Value params;
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
            auto const tx_blob =
                strHex(parsed.object->getSerializer().peekData());
            if (BEAST_EXPECT(parsed.object.has_value()))
            {
                Json::Value params;
                params[jss::tx_blob] = tx_blob;
                validate(env.rpc("json", "simulate", to_string(params)), tx);
                params[jss::binary] = true;
                validate(env.rpc("json", "simulate", to_string(params)), tx);
            }
            validate(env.rpc("simulate", tx_blob), tx);
            validate(env.rpc("simulate", tx_blob, "binary"), tx);
        }

        BEAST_EXPECTS(
            env.current()->txCount() == 0,
            std::to_string(env.current()->txCount()));
    }

    void
    testTxJsonMetadataField(
        jtx::Env& env,
        Json::Value const& tx,
        std::function<void(
            Json::Value const&,
            Json::Value const&,
            Json::Value const&,
            Json::Value const&)> const& validate,
        Json::Value const& expectedMetadataKey,
        Json::Value const& expectedMetadataValue)
    {
        env.close();

        Json::Value params;
        params[jss::tx_json] = tx;
        validate(
            env.rpc("json", "simulate", to_string(params)),
            tx,
            expectedMetadataKey,
            expectedMetadataValue);
        validate(
            env.rpc("simulate", to_string(tx)),
            tx,
            expectedMetadataKey,
            expectedMetadataValue);

        BEAST_EXPECTS(
            env.current()->txCount() == 0,
            std::to_string(env.current()->txCount()));
    }

    Json::Value
    getJsonMetadata(Json::Value txResult) const
    {
        if (txResult.isMember(jss::meta_blob))
        {
            auto unHexed = strUnHex(txResult[jss::meta_blob].asString());
            SerialIter sitTrans(makeSlice(*unHexed));
            return STObject(std::ref(sitTrans), sfGeneric)
                .getJson(JsonOptions::none);
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
            Json::Value const params = Json::objectValue;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Neither `tx_blob` nor `tx_json` included.");
        }
        {
            // Providing both `tx_json` and `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = Json::objectValue;
            params[jss::tx_blob] = "1200";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Can only include one of `tx_blob` and `tx_json`.");
        }
        {
            // `binary` isn't a boolean
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "1200";
            params[jss::binary] = "100";
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'binary'.");
        }
        {
            // Invalid `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "12";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Empty `tx_json`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = Json::objectValue;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Missing field 'tx.TransactionType'.");
        }
        {
            // No tx.Account
            Json::Value params = Json::objectValue;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::Payment;
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Missing field 'tx.Account'.");
        }
        {
            // Empty `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Non-string `tx_blob`
            Json::Value params;
            params[jss::tx_blob] = 1.1;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Non-object `tx_json`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = "";

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_json', not object.");
        }
        {
            // `seed` field included
            Json::Value params = Json::objectValue;
            params[jss::seed] = "doesnt_matter";
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'seed'.");
        }
        {
            // `secret` field included
            Json::Value params = Json::objectValue;
            params[jss::secret] = "doesnt_matter";
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'secret'.");
        }
        {
            // `seed_hex` field included
            Json::Value params = Json::objectValue;
            params[jss::seed_hex] = "doesnt_matter";
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'seed_hex'.");
        }
        {
            // `passphrase` field included
            Json::Value params = Json::objectValue;
            params[jss::passphrase] = "doesnt_matter";
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;
            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'passphrase'.");
        }
        {
            // Invalid transaction
            Json::Value params = Json::objectValue;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::Payment;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_exception] ==
                "Field 'Destination' is required but missing.");
        }
        {
            // Bad account
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = "badAccount";
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECTS(
                resp[jss::result][jss::error] == "srcActMalformed",
                resp[jss::result][jss::error].toStyledString());
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx.Account'.");
        }
        {
            // Account doesn't exist for Sequence autofill
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = alice.human();
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Source account not found.");
        }
        {
            // Invalid Signers field
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            tx_json[sfSigners] = "1";
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx.Signers'.");
        }
        {
            // Invalid Signers field
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            tx_json[sfSigners] = Json::arrayValue;
            tx_json[sfSigners].append("1");
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx.Signers[0]'.");
        }
        {
            // Invalid transaction
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            tx_json["foo"] = "bar";
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Field 'tx_json.foo' is unknown.");
        }
        {
            // non-`"binary"` second param for CLI
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = alice.human();
            auto const resp = env.rpc("simulate", to_string(tx_json), "1");
            BEAST_EXPECT(resp[jss::error_message] == "Invalid parameters.");
        }
        {
            // Signed transaction
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            tx_json[jss::TxnSignature] = "1200ABCD";
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Transaction should not be signed.");
        }
        {
            // Signed multisig transaction
            Json::Value params;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = env.master.human();
            tx_json[sfSigners] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = alice.human();
                signer[jss::SigningPubKey] = alice.human();
                signer[jss::TxnSignature] = "1200ABCD";
                Json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx_json[sfSigners].append(signerOuter);
            }
            params[jss::tx_json] = tx_json;

            auto const resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Transaction should not be signed.");
        }
    }

    void
    testFeeError()
    {
        testcase("Fee failure");

        using namespace jtx;

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section("transaction_queue")
                .set("minimum_txn_in_ledger_standalone", "3");
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
            Json::Value params;
            params[jss::tx_json] = noop(alice);

            auto const resp = env.rpc("json", "simulate", to_string(params));
            auto const result = resp[jss::result];
            if (BEAST_EXPECT(result.isMember(jss::error)))
            {
                BEAST_EXPECT(result[jss::error] == "highFee");
                BEAST_EXPECT(result[jss::error_code] == rpcHIGH_FEE);
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
            batch::inner(pay(alice, bob, XRP(10)), seq + 1),
            batch::inner(pay(alice, bob, XRP(10)), seq + 1));

        jt.jv.removeMember(jss::TxnSignature);
        Json::Value params;
        params[jss::tx_json] = jt.jv;
        auto const resp = env.rpc("json", "simulate", to_string(params));
        BEAST_EXPECT(resp[jss::result][jss::error] == "notImpl");
        BEAST_EXPECT(
            resp[jss::result][jss::error_message] == "Not implemented.");
    }

    void
    testSuccessfulTransaction()
    {
        testcase("Successful transaction");

        using namespace jtx;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->NETWORK_ID = 0;
                    return cfg;
                })};
        static auto const newDomain = "123ABC";

        {
            auto validateOutput = [&](Json::Value const& resp,
                                      Json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(
                    result, tx, 1, env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(
                                node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(
                                modifiedNode[sfLedgerEntryType] ==
                                "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == newDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(
                        metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            Json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;

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
            std::function<void(Json::Value const&, Json::Value const&)> const&
                testSimulation = [&](Json::Value const& resp,
                                     Json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(
                        result, tx, 1, env.current()->fees().base);

                    BEAST_EXPECT(result[jss::engine_result] == "temBAD_AMOUNT");
                    BEAST_EXPECT(result[jss::engine_result_code] == -298);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "Malformed: Bad amount.");

                    BEAST_EXPECT(
                        !result.isMember(jss::meta) &&
                        !result.isMember(jss::meta_blob));
                };

            Json::Value tx;

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
            std::function<void(Json::Value const&, Json::Value const&)> const&
                testSimulation = [&](Json::Value const& resp,
                                     Json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(
                        result, tx, 1, env.current()->fees().base);

                    BEAST_EXPECT(
                        result[jss::engine_result] == "tecNO_DST_INSUF_XRP");
                    BEAST_EXPECT(result[jss::engine_result_code] == 125);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "Destination does not exist. Too little XRP sent to "
                        "create "
                        "it.");

                    if (BEAST_EXPECT(
                            result.isMember(jss::meta) ||
                            result.isMember(jss::meta_blob)))
                    {
                        Json::Value const metadata = getJsonMetadata(result);

                        if (BEAST_EXPECT(
                                metadata.isMember(sfAffectedNodes.jsonName)))
                        {
                            BEAST_EXPECT(
                                metadata[sfAffectedNodes.jsonName].size() == 1);
                            auto node = metadata[sfAffectedNodes.jsonName][0u];
                            if (BEAST_EXPECT(
                                    node.isMember(sfModifiedNode.jsonName)))
                            {
                                auto modifiedNode = node[sfModifiedNode];
                                BEAST_EXPECT(
                                    modifiedNode[sfLedgerEntryType] ==
                                    "AccountRoot");
                                auto finalFields = modifiedNode[sfFinalFields];
                                BEAST_EXPECT(
                                    finalFields[sfBalance] ==
                                    std::to_string(
                                        100'000'000'000'000'000 -
                                        env.current()->fees().base.drops()));
                            }
                        }
                        BEAST_EXPECT(
                            metadata[sfTransactionIndex.jsonName] == 0);
                        BEAST_EXPECT(
                            metadata[sfTransactionResult.jsonName] ==
                            "tecNO_DST_INSUF_XRP");
                    }
                };

            Json::Value tx;

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
        static auto const newDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));
        env.close();

        {
            auto validateOutput = [&](Json::Value const& resp,
                                      Json::Value const& tx) {
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

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(
                                node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(
                                modifiedNode[sfLedgerEntryType] ==
                                "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == newDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(
                        metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            Json::Value tx;

            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;

            // test with autofill
            testTx(env, tx, validateOutput, false);

            tx[sfSigners] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = becky.human();
                Json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with just signer accounts
            testTx(env, tx, validateOutput, false);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(alice);
            // transaction requires a non-base fee
            tx[sfFee] =
                (env.current()->fees().base * 2).jsonClipped().asString();
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
        static auto const newDomain = "123ABC";
        Account const alice{"alice"};
        env(regkey(env.master, alice));
        env(fset(env.master, asfDisableMaster), sig(env.master));
        env.close();

        {
            std::function<void(Json::Value const&, Json::Value const&)> const&
                testSimulation =
                    [&](Json::Value const& resp, Json::Value const& tx) {
                        auto result = resp[jss::result];
                        checkBasicReturnValidity(
                            result,
                            tx,
                            env.seq(env.master),
                            env.current()->fees().base);

                        BEAST_EXPECT(
                            result[jss::engine_result] == "tefMASTER_DISABLED");
                        BEAST_EXPECT(result[jss::engine_result_code] == -188);
                        BEAST_EXPECT(
                            result[jss::engine_result_message] ==
                            "Master key is disabled.");

                        BEAST_EXPECT(
                            !result.isMember(jss::meta) &&
                            !result.isMember(jss::meta_blob));
                    };

            Json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;
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
        static auto const newDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));
        env.close();

        {
            std::function<void(Json::Value const&, Json::Value const&)> const&
                testSimulation = [&](Json::Value const& resp,
                                     Json::Value const& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(
                        result,
                        tx,
                        env.seq(env.master),
                        env.current()->fees().base * 2);

                    BEAST_EXPECT(result[jss::engine_result] == "temINVALID");
                    BEAST_EXPECT(result[jss::engine_result_code] == -277);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "The transaction is ill-formed.");

                    BEAST_EXPECT(
                        !result.isMember(jss::meta) &&
                        !result.isMember(jss::meta_blob));
                };

            Json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;
            // master key is disabled, so this is invalid
            tx[jss::SigningPubKey] = strHex(env.master.pk().slice());
            tx[sfSigners] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = becky.human();
                Json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with autofill
            testTx(env, tx, testSimulation, false);

            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(env.master);
            tx[sfFee] = env.current()->fees().base.jsonClipped().asString();
            tx[sfSigners][0u][sfSigner][jss::SigningPubKey] =
                strHex(becky.pk().slice());
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
        static auto const newDomain = "123ABC";
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const dylan("dylan");
        env.fund(XRP(10000), alice);
        env.close();

        // set up valid multisign
        env(signers(alice, 1, {{becky, 1}, {carol, 1}}));

        {
            auto validateOutput = [&](Json::Value const& resp,
                                      Json::Value const& tx) {
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

                BEAST_EXPECT(
                    !result.isMember(jss::meta) &&
                    !result.isMember(jss::meta_blob));
            };

            Json::Value tx;

            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;
            tx[sfSigners] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = becky.human();
                signer[jss::SigningPubKey] = strHex(dylan.pk().slice());
                Json::Value signerOuter;
                signerOuter[sfSigner] = signer;
                tx[sfSigners].append(signerOuter);
            }

            // test with autofill
            testTx(env, tx, validateOutput, false);

            tx[sfSigningPubKey] = "";
            tx[sfTxnSignature] = "";
            tx[sfSequence] = env.seq(alice);
            // transaction requires a non-base fee
            tx[sfFee] =
                (env.current()->fees().base * 2).jsonClipped().asString();
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
        uint32_t const t =
            env.current()->info().parentCloseTime.time_since_epoch().count();
        jv[sfExpiration.jsonName] = t;
        env(jv);
        env.close();

        {
            auto validateOutput = [&](Json::Value const& resp,
                                      Json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(
                    result, tx, env.seq(subject), env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tecEXPIRED");
                BEAST_EXPECT(result[jss::engine_result_code] == 148);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "Expiration time is passed.");

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 5);

                        try
                        {
                            bool found = false;
                            for (auto const& node :
                                 metadata[sfAffectedNodes.jsonName])
                            {
                                if (node.isMember(sfDeletedNode.jsonName) &&
                                    node[sfDeletedNode.jsonName]
                                        [sfLedgerEntryType.jsonName]
                                            .asString() == "Credential")
                                {
                                    auto const deleted =
                                        node[sfDeletedNode.jsonName]
                                            [sfFinalFields.jsonName];
                                    found = deleted[jss::Issuer] ==
                                            issuer.human() &&
                                        deleted[jss::Subject] ==
                                            subject.human() &&
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
                    BEAST_EXPECT(
                        metadata[sfTransactionResult.jsonName] == "tecEXPIRED");
                }
            };

            Json::Value tx = credentials::accept(subject, issuer, credType);

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
        auto const jle =
            credentials::ledgerEntry(env, subject, issuer, credType);
        BEAST_EXPECT(
            jle.isObject() && jle.isMember(jss::result) &&
            !jle[jss::result].isMember(jss::error) &&
            jle[jss::result].isMember(jss::node) &&
            jle[jss::result][jss::node].isMember("LedgerEntryType") &&
            jle[jss::result][jss::node]["LedgerEntryType"] == jss::Credential &&
            jle[jss::result][jss::node][jss::Issuer] == issuer.human() &&
            jle[jss::result][jss::node][jss::Subject] == subject.human() &&
            jle[jss::result][jss::node]["CredentialType"] ==
                strHex(std::string_view(credType)));

        BEAST_EXPECT(ownerCount(env, issuer) == 1);
        BEAST_EXPECT(ownerCount(env, subject) == 0);
    }

    void
    testSuccessfulTransactionNetworkID()
    {
        testcase("Successful transaction with a custom network ID");

        using namespace jtx;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->NETWORK_ID = 1025;
                    return cfg;
                })};
        static auto const newDomain = "123ABC";

        {
            auto validateOutput = [&](Json::Value const& resp,
                                      Json::Value const& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(
                    result, tx, 1, env.current()->fees().base);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value const metadata = getJsonMetadata(result);

                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(
                                node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode];
                            BEAST_EXPECT(
                                modifiedNode[sfLedgerEntryType] ==
                                "AccountRoot");
                            auto finalFields = modifiedNode[sfFinalFields];
                            BEAST_EXPECT(finalFields[sfDomain] == newDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                    BEAST_EXPECT(
                        metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            Json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain] = newDomain;

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
                    cfg->NETWORK_ID = 1025;
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
            auto validateOutput =
                [&](Json::Value const& resp,
                    Json::Value const& tx,
                    Json::Value const& expectedMetadataKey,
                    Json::Value const& expectedMetadataValue) {
                    auto result = resp[jss::result];

                    BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                    BEAST_EXPECT(result[jss::engine_result_code] == 0);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "The simulated transaction would have been applied.");

                    if (BEAST_EXPECT(
                            result.isMember(jss::meta) ||
                            result.isMember(jss::meta_blob)))
                    {
                        Json::Value const metadata = getJsonMetadata(result);

                        BEAST_EXPECT(
                            metadata[sfTransactionIndex.jsonName] == 0);
                        BEAST_EXPECT(
                            metadata[sfTransactionResult.jsonName] ==
                            "tesSUCCESS");
                        BEAST_EXPECT(
                            metadata.isMember(expectedMetadataKey.asString()));
                        BEAST_EXPECT(
                            metadata[expectedMetadataKey.asString()] ==
                            expectedMetadataValue);
                    }
                };

            {
                Json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::Payment;
                tx[sfDestination] = bob.human();
                tx[sfAmount] = "100";

                // test delivered amount
                testTxJsonMetadataField(
                    env, tx, validateOutput, jss::delivered_amount, "100");
            }

            {
                Json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::NFTokenMint;
                tx[sfNFTokenTaxon] = 1;

                Json::Value nftokenId =
                    to_string(token::getNextID(env, alice, 1));
                // test nft synthetic
                testTxJsonMetadataField(
                    env, tx, validateOutput, jss::nftoken_id, nftokenId);
            }

            {
                Json::Value tx;
                tx[jss::Account] = alice.human();
                tx[jss::TransactionType] = jss::MPTokenIssuanceCreate;

                Json::Value mptIssuanceId =
                    to_string(makeMptID(env.seq(alice), alice));
                // test mpt issuance id
                testTxJsonMetadataField(
                    env,
                    tx,
                    validateOutput,
                    jss::mpt_issuance_id,
                    mptIssuanceId);
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

BEAST_DEFINE_TESTSUITE(Simulate, rpc, ripple);

}  // namespace test

}  // namespace ripple
