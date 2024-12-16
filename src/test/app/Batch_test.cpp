//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <test/jtx/utility.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class Batch_test : public beast::unit_test::suite
{
    struct TestBatchData
    {
        std::string result;
        std::string txHash;
    };

    struct TestSignData
    {
        int index;
        jtx::Account account;
    };

    Json::Value
    getTxByIndex(Json::Value jrr, std::uint8_t index)
    {
        for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
        {
            if (txn[jss::metaData][sfTransactionIndex.jsonName] == index)
                return txn;
        }
        return {};
    }

    void
    validateBatchTxns(
        Json::Value meta,
        std::uint32_t const& txns,
        std::vector<TestBatchData> const& batchResults)
    {
        // TODO: DA
    }

    void
    validateBatchPreMeta(
        Json::Value const& meta,
        STAmount const& balance,
        std::uint32_t const& sequence,
        std::optional<std::uint32_t> ownerCount = std::nullopt,
        std::optional<std::uint32_t> ticketCount = std::nullopt)
    {
        for (Json::Value const& node : meta[sfAffectedNodes.jsonName])
        {
            if (node.isMember(sfModifiedNode.jsonName))
            {
                Json::Value const& modified = node[sfModifiedNode.jsonName];
                std::string const entryType =
                    modified[sfLedgerEntryType.jsonName].asString();
                if (entryType == jss::AccountRoot)
                {
                    auto const& previousFields =
                        modified[sfPreviousFields.jsonName];
                    std::uint32_t const prevSeq =
                        previousFields[sfSequence.jsonName].asUInt();
                    BEAST_EXPECT(prevSeq == sequence);
                    if (ownerCount.has_value())
                        BEAST_EXPECT(
                            previousFields[sfOwnerCount.jsonName].asUInt() ==
                            *ownerCount);
                    if (ticketCount.has_value())
                        BEAST_EXPECT(
                            previousFields[sfTicketCount.jsonName].asUInt() ==
                            *ticketCount);
                }
            }
        }
    }

    Json::Value
    addBatchTx(
        Json::Value jv,
        Json::Value const& tx,
        std::uint32_t sequence,
        std::optional<std::uint32_t> ticket = std::nullopt)
    {
        std::uint32_t const index = jv[jss::RawTransactions].size();
        Json::Value& batchTransaction = jv[jss::RawTransactions][index];

        // Initialize the batch transaction
        batchTransaction = Json::Value{};
        batchTransaction[jss::RawTransaction] = tx;
        batchTransaction[jss::RawTransaction][jss::SigningPubKey] = "";
        batchTransaction[jss::RawTransaction][sfFee.jsonName] = 0;
        batchTransaction[jss::RawTransaction][jss::Sequence] = sequence;
        batchTransaction[jss::RawTransaction][jss::Flags] = tfInnerBatchTxn;

        // Optionally set ticket sequence
        if (ticket.has_value())
        {
            batchTransaction[jss::RawTransaction][jss::Sequence] = 0;
            batchTransaction[jss::RawTransaction][sfTicketSequence.jsonName] =
                *ticket;
        }

        return jv;
    }

    Json::Value
    addBatchSignatures(Json::Value jv, std::vector<TestSignData> const& signers)
    {
        auto const ojv = jv;
        for (auto const& signer : signers)
        {
            Serializer ss{
                buildMultiSigningData(jtx::parse(ojv), signer.account.id())};
            auto const sig = ripple::sign(
                signer.account.pk(), signer.account.sk(), ss.slice());
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfAccount.jsonName] = signer.account.human();
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfSigningPubKey.jsonName] = strHex(signer.account.pk());
            jv[sfBatchSigners.jsonName][signer.index][sfBatchSigner.jsonName]
              [sfTxnSignature.jsonName] = strHex(Slice{sig.data(), sig.size()});
        }
        return jv;
    }

    void
    testEnable(FeatureBitset features)
    {
        testcase("enabled");

        using namespace test::jtx;
        using namespace std::literals;

        for (bool const withBatch : {true, false})
        {
            auto const amend = withBatch ? features : features - featureBatch;
            test::jtx::Env env{*this, envconfig(), amend};
            auto const feeDrops = env.current()->fees().base;

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 1;

            auto const txResult =
                withBatch ? ter(tesSUCCESS) : ter(temDISABLED);

            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                txResult);
            env.close();
        }
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("preflight");

        using namespace test::jtx;
        using namespace std::literals;

        //----------------------------------------------------------------------
        // preflight

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // temINVALID_FLAG: Batch: invalid flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                txflags(tfDisallowXRP),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temMALFORMED: Batch: too many flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                txflags(tfAllOrNothing | tfOnlyOne),
                ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: Batch: hashes array size does not match txns.
        {
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            Json::Value jv =
                batch::batch(alice, env.seq(alice), batchFee, tfAllOrNothing);

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, env.seq(alice) + 1);
            auto txn1 = jv[jss::RawTransactions][0u][jss::RawTransaction];
            STParsedJSONObject parsed1(std::string(jss::tx_json), txn1);
            STTx const stx1 = STTx{std::move(parsed1.object.value())};
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, env.seq(bob));
            auto txn2 = jv[jss::RawTransactions][1u][jss::RawTransaction];
            STParsedJSONObject parsed2(std::string(jss::tx_json), txn2);
            STTx const stx2 = STTx{std::move(parsed2.object.value())};
            jv[sfTransactionIDs.jsonName].append(to_string(stx2.getTransactionID()));

            // Add another txn hash to the TxIDs array
            jv[sfTransactionIDs.jsonName].append(to_string(stx2.getTransactionID()));

            env(jv, batch::sig(bob), ter(temMALFORMED));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array empty.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                ter(temARRAY_EMPTY));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                batch::add(pay(alice, bob, XRP(1)), seq + 3),
                batch::add(pay(alice, bob, XRP(1)), seq + 4),
                batch::add(pay(alice, bob, XRP(1)), seq + 5),
                batch::add(pay(alice, bob, XRP(1)), seq + 6),
                batch::add(pay(alice, bob, XRP(1)), seq + 7),
                batch::add(pay(alice, bob, XRP(1)), seq + 8),
                batch::add(pay(alice, bob, XRP(1)), seq + 9),
                ter(temARRAY_TOO_LARGE));
            env.close();
        }

        // temINVALID_BATCH: Batch: Duplicate signer found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                batch::sig(bob, bob),
                ter(temINVALID_BATCH));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: signers array exceeds 8 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((9 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                batch::sig(
                    bob,
                    carol,
                    alice,
                    bob,
                    carol,
                    alice,
                    bob,
                    carol,
                    alice,
                    alice),
                ter(temARRAY_TOO_LARGE));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            std::vector<TestSignData> const signers = {{
                {0, bob},
            }};

            auto const batchFee =
                ((signers.size() + 2) * feeDrops) + feeDrops * 2;
            Json::Value jv =
                batch::batch(alice, env.seq(alice), batchFee, tfAllOrNothing);

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, env.seq(alice) + 1);
            auto txn1 = jv[jss::RawTransactions][0u][jss::RawTransaction];
            STParsedJSONObject parsed1(std::string(jss::tx_json), txn1);
            STTx const stx1 = STTx{std::move(parsed1.object.value())};
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, env.seq(bob));
            auto txn2 = jv[jss::RawTransactions][1u][jss::RawTransaction];
            STParsedJSONObject parsed2(std::string(jss::tx_json), txn2);
            STTx const stx2 = STTx{std::move(parsed2.object.value())};
            jv[sfTransactionIDs.jsonName].append(to_string(stx2.getTransactionID()));

            for (auto const& signer : signers)
            {
                Serializer msg;
                serializeBatch(
                    msg,
                    tfAllOrNothing,
                    STVector256(
                        {stx1.getTransactionID(), stx2.getTransactionID()}));
                auto const sig = ripple::sign(
                    signer.account.pk(), signer.account.sk(), msg.slice());
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfAccount.jsonName] =
                      signer.account.human();
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfSigningPubKey.jsonName] =
                      strHex(alice.pk());
                jv[sfBatchSigners.jsonName][signer.index]
                  [sfBatchSigner.jsonName][sfTxnSignature.jsonName] =
                      strHex(Slice{sig.data(), sig.size()});
            }

            jv = addBatchSignatures(jv, signers);

            env(jv, ter(temBAD_SIGNATURE));
            env.close();
        }

        // temMALFORMED: Batch: duplicate TxID found.
        {
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            Json::Value jv =
                batch::batch(alice, env.seq(alice), batchFee, tfAllOrNothing);

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, env.seq(alice) + 1);
            auto txn1 = jv[jss::RawTransactions][0u][jss::RawTransaction];
            STParsedJSONObject parsed1(std::string(jss::tx_json), txn1);
            STTx const stx1 = STTx{std::move(parsed1.object.value())};

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, env.seq(bob));

            // Add a duplicate hash
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));

            env(jv, batch::sig(bob), ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: Batch: order of inner transactions does not match
        // TxIDs.
        {
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            Json::Value jv =
                batch::batch(alice, env.seq(alice), batchFee, tfAllOrNothing);

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, env.seq(alice) + 1);
            auto txn1 = jv[jss::RawTransactions][0u][jss::RawTransaction];
            STParsedJSONObject parsed1(std::string(jss::tx_json), txn1);
            STTx const stx1 = STTx{std::move(parsed1.object.value())};

            // Tx 2
            Json::Value const tx2 = pay(bob, alice, XRP(5));
            jv = addBatchTx(jv, tx2, env.seq(bob));
            auto txn2 = jv[jss::RawTransactions][1u][jss::RawTransaction];
            STParsedJSONObject parsed2(std::string(jss::tx_json), txn2);
            STTx const stx2 = STTx{std::move(parsed2.object.value())};

            // Add the hashes out of order
            jv[sfTransactionIDs.jsonName].append(to_string(stx2.getTransactionID()));
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));

            env(jv, batch::sig(bob), ter(temMALFORMED));
            env.close();
        }

        // temINVALID_BATCH: Batch: TransactionType missing in array entry.
        {
            auto const txBlob =
                "12003D2200010000240000000468400000000000003273210388935426E0D0"
                "8083314842EDFBB2D517BD47699F9A4527318A8E10468C97C0527446304402"
                "20280E69E1CD973C909586B3EBF41556F50694F8FE5E905BF6C9E9B6F97417"
                "A1D40220509BE54BF5CE3B5D7A989D12F302C486657883629CAF34EC648361"
                "6237AFA9C88114AE123A8556F3CF91154711376AFB0F894F832B3DF01DE022"
                "22800000002400000005614000000000989680684000000000000000730081"
                "14AE123A8556F3CF91154711376AFB0F894F832B3D8314F51DFC2A09D62CBB"
                "A1DFBDD4691DAC96AD98B90FE1F1061320B767AB126F2655B1848233CE8952"
                "7A1503C7B03A75D8C9DE547FEDB408CA26A1";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction");

            env.close();
        }

        // temINVALID_BATCH: Batch: batch cannot have inner batch txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(
                    batch::batch(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_BATCH));
            env.close();
        }

        // temINVALID_BATCH: Batch: batch cannot have inner account delete txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(acctdelete(alice, bob), seq + 1),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_BATCH));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(carol),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: outer signature for inner txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(alice, bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: unique signers does not match batch signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(bob, carol),
                ter(temBAD_SIGNER));
            env.close();
        }
    }

    void
    testNonTecInner(FeatureBitset features)
    {
        testcase("non tec in inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // temBAD_FEE(tecINTERNAL): Batch: sfFee must be zero.
        {
            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            Json::Value jv =
                batch::batch(alice, env.seq(alice), batchFee, tfAllOrNothing);

            // Tx 1
            Json::Value tx1 = pay(alice, bob, XRP(10));
            jv = addBatchTx(jv, tx1, env.seq(alice) + 1);
            jv[jss::RawTransactions][0u][jss::RawTransaction][sfFee.jsonName] =
                to_string(feeDrops);
            auto txn1 = jv[jss::RawTransactions][0u][jss::RawTransaction];
            STParsedJSONObject parsed1(std::string(jss::tx_json), txn1);
            STTx const stx1 = STTx{std::move(parsed1.object.value())};
            jv[sfTransactionIDs.jsonName].append(to_string(stx1.getTransactionID()));

            env(jv, ter(tecINTERNAL));
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            std::cout << "jrr: " << jrr << "\n";
        }
    }

    void
    testBadSequence(FeatureBitset features)
    {
        testcase("bad sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        // Invalid: Bob Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::add(pay(bob, alice, XRP(5)), preBobSeq + 10),
                batch::sig(bob),
                ter(tecINTERNAL));
            auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"terPRE_SEQ", to_string(txIDs[1])},
            };
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            auto const txn = getTxByIndex(jrr, 0);
            validateBatchTxns(txn[jss::metaData], 2, testCases);
            validateBatchPreMeta(txn[jss::metaData], preAlice, preAliceSeq);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Outer and Inner Sequence are the same
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), preAliceSeq),
                batch::add(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob),
                ter(tecINTERNAL));
            auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
            std::vector<TestBatchData> testCases = {
                {"tefPAST_SEQ", to_string(txIDs[0])},
            };
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            auto const txn = getTxByIndex(jrr, 0);
            validateBatchTxns(txn[jss::metaData], 1, testCases);
            validateBatchPreMeta(txn[jss::metaData], preAlice, preAliceSeq);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }
    }

    void
    testBadFeeNoSigner(FeatureBitset features)
    {
        testcase("bad fee no signer");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const preAliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobSeq = env.seq(bob);
        auto const preBob = env.balance(bob);
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = feeDrops * 2;
        env(batch::batch(alice, preAliceSeq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(10)), preAliceSeq + 1),
            batch::add(pay(bob, alice, XRP(5)), preBobSeq),
            batch::sig(bob),
            ter(telINSUF_FEE_P));
        env.close();

        // Alice & Bob should not be affected.
        BEAST_EXPECT(env.seq(alice) == preAliceSeq);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
        BEAST_EXPECT(env.seq(bob) == preBobSeq);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
    }

    void
    testBadFeeSigner(FeatureBitset features)
    {
        testcase("bad fee signer");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const preAliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobSeq = env.seq(bob);
        auto const preBob = env.balance(bob);
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, preAliceSeq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(10)), preAliceSeq + 1),
            batch::add(pay(bob, alice, XRP(5)), preBobSeq),
            batch::sig(bob),
            ter(telINSUF_FEE_P));
        env.close();

        // Alice & Bob should not be affected.
        BEAST_EXPECT(env.seq(alice) == preAliceSeq);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
        BEAST_EXPECT(env.seq(bob) == preBobSeq);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
    }

    void
    testChangesBetweenViews(FeatureBitset features)
    {
        testcase("changes between views");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(220), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = XRP(1);
        auto const seq = env.seq(alice);
        // env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
        //     batch::add(pay(alice, bob, XRP(10)), seq + 1),
        //     batch::add(pay(alice, bob, XRP(10)), seq + 2),
        //     ter(tecBATCH_FAILURE));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tecUNFUNDED_PAYMENT", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 5);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
    }

    void
    testAllOrNothing(FeatureBitset features)
    {
        testcase("all or nothing");

        using namespace test::jtx;
        using namespace std::literals;

        // all
        {
            test::jtx::Env env{*this, envconfig()};

            auto const feeDrops = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
            auto const seq = env.seq(alice);
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                batch::add(pay(alice, bob, XRP(1)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            auto const txn = getTxByIndex(jrr, 0);
            validateBatchTxns(txn[jss::metaData], 2, testCases);
            validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

            BEAST_EXPECT(env.seq(alice) == 7);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
        }

        // nothing
        {
            test::jtx::Env env{*this, envconfig()};

            auto const feeDrops = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
            auto const seq = env.seq(alice);
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(1)), seq + 1),
                batch::add(pay(alice, bob, XRP(999)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[1])},
            };
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            auto const txn = getTxByIndex(jrr, 0);
            validateBatchTxns(txn[jss::metaData], 2, testCases);
            validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

            BEAST_EXPECT(env.seq(alice) == 5);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }
    }

    void
    testOnlyOne(FeatureBitset features)
    {
        testcase("only one");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 3;
        auto const seq = env.seq(alice);
        env(batch::batch(alice, seq, batchFee, tfOnlyOne),
            batch::add(pay(alice, bob, XRP(999)), seq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 2),
            batch::add(pay(alice, bob, XRP(1)), seq + 3),
            ter(tesSUCCESS));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tecUNFUNDED_PAYMENT", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testUntilFailure(FeatureBitset features)
    {
        testcase("until failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 4;
        auto const seq = env.seq(alice);
        env(batch::batch(alice, seq, batchFee, tfUntilFailure),
            batch::add(pay(alice, bob, XRP(1)), seq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 2),
            batch::add(pay(alice, bob, XRP(999)), seq + 3),
            batch::add(pay(alice, bob, XRP(1)), seq + 4),
            ter(tesSUCCESS));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
            {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 3, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 8);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testIndependent(FeatureBitset features)
    {
        testcase("independent");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 4;
        auto const seq = env.seq(alice);
        env(batch::batch(alice, seq, batchFee, tfIndependent),
            batch::add(pay(alice, bob, XRP(1)), seq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 2),
            batch::add(pay(alice, bob, XRP(999)), seq + 3),
            batch::add(pay(alice, bob, XRP(1)), seq + 4),
            ter(tesSUCCESS));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
            {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
            {"tesSUCCESS", to_string(txIDs[3])},
        };
        env.close();
        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 4, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 9);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testMultiParty(FeatureBitset features)
    {
        testcase("multi party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(10)), seq + 1),
            batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
            batch::sig(bob));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(5));
    }

    void
    testMultisign(FeatureBitset features)
    {
        testcase("multisign");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 6;
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(1)), seq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 2),
            msig(bob, carol));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 8);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testMultisignMultiParty(FeatureBitset features)
    {
        testcase("multisign multi party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const elsa = Account("elsa");

        env.fund(XRP(1000), alice, bob, carol, dave, elsa);
        env.close();

        env(signers(bob, 2, {{carol, 1}, {dave, 1}, {elsa, 1}}));
        env.close();

        // tefBAD_QUORUM
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {dave}),
                ter(tefBAD_QUORUM));
            env.close();
        }

        // tefBAD_SIGNATURE
        {
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {alice, dave}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }

        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(10)), seq + 1),
                batch::add(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {dave, carol}));
            auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();

            Json::Value params;
            params[jss::ledger_index] = env.current()->seq() - 1;
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            auto const txn = getTxByIndex(jrr, 0);
            validateBatchTxns(txn[jss::metaData], 2, testCases);
            validateBatchPreMeta(txn[jss::metaData], STAmount(XRP(1000)), 4);

            BEAST_EXPECT(env.seq(alice) == 6);
            BEAST_EXPECT(env.seq(bob) == 6);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(5));
        }
    }

    void
    testBatchType(FeatureBitset features)
    {
        testcase("batch type");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const eve = Account("eve");
        env.fund(XRP(100000), alice, bob, carol, eve);
        env.close();

        // auto dumpCL = [this,&env]() {
        //     log << "Full Ledger:\n" << env.rpc("ledger", "closed", "tx")[jss::result].toStyledString() << "\n";
        // };

        { // All or Nothing: all succeed
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(100)), seq + 1),
                batch::add(pay(alice, carol, XRP(100)), seq + 2));
            env.close();

            // dumpCL();

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        { // All or Nothing: one fails
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
                batch::add(pay(alice, bob, XRP(100)), seq + 1),
                batch::add(pay(alice, carol, XRP(747681)), seq + 2));
            env.close();

            // dumpCL();

            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }

        { // Independent (one fails)
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfIndependent),
                batch::add(pay(alice, bob, XRP(100)), seq + 1),
                batch::add(pay(alice, carol, XRP(100)), seq + 2),
                batch::add(offer(alice, alice["USD"](100), XRP(100),tfImmediateOrCancel), seq + 3));
            env.close();

            // dumpCL();

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        { // Until Failure: one fails, one is not executed
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfUntilFailure),
                batch::add(pay(alice, bob, XRP(100)), seq + 1),
                batch::add(pay(alice, carol, XRP(100)), seq + 2),
                batch::add(offer(alice, alice["USD"](100), XRP(100),tfImmediateOrCancel), seq + 3),
                batch::add(pay(alice, eve, XRP(100)), seq + 4));
            env.close();

            // dumpCL();

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }

        { // Only one: the fourth succeeds
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = (8 * feeDrops) + feeDrops * 2;
            env(batch::batch(alice, seq, batchFee, tfOnlyOne),
                batch::add(offer(alice, alice["USD"](100), XRP(100),tfImmediateOrCancel), seq + 1),
                batch::add(offer(alice, alice["USD"](100), XRP(100),tfImmediateOrCancel), seq + 2),
                batch::add(offer(alice, alice["USD"](100), XRP(100),tfImmediateOrCancel), seq + 3),
                batch::add(pay(alice, bob, XRP(100)), seq + 4),
                batch::add(pay(alice, carol, XRP(100)), seq + 5),
                batch::add(pay(alice, eve, XRP(100)), seq + 6));
            env.close();

            // dumpCL();

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(100) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }
    }

    void
    testSubmit(FeatureBitset features)
    {
        testcase("submit");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        // Invalid: txn has `tfInnerBatchTxn` flag and signature
        {
            auto jv = pay(alice, bob, USD(1));
            jv[sfFlags.fieldName] = tfInnerBatchTxn;

            Serializer s;
            auto jt = env.jt(jv);
            jv.removeMember(sfTxnSignature.jsonName);
            s.erase();
            jt.stx->add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "error" &&
                jrr[jss::error] == "invalidTransaction");

            env.close();
        }

        // Invalid: txn has `tfInnerBatchTxn` flag and no signature
        {
            std::string txBlob =
                "1200002240000000240000000561D4838D7EA4C68000000000000000000000"
                "0000005553440000000000A407AF5856CCF3C42619DAA925813FC955C72983"
                "68400000000000000A73210388935426E0D08083314842EDFBB2D517BD4769"
                "9F9A4527318A8E10468C97C0528114AE123A8556F3CF91154711376AFB0F89"
                "4F832B3D8314F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B90F";
            auto const jrr = env.rpc("submit", txBlob)[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "success" &&
                jrr[jss::engine_result] == "temINVALID_BATCH");

            env.close();
        }
    }

    void
    testNoAccount(FeatureBitset features)
    {
        testcase("no account");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice);
        env.close();
        env.memoize(bob);

        auto const preAlice = env.balance(alice);

        // Tx 1
        Json::Value tx1 = noop(bob);
        tx1[sfSetFlag.fieldName] = asfAllowTrustLineClawback;

        auto const ledSeq = env.current()->seq();
        auto const seq = env.seq(alice);
        auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(1000)), seq + 1),
            batch::add(tx1, ledSeq),
            batch::sig(bob));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 5);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000) - batchFee);
        BEAST_EXPECT(env.balance(bob) == XRP(1000));
    }

    void
    testAccountSet(FeatureBitset features)
    {
        testcase("account set");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        // Tx 1
        Json::Value tx1 = noop(alice);
        std::string const domain = "example.com";
        tx1[sfDomain.fieldName] = strHex(domain);

        auto const seq = env.seq(alice);
        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(tx1, seq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 2));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(
            sle->getFieldVL(sfDomain) == Blob(domain.begin(), domain.end()));

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    void
    testObjectCreateSequence(FeatureBitset features)
    {
        testcase("object create w/ sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const seq = env.seq(alice);
        auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(check::create(bob, alice, USD(10)), env.seq(bob)),
            batch::add(check::cash(alice, chkId, USD(10)), seq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testObjectCreateTicket(FeatureBitset features)
    {
        testcase("object create w/ ticket");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const seq = env.seq(alice);
        auto const batchFee = ((1 + 2) * feeDrops) + feeDrops * 2;
        uint256 const chkId{getCheckIndex(bob, bobTicketSeq)};
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(check::create(bob, alice, USD(10)), 0, bobTicketSeq),
            batch::add(check::cash(alice, chkId, USD(10)), seq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        BEAST_EXPECT(env.seq(alice) == 7);
        BEAST_EXPECT(env.seq(bob) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testObjectCreate3rdParty(FeatureBitset features)
    {
        testcase("object create w/ 3rd party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, carol, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preCarol = env.balance(carol);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const seq = env.seq(carol);
        auto const batchFee = ((2 + 2) * feeDrops) + feeDrops * 2;
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::batch(carol, seq, batchFee, tfAllOrNothing),
            batch::add(check::create(bob, alice, USD(10)), env.seq(bob)),
            batch::add(check::cash(alice, chkId, USD(10)), env.seq(alice)),
            batch::sig(alice, bob));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preCarol, seq);

        BEAST_EXPECT(env.seq(alice) == 6);
        BEAST_EXPECT(env.seq(bob) == 6);
        BEAST_EXPECT(env.seq(carol) == 5);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(carol) == preCarol - batchFee);
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testTicketsOuter(FeatureBitset features)
    {
        testcase("tickets outer");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, 0, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(1)), seq + 0),
            batch::add(pay(alice, bob, XRP(1)), seq + 1),
            ticket::use(aliceTicketSeq++));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, 0, 10, 10);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 9);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 9);

        BEAST_EXPECT(env.seq(alice) == 17);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testTicketsInner(FeatureBitset features)
    {
        testcase("tickets inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, seq, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(1)), 0, aliceTicketSeq),
            batch::add(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, seq);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testTicketsOuterInner(FeatureBitset features)
    {
        testcase("tickets outer inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const feeDrops = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = ((0 + 2) * feeDrops) + feeDrops * 2;
        env(batch::batch(alice, 0, batchFee, tfAllOrNothing),
            batch::add(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
            batch::add(pay(alice, bob, XRP(1)), seq + 0),
            ticket::use(aliceTicketSeq));
        auto const txIDs = env.tx()->getFieldV256(sfTransactionIDs);
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        auto const txn = getTxByIndex(jrr, 0);
        validateBatchTxns(txn[jss::metaData], 2, testCases);
        validateBatchPreMeta(txn[jss::metaData], preAlice, 0, 10, 10);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == 16);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnable(features);
        testPreflight(features);
        // testNonTecInner(features);
        // testBadSequence(features);
        // testBadFeeNoSigner(features);
        // testBadFeeSigner(features);
        // testChangesBetweenViews(features);
        testAllOrNothing(features);
        testOnlyOne(features);
        testUntilFailure(features);
        testIndependent(features);
        testMultiParty(features);
        testMultisign(features);
        testMultisignMultiParty(features);
        testBatchType(features);
        testSubmit(features);
        testNoAccount(features);
        testAccountSet(features);
        testObjectCreateSequence(features);
        testObjectCreateTicket(features);
        testObjectCreate3rdParty(features);
        testTicketsOuter(features);
        testTicketsInner(features);
        testTicketsOuterInner(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Batch, app, ripple);

}  // namespace test
}  // namespace ripple