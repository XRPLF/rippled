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

#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>

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
    struct TestLedgerData
    {
        int index;
        std::string txType;
        std::string result;
        std::string txHash;
        bool isInner;
    };

    struct TestBatchData
    {
        std::string result;
        std::string txHash;
    };

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
    validateBatch(
        jtx::Env& env,
        TxID const& parentBatchId,
        std::vector<TestBatchData> const& batchResults,
        int const& pbIndex = 0)
    {
        Json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));

        // Validate the number of transactions in the ledger
        auto const transactions =
            jrr[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.size() == batchResults.size() + 1 + pbIndex);

        // Validate ttBatch is correct index
        auto const txn = getTxByIndex(jrr, pbIndex);
        BEAST_EXPECT(txn.isMember(jss::metaData));
        Json::Value const meta = txn[jss::metaData];
        BEAST_EXPECT(txn[sfTransactionType.jsonName] == "Batch");
        BEAST_EXPECT(meta[sfTransactionResult.jsonName] == "tesSUCCESS");

        // Validate the inner transactions
        for (TestBatchData const& batchResult : batchResults)
        {
            Json::Value jsonTx;
            jsonTx[jss::binary] = false;
            jsonTx[jss::transaction] = batchResult.txHash;
            Json::Value const jrr =
                env.rpc("tx", batchResult.txHash)[jss::result];
            BEAST_EXPECT(
                jrr[jss::meta][sfTransactionResult.jsonName] ==
                batchResult.result);
            BEAST_EXPECT(
                jrr[jss::meta][sfParentBatchID.jsonName] ==
                to_string(parentBatchId));
        }
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

    void
    validateInnerTxn(
        jtx::Env& env,
        TxID const& parentBatchId,
        TestLedgerData const& ledgerResult)
    {
        Json::Value const jrr = env.rpc("tx", ledgerResult.txHash)[jss::result];
        BEAST_EXPECT(jrr[sfTransactionType.jsonName] == ledgerResult.txType);
        BEAST_EXPECT(
            jrr[jss::meta][sfTransactionResult.jsonName] ==
            ledgerResult.result);
        BEAST_EXPECT(
            jrr[jss::meta][sfParentBatchID.jsonName] ==
            to_string(parentBatchId));
    }

    void
    validateLedgerTxns(
        jtx::Env& env,
        Json::Value const& jrr,
        std::vector<TestLedgerData> const& ledgerResults,
        TxID const& parentBatchId)
    {
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
            if (ledgerResult.isInner)
                validateInnerTxn(env, parentBatchId, ledgerResult);
        }
    }

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    static std::unique_ptr<Config>
    makeConfig(
        std::map<std::string, std::string> extraTxQ = {},
        std::map<std::string, std::string> extraVoting = {})
    {
        auto p = test::jtx::envconfig();
        auto& section = p->section("transaction_queue");
        section.set("ledgers_in_queue", "2");
        section.set("minimum_queue_size", "2");
        section.set("min_ledgers_to_compute_size_limit", "3");
        section.set("max_ledger_counts_to_store", "100");
        section.set("retry_sequence_percent", "25");
        section.set("normal_consensus_increase_percent", "0");

        for (auto const& [k, v] : extraTxQ)
            section.set(k, v);

        return p;
    }

    void
    checkMetrics(
        int line,
        jtx::Env& env,
        std::size_t expectedCount,
        std::optional<std::size_t> expectedMaxCount,
        std::size_t expectedInLedger)
    {
        auto const metrics = env.app().getTxQ().getMetrics(*env.current());
        using namespace std::string_literals;
        metrics.txCount == expectedCount
            ? pass()
            : fail(
                  "txCount: "s + std::to_string(metrics.txCount) + "/" +
                      std::to_string(expectedCount),
                  __FILE__,
                  line);
        metrics.txQMaxSize == expectedMaxCount
            ? pass()
            : fail(
                  "txQMaxSize: "s +
                      std::to_string(metrics.txQMaxSize.value_or(0)) + "/" +
                      std::to_string(expectedMaxCount.value_or(0)),
                  __FILE__,
                  line);
        metrics.txInLedger == expectedInLedger
            ? pass()
            : fail(
                  "txInLedger: "s + std::to_string(metrics.txInLedger) + "/" +
                      std::to_string(expectedInLedger),
                  __FILE__,
                  line);
    }

    auto
    openLedgerFee(jtx::Env& env, XRPAmount const& batchFee)
    {
        using namespace jtx;

        auto const& view = *env.current();
        auto metrics = env.app().getTxQ().getMetrics(view);
        return toDrops(metrics.openLedgerFeeLevel, batchFee) + 1;
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

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // ttBatch
            {
                auto const seq = env.seq(alice);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);
                auto const txResult =
                    withBatch ? ter(tesSUCCESS) : ter(temDISABLED);
                env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                    batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                    batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                    txResult);
                env.close();
            }

            // tfInnerBatchTxn
            // If the feature is disabled, the transaction fails with
            // temINVALID_FLAG If the feature is enabled, the transaction fails
            // early in checkValidity()
            {
                auto const txResult =
                    withBatch ? ter(telENV_RPC_FAILED) : ter(temINVALID_FLAG);
                env(pay(alice, bob, XRP(1)),
                    txflags(tfInnerBatchTxn),
                    txResult);
                env.close();
            }

            // TransactionType missing tfUniversalMask flag
            // If the feature is disabled, the transaction is successful
            // If the feature is enabled, the transaction fails early in
            // checkValidity()
            {
                auto const txResult =
                    withBatch ? ter(telENV_RPC_FAILED) : ter(tesSUCCESS);
                env(signers(alice, 2, {{bob, 1}, {carol, 1}}),
                    txflags(tfInnerBatchTxn),
                    txResult);
                env.close();
            }

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

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // temBAD_FEE: preflight1
        {
            env(batch::outer(alice, env.seq(alice), XRP(-1), tfAllOrNothing),
                ter(temBAD_FEE));
            env.close();
        }

        // DEFENSIVE: temINVALID_FLAG: Batch: inner batch flag.
        // ACTUAL: telENV_RPC_FAILED: checkValidity()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfInnerBatchTxn),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_FLAG: Batch: invalid flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfDisallowXRP),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temINVALID_FLAG: Batch: too many flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                txflags(tfAllOrNothing | tfOnlyOne),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                ter(temARRAY_EMPTY));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                ter(temARRAY_EMPTY));
            env.close();
        }

        // DEFENSIVE: temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(1)), seq + 3),
                batch::inner(pay(alice, bob, XRP(1)), seq + 4),
                batch::inner(pay(alice, bob, XRP(1)), seq + 5),
                batch::inner(pay(alice, bob, XRP(1)), seq + 6),
                batch::inner(pay(alice, bob, XRP(1)), seq + 7),
                batch::inner(pay(alice, bob, XRP(1)), seq + 8),
                batch::inner(pay(alice, bob, XRP(1)), seq + 9),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: duplicate Txn found.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1));

            env(jt.jv, batch::sig(bob), ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: batch cannot have inner batch txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(
                    batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn must have the
        // tfInnerBatchTxn flag.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1[jss::Flags] = 0;
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn cannot include TxnSignature.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jt =
                env.jt(batch::inner(pay(alice, bob, XRP(1)), seq + 1).getTxn());
            jt.jv[jss::SigningPubKey] = "";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner_nofill(jt.jv),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn must include empty
        // SigningPubKey.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::SigningPubKey] = strHex(alice.pk());
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(1)), seq + 2));

            env(jt.jv, ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn cannot include Signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[sfSigners.jsonName] = Json::arrayValue;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName] = Json::objectValue;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName][sfAccount.jsonName] =
                alice.human();
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName]
               [sfSigningPubKey.jsonName] = strHex(alice.pk());
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName]
               [sfTxnSignature.jsonName] = "DEADBEEF";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(tx1, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn preflight failed.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // amount can't be negative
                batch::inner(pay(alice, bob, XRP(-1)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn must have a fee of 0.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::Fee] = to_string(env.current()->fees().base);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn cannot have both Sequence
        // and TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::inner(pay(alice, bob, XRP(1)), 0, 1);
            tx1[jss::Sequence] = seq + 1;
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn must have either Sequence or
        // TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: duplicate sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 1),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: duplicate ticket found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), 0, seq + 1),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: duplicate ticket & sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 1),
                ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // DEFENSIVE: temARRAY_TOO_LARGE: Batch: signers array exceeds 8
        // entries.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(5)), seq + 2),
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
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temBAD_SIGNER: Batch: signer cannot be the outer account
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(alice, bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: duplicate signer found
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(bob, bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        // Note: Extra signature by bob
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::sig(bob),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(carol),
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            auto const seq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq));

            Serializer msg;
            serializeBatch(
                msg, tfAllOrNothing, jt.stx->getBatchTransactionIDs());
            auto const sig = ripple::sign(bob.pk(), bob.sk(), msg.slice());
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName]
                 [sfAccount.jsonName] = bob.human();
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName]
                 [sfSigningPubKey.jsonName] = strHex(alice.pk());
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName]
                 [sfTxnSignature.jsonName] =
                strHex(Slice{sig.data(), sig.size()});

            env(jt.jv, ter(temBAD_SIGNATURE));
            env.close();
        }

        // temBAD_SIGNER: Batch: invalid batch signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::inner(pay(carol, alice, XRP(5)), env.seq(carol)),
                batch::sig(bob),
                ter(temBAD_SIGNER));
            env.close();
        }
    }

    void
    testPreclaim(FeatureBitset features)
    {
        testcase("preclaim");

        using namespace test::jtx;
        using namespace std::literals;

        //----------------------------------------------------------------------
        // preclaim

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const elsa = Account("elsa");
        auto const frank = Account("frank");
        auto const phantom = Account("phantom");
        env.memoize(phantom);

        env.fund(XRP(10000), alice, bob, carol, dave, elsa, frank);
        env.close();

        //----------------------------------------------------------------------
        // checkBatchSign.checkMultiSign

        // tefNOT_MULTI_SIGNING: SignersList not enabled
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {dave, carol}),
                ter(tefNOT_MULTI_SIGNING));
            env.close();
        }

        env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
        env.close();

        env(signers(bob, 2, {{carol, 1}, {dave, 1}, {elsa, 1}}));
        env.close();

        // tefBAD_SIGNATURE: Account not in SignersList
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, frank}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Wrong publicKey type
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Account("dave", KeyType::ed25519)}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefMASTER_DISABLED: Master key disabled
        {
            env(regkey(elsa, frank));
            env(fset(elsa, asfDisableMaster), sig(elsa));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, elsa}),
                ter(tefMASTER_DISABLED));
            env.close();
        }
        // tefBAD_SIGNATURE: Signer does not exist
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, phantom}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Signer has not enabled RegularKey
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Reg{dave, davo}}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_SIGNATURE: Wrong RegularKey Set
        {
            env(regkey(dave, frank));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, Reg{dave, davo}}),
                ter(tefBAD_SIGNATURE));
            env.close();
        }
        // tefBAD_QUORUM
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol}),
                ter(tefBAD_QUORUM));
            env.close();
        }
        // tesSUCCESS: BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, dave}),
                ter(tesSUCCESS));
            env.close();
        }
        // tesSUCCESS: Multisign + BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 4, 2);
            Account const davo{"davo", KeyType::ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::msig(bob, {carol, dave}),
                msig(bob, carol),
                ter(tesSUCCESS));
            env.close();
        }

        //----------------------------------------------------------------------
        // checkBatchSign.checkSingleSign

        // tefBAD_AUTH: Wrong PublicKey Type
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(Reg{bob, Account("bob", KeyType::ed25519)}),
                ter(tefBAD_AUTH));
            env.close();
        }
        // tefBAD_AUTH: Account is not signer
        {
            auto const ledSeq = env.current()->seq();
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1000)), seq + 1),
                batch::inner(noop(bob), ledSeq),
                batch::sig(Reg{bob, carol}),
                ter(tefBAD_AUTH));
            env.close();
        }
        // tesSUCCESS: Signed With Regular Key
        {
            env(regkey(bob, carol));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(Reg{bob, carol}),
                ter(tesSUCCESS));
            env.close();
        }
        // tesSUCCESS: Signed With Master Key
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(bob),
                ter(tesSUCCESS));
            env.close();
        }
        // tefMASTER_DISABLED: Signed With Master Key Disabled
        {
            env(regkey(bob, carol));
            env(fset(bob, asfDisableMaster), sig(bob));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::sig(bob),
                ter(tefMASTER_DISABLED));
            env.close();
        }
    }

    void
    testBadRawTxn(FeatureBitset features)
    {
        testcase("bad raw txn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);

        // Invalid: sfTransactionType
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::TransactionType);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfAccount
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Account);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfSequence
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Sequence);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfFee
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Fee);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfSigningPubKey
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::SigningPubKey);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::sig(bob), ter(telENV_RPC_FAILED));
            env.close();
        }
    }

    void
    testBadSequence(FeatureBitset features)
    {
        testcase("bad sequence");

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

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        // Invalid: Alice Sequence is a past sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq - 10),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Alice Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 10),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Bob Sequence is a past sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq - 10),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }

        // Invalid: Bob Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq + 10),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

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

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

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
    testBadOuterFee(FeatureBitset features)
    {
        testcase("bad outer fee");

        using namespace test::jtx;
        using namespace std::literals;

        // Bad Fee Without Signer
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 0, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 1);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 2, 2)
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 3, 2)
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners.Signers
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            env(signers(bob, 2, {{alice, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 4, 2)
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::msig(bob, {alice, carol}),
                msig(bob, carol),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With BatchSigners
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            env(noop(bob), ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 1, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee Dynamic Fee Calculation
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            auto const ammCreate =
                [&alice](STAmount const& amount, STAmount const& amount2) {
                    Json::Value jv;
                    jv[jss::Account] = alice.human();
                    jv[jss::Amount] = amount.getJson(JsonOptions::none);
                    jv[jss::Amount2] = amount2.getJson(JsonOptions::none);
                    jv[jss::TradingFee] = 0;
                    jv[jss::TransactionType] = jss::AMMCreate;
                    return jv;
                };

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(ammCreate(XRP(10), USD(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 2),
                ter(telINSUF_FEE_P));
            env.close();
        }
    }

    void
    testCalculateBaseFee(FeatureBitset features)
    {
        testcase("calculate base fee");

        using namespace test::jtx;
        using namespace std::literals;

        // telENV_RPC_FAILED: Batch: txns array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq));

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    auto const result =
                        ripple::apply(env.app(), view, *jt.stx, tapNONE, j);
                    BEAST_EXPECT(
                        !result.applied && result.ter == temARRAY_TOO_LARGE);
                    return result.applied;
                });
        }

        // telENV_RPC_FAILED: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob));

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    auto const result =
                        ripple::apply(env.app(), view, *jt.stx, tapNONE, j);
                    BEAST_EXPECT(
                        !result.applied && result.ter == temARRAY_TOO_LARGE);
                    return result.applied;
                });
        }
    }

    void
    testAllOrNothing(FeatureBitset features)
    {
        testcase("all or nothing");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        // all
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tec failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequence
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays Fee; Bob should not be affected
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // tef failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                // tx #2 fails with tefNO_AUTH_REQUIRED
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequence
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays Fee; Bob should not be affected
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // ter failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 2),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                // tx #2 fails with terPRE_TICKET
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequence
            BEAST_EXPECT(env.seq(alice) == seq + 1);

            // Alice pays Fee; Bob should not be affected
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

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, carol, dave, gw);
        env.close();

        // all transactions fail
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
            // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[0])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 4);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // first transaction fails
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
        }

        // tec failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 2);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
        }

        // tef failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                // tx #1 fails with tefNO_AUTH_REQUIRED
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 2);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee - XRP(1));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
        }

        // ter failure
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                // tx #1 fails with terPRE_TICKET
                {"tesSUCCESS", to_string(txIDs[1])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 2);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee - XRP(1));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
        }

        // tec (tecKILLED) error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 6);
            env(batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 1),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3),
                batch::inner(pay(alice, bob, XRP(100)), seq + 4),
                batch::inner(pay(alice, carol, XRP(100)), seq + 5),
                batch::inner(pay(alice, dave, XRP(100)), seq + 6));

            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecKILLED", to_string(txIDs[0])},
                {"tecKILLED", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(100) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }
    }

    void
    testUntilFailure(FeatureBitset features)
    {
        testcase("until failure");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, carol, dave, gw);
        env.close();

        // first transaction fails
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[0])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 2);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // all transactions succeed
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3),
                batch::inner(pay(alice, bob, XRP(4)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tesSUCCESS", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 5);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(10));
        }

        // tec error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 4);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tef error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with tefNO_AUTH_REQUIRED
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }
        
        // ter error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with terPRE_TICKET
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tec (tecKILLED) error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            env(batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3),
                batch::inner(pay(alice, dave, XRP(100)), seq + 4));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }
    }

    void
    testIndependent(FeatureBitset features)
    {
        testcase("independent");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, bob, carol, gw);
        env.close();

        // multiple transactions fail
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 5);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(4) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(4));
        }

        // tec error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecUNFUNDED_PAYMENT", to_string(txIDs[2])},
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 5);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(6) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(6));
        }

        // tef error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with tefNO_AUTH_REQUIRED
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 4);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee - XRP(6));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(6));
        }

        // ter error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3),
                ter(tesSUCCESS));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // tx #3 fails with terPRE_TICKET
                {"tesSUCCESS", to_string(txIDs[3])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 4);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee - XRP(6));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(6));
        }

        // tec (tecKILLED) error
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                {"tecKILLED", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }
    }

    void
    testInnerSubmitRPC(FeatureBitset features)
    {
        testcase("inner submit rpc");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto submitAndValidate = [&](Slice const& slice) {
            auto const jrr = env.rpc("submit", strHex(slice))[jss::result];
            BEAST_EXPECT(
            jrr[jss::status] == "error" &&
            jrr[jss::error] == "invalidTransaction" &&
            jrr[jss::error_exception] ==
                "fails local checks: Malformed: Invalid inner batch "
                "transaction.");
            env.close();
        };

        // Invalid RPC Submission: TxnSignature
        // - has `TxnSignature` field
        // - has no `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            auto txn = batch::inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfTxnSignature] = "DEADBEEF";
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);
            submitAndValidate(s.slice());
        }

        // Invalid RPC Submission: SigningPubKey
        // - has no `TxnSignature` field
        // - has `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            auto txn = batch::inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfSigningPubKey] = strHex(alice.pk());
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);
            submitAndValidate(s.slice());
        }

        // Invalid RPC Submission: Signers
        // - has no `TxnSignature` field
        // - has empty `SigningPubKey` field
        // - has `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            auto txn = batch::inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfSigners] = Json::arrayValue;
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);
            submitAndValidate(s.slice());
        }

        // Invalid RPC Submission: tfInnerBatchTxn
        // - has no `TxnSignature` field
        // - has empty `SigningPubKey` field
        // - has no `Signers` field
        // - has `tfInnerBatchTxn` flag
        {
            auto txn = batch::inner(pay(alice, bob, XRP(1)), env.seq(alice));
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            BEAST_EXPECT(
                jrr[jss::status] == "success" &&
                jrr[jss::engine_result] == "temINVALID_FLAG");

            env.close();
        }
    }

    void
    testAccountActivation(FeatureBitset features)
    {
        testcase("account activation");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice);
        env.close();
        env.memoize(bob);

        auto const preAlice = env.balance(alice);
        auto const ledSeq = env.current()->seq();
        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1000)), seq + 1),
            batch::inner(fset(bob, asfAllowTrustLineClawback), ledSeq),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == seq + 2);

        // Bob consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == ledSeq + 1);

        // Alice pays XRP & Fee; Bob receives XRP
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

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        // Tx 1
        Json::Value tx1 = noop(alice);
        std::string const domain = "example.com";
        tx1[sfDomain.fieldName] = strHex(domain);

        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner(tx1, seq + 1),
            batch::inner(pay(alice, bob, XRP(1)), seq + 2));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(
            sle->getFieldVL(sfDomain) == Blob(domain.begin(), domain.end()));

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == seq + 3);

        // Alice pays XRP & Fee; Bob receives XRP
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account delete");

        using namespace test::jtx;
        using namespace std::literals;

        // tfIndependent: account delete success
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee =
                batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(acctdelete(alice, bob), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tesSUCCESS", to_string(txIDs[1])},
                // terNO_ACCOUNT: alice does not exist
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice does not exist; Bob receives Alice's XRP
            BEAST_EXPECT(!env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob + (preAlice - batchFee));
        }

        // tfIndependent: account delete fails
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env.trust(bob["USD"](1000), alice);
            env.close();

            auto const seq = env.seq(alice);
            auto const batchFee =
                batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            env(batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(acctdelete(alice, bob), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {
                {"tesSUCCESS", to_string(txIDs[0])},
                {"tecHAS_OBLIGATIONS", to_string(txIDs[1])},
                {"tesSUCCESS", to_string(txIDs[2])},
            };
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice does not exist; Bob receives XRP
            BEAST_EXPECT(env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }
        
        // tfAllOrNothing: account delete fails
        {
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee =
                batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(acctdelete(alice, bob), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            auto const txIDs = env.tx()->getBatchTransactionIDs();
            TxID const parentBatchId = env.tx()->getTransactionID();
            std::vector<TestBatchData> testCases = {};
            env.close();
            validateBatch(env, parentBatchId, testCases);

            // Alice still exists; Bob is unchanged
            BEAST_EXPECT(env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob);
        }
    }

    void
    testObjectCreateSequence(FeatureBitset features)
    {
        testcase("object create w/ sequence");

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

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), bobSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);

        // Alice pays Fee; Bob XRP Unchanged
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);

        // Alice pays USD & Bob receives USD
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

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        uint256 const chkId{getCheckIndex(bob, bobTicketSeq)};
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), 0, bobTicketSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
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

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const carolSeq = env.seq(carol);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preCarol = env.balance(carol);
        auto const preAliceUSD = env.balance(alice, USD.issue());
        auto const preBobUSD = env.balance(bob, USD.issue());

        auto const batchFee = batch::calcBatchFee(env, 2, 2);
        uint256 const chkId{getCheckIndex(bob, env.seq(bob))};
        env(batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), bobSeq),
            batch::inner(check::cash(alice, chkId, USD(10)), aliceSeq),
            batch::sig(alice, bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        BEAST_EXPECT(env.seq(carol) == carolSeq + 1);
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

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, 0, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 0),
            batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
            ticket::use(aliceTicketSeq));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 9);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 9);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testTicketsInner(FeatureBitset features)
    {
        testcase("tickets inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq),
            batch::inner(pay(alice, bob, XRP(2)), 0, aliceTicketSeq + 1));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testTicketsOuterInner(FeatureBitset features)
    {
        testcase("tickets outer inner");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(alice, 0, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
            batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
            ticket::use(aliceTicketSeq));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };
        env.close();
        validateBatch(env, parentBatchId, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
        BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
    }

    void
    testSequenceOpenLedger(FeatureBitset features)
    {
        testcase("sequence open ledger");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // Before Batch Txn w/ same sequence
        // Batch inner using non existing ticket
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const batchTxn = env.jt(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(
                    pay(alice, bob, XRP(1)),
                    0,
                    aliceSeq + 1),  // Uses Ticket (Fails with terPRE_TICKET)
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 2));
            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 1));
            env(noopTxn, ter(terPRE_SEQ));
            env(batchTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1,
                 "AccountSet",
                 "tesSUCCESS",
                 to_string(noopTxn.stx->getTransactionID()),
                 false},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }

        // Before Batch Txn w/ same sequence
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const batchTxn = env.jt(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 2));
            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 1));
            env(noopTxn, ter(terPRE_SEQ));
            env(batchTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "Payment", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }

        // After Batch Txn w/ same sequence
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const batchTxn = env.jt(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 2));
            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 1));
            env(batchTxn);
            env(noopTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "Payment", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }
    }

    void
    testTicketsOpenLedger(FeatureBitset features)
    {
        testcase("tickets open ledger");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // Before Batch Txn w/ same ticket
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const batchTxn = env.jt(
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::use(aliceTicketSeq));
            auto const noopTxn =
                env.jt(noop(alice), ticket::use(aliceTicketSeq + 1));
            env(noopTxn);
            env(batchTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "Payment", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }

        // After Batch Txn w/ same ticket
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const batchTxn = env.jt(
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::use(aliceTicketSeq));
            auto const noopTxn =
                env.jt(noop(alice), ticket::use(aliceTicketSeq + 1));
            env(batchTxn);
            env(noopTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "Payment", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }
    }

    void
    testObjectsOpenLedger(FeatureBitset features)
    {
        testcase("objects open ledger");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // Before Batch Txn
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            uint256 const chkId{getCheckIndex(alice, aliceSeq)};
            auto const batchTxn = env.jt(
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::use(aliceTicketSeq));
            auto const objTxn = env.jt(check::cash(bob, chkId, XRP(10)));
            env(objTxn, ter(tecNO_ENTRY));
            env(batchTxn);
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "CheckCreate", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
                {3,
                 "CheckCash",
                 "tesSUCCESS",
                 to_string(objTxn.stx->getTransactionID()),
                 false},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }

        // After Batch Txn
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            uint256 const chkId{getCheckIndex(alice, aliceSeq)};
            auto const batchTxn = env.jt(
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::use(aliceTicketSeq));
            auto const objTxn = env.jt(check::cash(bob, chkId, XRP(10)));
            env(batchTxn);
            env(objTxn, ter(tecNO_ENTRY));
            env.close();

            auto const txIDs = batchTxn.stx->getBatchTransactionIDs();
            TxID const parentBatchId = batchTxn.stx->getTransactionID();
            Json::Value const jrr = getLastLedger(env);
            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", to_string(parentBatchId), false},
                {1, "CheckCreate", "tesSUCCESS", to_string(txIDs[0]), true},
                {2, "Payment", "tesSUCCESS", to_string(txIDs[1]), true},
                {3,
                 "CheckCash",
                 "tesSUCCESS",
                 to_string(objTxn.stx->getTransactionID()),
                 false},
            };
            validateLedgerTxns(env, jrr, testCases, parentBatchId);
        }
    }

    void
    testPseudoTxn(FeatureBitset features)
    {
        testcase("pseudo txn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        STTx const stx = STTx(ttAMENDMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID());
            obj.setFieldH256(sfAmendment, uint256(2));
            obj.setFieldU32(sfLedgerSequence, env.seq(alice));
            obj.setFieldU32(sfFlags, tfInnerBatchTxn);
        });

        std::string reason;
        BEAST_EXPECT(isPseudoTx(stx));
        BEAST_EXPECT(!passesLocalChecks(stx, reason));
        BEAST_EXPECT(reason == "Cannot submit pseudo transactions.");
        env.app().openLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const result = ripple::apply(env.app(), view, stx, tapNONE, j);
            BEAST_EXPECT(!result.applied && result.ter == temINVALID_FLAG);
            return result.applied;
        });
    }

    void
    testBatchWithSelfSubmit(FeatureBitset features)
    {
        testcase("batch with self submit");
        // IMPORTANT: When a transaction is self submitted and another
        // transaction is part of the batch, the batch might fail because the
        // sequence is out of order. This is because the canonical order of
        // transactions is determined by the account first. So in this case,
        // alice's batch comes after bobs self submitted transaction even though
        // the self submitted was after the batch.

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        XRPAmount const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(1000), alice, bob);
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const bobSeq = env.seq(bob);

        // Alice Pays Bob (Self Submit)
        env(pay(alice, bob, XRP(10)), ter(tesSUCCESS));

        // Alice & Bob Atomic Batch
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        env(batch::outer(alice, aliceSeq + 1, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 2),
            batch::inner(pay(bob, alice, XRP(5)), bobSeq + 1),
            batch::sig(bob));
        auto const txIDs = env.tx()->getBatchTransactionIDs();
        TxID const parentBatchId = env.tx()->getTransactionID();
        std::vector<TestBatchData> testCases = {
            {"tesSUCCESS", to_string(txIDs[0])},
            {"tesSUCCESS", to_string(txIDs[1])},
        };

        // Bob pays Alice (Self Submit)
        env(pay(bob, alice, XRP(5)), ter(tesSUCCESS));
        env.close();

        validateBatch(env, parentBatchId, testCases, 2);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 3);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == bobSeq + 2);

        // Alice pays XRP & Fee; Bob receives XRP & pays Fee
        BEAST_EXPECT(
            env.balance(alice) == preAlice - XRP(10) - batchFee - baseFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(10) - baseFee);
    }

    void
    testBatchTxQueue(FeatureBitset features)
    {
        testcase("batch tx queue");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{
            *this,
            makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}),
            nullptr,
            beast::severities::kError};

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto carol = Account("carol");

        auto queued = ter(terQUEUED);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(carol));
        env.close(env.now() + 5s, 10000ms);

        // Fill the ledger
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3);

        env(noop(carol), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 3);

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);

        // Queue Batch
        {
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                queued);
        }

        checkMetrics(__LINE__, env, 2, std::nullopt, 3);

        // Replace Queued Batch
        {
            env(batch::outer(
                    alice,
                    aliceSeq,
                    openLedgerFee(env, batchFee),
                    tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::sig(bob),
                ter(tesSUCCESS));
            env.close();
        }

        checkMetrics(__LINE__, env, 0, 12, 1);
    }

    void
    testBatchNetworkOps(FeatureBitset features)
    {
        testcase("batch network ops");

        using namespace test::jtx;
        using namespace std::literals;

        Env env(
            *this,
            envconfig(),
            features,
            nullptr,
            beast::severities::kDisabled);

        auto alice = Account("alice");
        auto bob = Account("bob");
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto submitTx = [&](std::uint32_t flags) -> uint256 {
            auto jv = pay(alice, bob, XRP(1));
            jv[sfFlags.fieldName] = flags;
            Serializer s;
            auto jt = env.jt(jv);
            jt.stx->add(s);
            env.app().getOPs().submitTransaction(jt.stx);
            return jt.stx->getTransactionID();
        };

        auto processTxn = [&](std::uint32_t flags) -> uint256 {
            auto jv = pay(alice, bob, XRP(1));
            jv[sfFlags.fieldName] = flags;
            Serializer s;
            auto jt = env.jt(jv);
            jt.stx->add(s);
            std::string reason;
            auto transaction =
                std::make_shared<Transaction>(jt.stx, reason, env.app());
            env.app().getOPs().processTransaction(
                transaction, false, true, NetworkOPs::FailHard::yes);
            return transaction->getID();
        };

        // Validate: NetworkOPs::submitTransaction()
        {
            // Submit a tx with tfPartialPayment
            uint256 const txGood = submitTx(tfPartialPayment);
            BEAST_EXPECT(
                env.app().getHashRouter().getFlags(txGood) ==
                SF_PRIVATE2 + SF_PRIVATE4);
            // Submit a tx with tfInnerBatchTxn
            uint256 const txBad = submitTx(tfInnerBatchTxn);
            BEAST_EXPECT(env.app().getHashRouter().getFlags(txBad) == 0);
        }

        // Validate: NetworkOPs::processTransaction()
        {
            uint256 const txid = processTxn(tfInnerBatchTxn);
            // HashRouter::getFlags() should return SF_BAD
            BEAST_EXPECT(env.app().getHashRouter().getFlags(txid) == SF_BAD);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnable(features);
        testPreflight(features);
        testPreclaim(features);
        testBadRawTxn(features);
        testBadSequence(features);
        testBadOuterFee(features);
        testCalculateBaseFee(features);
        testAllOrNothing(features);
        testOnlyOne(features);
        testUntilFailure(features);
        testIndependent(features);
        testInnerSubmitRPC(features);
        testAccountActivation(features);
        testAccountSet(features);
        testAccountDelete(features);
        testObjectCreateSequence(features);
        testObjectCreateTicket(features);
        testObjectCreate3rdParty(features);
        testTicketsOuter(features);
        testTicketsInner(features);
        testTicketsOuterInner(features);
        testSequenceOpenLedger(features);
        testTicketsOpenLedger(features);
        testObjectsOpenLedger(features);
        testPseudoTxn(features);
        testBatchWithSelfSubmit(features);
        testBatchTxQueue(features);
        testBatchNetworkOps(features);
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
