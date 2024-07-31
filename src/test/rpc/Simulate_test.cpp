//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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
        Json::Value& result,
        Json::Value& tx,
        int sequence = 1,
        std::string fee = "10")
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
            auto unHexed = strUnHex(result[jss::tx_blob].asString());
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
        BEAST_EXPECT(tx_json[jss::Fee] == tx.get(jss::Fee, fee));
        BEAST_EXPECT(tx_json[jss::Sequence] == tx.get(jss::Sequence, sequence));
    }

    void
    testTx(
        jtx::Env& env,
        Json::Value& tx,
        std::function<void(Json::Value, Json::Value&)> validate,
        bool testSerialized = true)
    {
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
            STParsedJSONObject parsed(std::string(jss::tx_json), tx);
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
            Json::Value params = Json::objectValue;
            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Invalid parameters.");
        }
        {
            // Providing both `tx_json` and `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = Json::objectValue;
            params[jss::tx_blob] = "1200";

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Invalid parameters.");
        }
        {
            // `binary` isn't a boolean
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "1200";
            params[jss::binary] = "100";
            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'binary'.");
        }
        {
            // Invalid `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "12";

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Empty `tx_json`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = Json::objectValue;

            auto resp = env.rpc("json", "simulate", to_string(params));
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

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Missing field 'tx.Account'.");
        }
        {
            // Empty `tx_blob`
            Json::Value params = Json::objectValue;
            params[jss::tx_blob] = "";

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Non-string `tx_blob`
            Json::Value params;
            params[jss::tx_blob] = 1.1;

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_blob'.");
        }
        {
            // Non-object `tx_json`
            Json::Value params = Json::objectValue;
            params[jss::tx_json] = "";

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'tx_json', not object.");
        }
        {
            // Invalid transaction
            Json::Value params = Json::objectValue;
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::Payment;
            tx_json[jss::Account] = env.master.human();
            params[jss::tx_json] = tx_json;

            auto resp = env.rpc("json", "simulate", to_string(params));
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

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::error] == "srcActMalformed");
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

            auto resp = env.rpc("json", "simulate", to_string(params));
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
            tx_json[sfSigners.jsonName] = "1";
            params[jss::tx_json] = tx_json;

            auto resp = env.rpc("json", "simulate", to_string(params));
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
            tx_json[sfSigners.jsonName] = Json::arrayValue;
            tx_json[sfSigners.jsonName].append("1");
            params[jss::tx_json] = tx_json;

            auto resp = env.rpc("json", "simulate", to_string(params));
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

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Field 'tx_json.foo' is unknown.");
        }
        {
            // non-`"binary"` second param for CLI
            Json::Value tx_json = Json::objectValue;
            tx_json[jss::TransactionType] = jss::AccountSet;
            tx_json[jss::Account] = alice.human();
            auto resp = env.rpc("simulate", to_string(tx_json), "1");
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

            auto resp = env.rpc("json", "simulate", to_string(params));
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
            tx_json[sfSigners.jsonName] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = alice.human();
                signer[jss::SigningPubKey] = alice.human();
                signer[jss::TxnSignature] = "1200ABCD";
                Json::Value signerOuter;
                signerOuter[sfSigner.jsonName] = signer;
                tx_json[sfSigners.jsonName].append(signerOuter);
            }
            params[jss::tx_json] = tx_json;

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Transaction should not be signed.");
        }
    }
    void
    testSuccessfulTransaction()
    {
        testcase("Successful transaction");

        using namespace jtx;
        Env env(*this);
        static auto const newDomain = "123ABC";

        {
            auto validateOutput = [&](Json::Value resp, Json::Value& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(result, tx);

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value metadata;
                    if (result.isMember(jss::meta_blob))
                    {
                        auto unHexed =
                            strUnHex(result[jss::meta_blob].asString());
                        SerialIter sitTrans(makeSlice(*unHexed));
                        metadata = STObject(std::ref(sitTrans), sfGeneric)
                                       .getJson(JsonOptions::none);
                    }
                    else
                    {
                        metadata = result[jss::meta];
                    }
                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(
                                node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode.jsonName];
                            BEAST_EXPECT(
                                modifiedNode[sfLedgerEntryType.jsonName] ==
                                "AccountRoot");
                            auto finalFields =
                                modifiedNode[sfFinalFields.jsonName];
                            BEAST_EXPECT(
                                finalFields[sfDomain.jsonName] == newDomain);
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
            tx[sfDomain.jsonName] = newDomain;

            // test with autofill
            testTx(env, tx, validateOutput);

            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 1;
            tx[sfFee.jsonName] = "12";

            // test without autofill
            testTx(env, tx, validateOutput);

            // TODO: check that the ledger wasn't affected
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
            std::function<void(Json::Value, Json::Value&)> testSimulation =
                [&](Json::Value resp, Json::Value& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(result, tx);

                    BEAST_EXPECT(result[jss::engine_result] == "temBAD_AMOUNT");
                    BEAST_EXPECT(result[jss::engine_result_code] == -298);
                    BEAST_EXPECT(
                        result[jss::engine_result_message] ==
                        "Can only send positive amounts.");

                    BEAST_EXPECT(
                        !result.isMember(jss::meta) &&
                        !result.isMember(jss::meta_blob));
                };

            Json::Value tx;

            tx[jss::Account] = env.master.human();
            tx[jss::TransactionType] = jss::Payment;
            tx[sfDestination.jsonName] = alice.human();
            tx[sfAmount.jsonName] = "0";  // invalid amount

            // test with autofill
            testTx(env, tx, testSimulation);

            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 1;
            tx[sfFee.jsonName] = "12";

            // test without autofill
            testTx(env, tx, testSimulation);

            // TODO: check that the ledger wasn't affected
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
            std::function<void(Json::Value, Json::Value&)> testSimulation =
                [&](Json::Value resp, Json::Value& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(result, tx);

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
                        Json::Value metadata;
                        if (result.isMember(jss::meta_blob))
                        {
                            auto unHexed =
                                strUnHex(result[jss::meta_blob].asString());
                            SerialIter sitTrans(makeSlice(*unHexed));
                            metadata = STObject(std::ref(sitTrans), sfGeneric)
                                           .getJson(JsonOptions::none);
                        }
                        else
                        {
                            metadata = result[jss::meta];
                        }
                        if (BEAST_EXPECT(
                                metadata.isMember(sfAffectedNodes.jsonName)))
                        {
                            BEAST_EXPECT(
                                metadata[sfAffectedNodes.jsonName].size() == 1);
                            auto node = metadata[sfAffectedNodes.jsonName][0u];
                            if (BEAST_EXPECT(
                                    node.isMember(sfModifiedNode.jsonName)))
                            {
                                auto modifiedNode =
                                    node[sfModifiedNode.jsonName];
                                BEAST_EXPECT(
                                    modifiedNode[sfLedgerEntryType.jsonName] ==
                                    "AccountRoot");
                                auto finalFields =
                                    modifiedNode[sfFinalFields.jsonName];
                                BEAST_EXPECT(
                                    finalFields[sfBalance.jsonName] ==
                                    "99999999999999990");
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
            tx[sfDestination.jsonName] = alice.human();
            tx[sfAmount.jsonName] = "1";  // not enough to create an account

            // test with autofill
            testTx(env, tx, testSimulation);

            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 1;
            tx[sfFee.jsonName] = "10";

            // test without autofill
            testTx(env, tx, testSimulation);

            // TODO: check that the ledger wasn't affected
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

        {
            auto validateOutput = [&](Json::Value resp, Json::Value& tx) {
                auto result = resp[jss::result];
                checkBasicReturnValidity(result, tx, 5, "20");

                BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
                BEAST_EXPECT(result[jss::engine_result_code] == 0);
                BEAST_EXPECT(
                    result[jss::engine_result_message] ==
                    "The simulated transaction would have been applied.");

                if (BEAST_EXPECT(
                        result.isMember(jss::meta) ||
                        result.isMember(jss::meta_blob)))
                {
                    Json::Value metadata;
                    if (result.isMember(jss::meta_blob))
                    {
                        auto unHexed =
                            strUnHex(result[jss::meta_blob].asString());
                        SerialIter sitTrans(makeSlice(*unHexed));
                        metadata = STObject(std::ref(sitTrans), sfGeneric)
                                       .getJson(JsonOptions::none);
                    }
                    else
                    {
                        metadata = result[jss::meta];
                    }
                    if (BEAST_EXPECT(
                            metadata.isMember(sfAffectedNodes.jsonName)))
                    {
                        BEAST_EXPECT(
                            metadata[sfAffectedNodes.jsonName].size() == 1);
                        auto node = metadata[sfAffectedNodes.jsonName][0u];
                        if (BEAST_EXPECT(
                                node.isMember(sfModifiedNode.jsonName)))
                        {
                            auto modifiedNode = node[sfModifiedNode.jsonName];
                            BEAST_EXPECT(
                                modifiedNode[sfLedgerEntryType.jsonName] ==
                                "AccountRoot");
                            auto finalFields =
                                modifiedNode[sfFinalFields.jsonName];
                            BEAST_EXPECT(
                                finalFields[sfDomain.jsonName] == newDomain);
                        }
                    }
                    BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 1);
                    BEAST_EXPECT(
                        metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
                }
            };

            Json::Value tx;

            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain.jsonName] = newDomain;
            tx[sfSigners.jsonName] = Json::arrayValue;
            {
                Json::Value signer;
                signer[jss::Account] = becky.human();
                signer[jss::SigningPubKey] = strHex(becky.pk().slice());
                Json::Value signerOuter;
                signerOuter[sfSigner.jsonName] = signer;
                tx[sfSigners.jsonName].append(signerOuter);
            }

            // test with autofill
            testTx(env, tx, validateOutput, false);

            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 5;
            tx[sfFee.jsonName] = "20";  // also tests a non-base fee
            tx[sfSigners.jsonName][0u][sfSigner.jsonName][jss::TxnSignature] =
                "";

            // test without autofill
            testTx(env, tx, validateOutput);

            // TODO: check that the ledger wasn't affected
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
            std::function<void(Json::Value, Json::Value&)> testSimulation =
                [&](Json::Value resp, Json::Value& tx) {
                    auto result = resp[jss::result];
                    checkBasicReturnValidity(result, tx, 3);

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
            tx[sfDomain.jsonName] = newDomain;

            // test with autofill
            testTx(env, tx, testSimulation, 3);

            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 3;
            tx[sfFee.jsonName] = "12";

            // test without autofill
            testTx(env, tx, testSimulation, 3);

            // TODO: check that the ledger wasn't affected
        }
    }

public:
    void
    run() override
    {
        testParamErrors();
        testSuccessfulTransaction();
        testTransactionNonTecFailure();
        testTransactionTecFailure();
        testSuccessfulTransactionMultisigned();
        testTransactionSigningFailure();
    }
};

BEAST_DEFINE_TESTSUITE(Simulate, rpc, ripple);

}  // namespace test

}  // namespace ripple
