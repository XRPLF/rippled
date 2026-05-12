#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/batch.h>
#include <test/jtx/check.h>
#include <test/jtx/delegate.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class Batch_test : public beast::unit_test::Suite
{
    struct TestLedgerData
    {
        int index;
        std::string txType;
        std::string result;
        std::string txHash;
        std::optional<std::string> batchID;
    };

    struct TestBatchData
    {
        std::string result;
        std::string txHash;
    };

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

    static json::Value
    getLastLedger(jtx::Env& env)
    {
        json::Value params;
        params[jss::ledger_index] = env.closed()->seq();
        params[jss::transactions] = true;
        params[jss::expand] = true;
        return env.rpc("json", "ledger", to_string(params));
    }

    void
    validateInnerTxn(jtx::Env& env, std::string const& batchID, TestLedgerData const& ledgerResult)
    {
        json::Value const jrr = env.rpc("tx", ledgerResult.txHash)[jss::result];
        BEAST_EXPECT(jrr[sfTransactionType.jsonName] == ledgerResult.txType);
        BEAST_EXPECT(jrr[jss::meta][sfTransactionResult.jsonName] == ledgerResult.result);
        BEAST_EXPECT(jrr[jss::meta][sfParentBatchID.jsonName] == batchID);
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
            BEAST_EXPECT(txn[jss::hash].asString() == ledgerResult.txHash);
            BEAST_EXPECT(txn.isMember(jss::metaData));
            json::Value const meta = txn[jss::metaData];
            BEAST_EXPECT(txn[sfTransactionType.jsonName] == ledgerResult.txType);
            BEAST_EXPECT(meta[sfTransactionResult.jsonName] == ledgerResult.result);
            if (ledgerResult.batchID)
                validateInnerTxn(env, *ledgerResult.batchID, ledgerResult);
        }
    }

    template <typename... Args>
    std::pair<std::vector<std::string>, std::string>
    submitBatch(jtx::Env& env, TER const& result, Args&&... args)
    {
        auto batchTxn = env.jt(std::forward<Args>(args)...);
        env(batchTxn, jtx::Ter(result));

        auto const ids = batchTxn.stx->getBatchTransactionIDs();
        std::vector<std::string> txIDs;
        txIDs.reserve(ids.size());
        for (auto const& id : ids)
            txIDs.push_back(strHex(id));
        TxID const batchID = batchTxn.stx->getTransactionID();
        return std::make_pair(txIDs, strHex(batchID));
    }

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    static std::unique_ptr<Config>
    makeSmallQueueConfig(
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

    static auto
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
        using namespace test::jtx;
        using namespace std::literals;

        bool const withInnerSigFix = features[fixBatchInnerSigs];

        for (bool const withBatch : {true, false})
        {
            testcase << "enabled: Batch " << (withBatch ? "enabled" : "disabled")
                     << ", Inner Sig Fix: " << (withInnerSigFix ? "enabled" : "disabled");

            auto const amend = withBatch ? features : features - featureBatch;

            test::jtx::Env env{*this, amend};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            // ttBatch
            {
                auto const seq = env.seq(alice);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);
                auto const txResult = withBatch ? Ter(tesSUCCESS) : Ter(temDISABLED);
                env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                    batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                    batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                    txResult);
                env.close();
            }

            // tfInnerBatchTxn
            // If the feature is disabled, the transaction fails with
            // temINVALID_FLAG. If the feature is enabled, the transaction fails
            // early in checkValidity()
            {
                auto const txResult = withBatch ? Ter(telENV_RPC_FAILED) : Ter(temINVALID_FLAG);
                env(pay(alice, bob, XRP(1)), Txflags(tfInnerBatchTxn), txResult);
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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        // temBAD_FEE: preflight1
        {
            env(batch::outer(alice, env.seq(alice), XRP(-1), tfAllOrNothing), Ter(temBAD_FEE));
            env.close();
        }

        // DEFENSIVE: temINVALID_FLAG: Batch: inner batch flag.
        // ACTUAL: telENV_RPC_FAILED: checkValidity()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfInnerBatchTxn), Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_FLAG: Batch: invalid flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfDisallowXRP), Ter(temINVALID_FLAG));
            env.close();
        }

        // temINVALID_FLAG: Batch: too many flags.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                Txflags(tfAllOrNothing | tfOnlyOne),
                Ter(temINVALID_FLAG));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing), Ter(temARRAY_EMPTY));
            env.close();
        }

        // temARRAY_EMPTY: Batch: txns array must have at least 2 entries.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 0);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                Ter(temARRAY_EMPTY));
            env.close();
        }

        // DEFENSIVE: temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 4),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 5),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 6),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 7),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 8),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 9),
                Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate Txn found.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1));

            env(jt.jv, batch::Sig(bob), Ter(temREDUNDANT));
            env.close();
        }

        // DEFENSIVE: temINVALID: Batch: batch cannot have inner batch txn.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_FLAG: Batch: inner txn must have the
        // tfInnerBatchTxn flag.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1[jss::Flags] = 0;
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(temINVALID_FLAG));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: inner txn cannot include TxnSignature.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jt = env.jt(pay(alice, bob, XRP(1)));
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(jt.jv, seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                Ter(temBAD_SIGNATURE));
            env.close();
        }

        // temBAD_SIGNER: Batch: inner txn cannot include Signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = pay(alice, bob, XRP(1));
            tx1[sfSigners.jsonName] = json::ValueType::Array;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName] = json::ValueType::Object;
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName][sfAccount.jsonName] = alice.human();
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName][sfSigningPubKey.jsonName] =
                strHex(alice.pk());
            tx1[sfSigners.jsonName][0U][sfSigner.jsonName][sfTxnSignature.jsonName] = "DEADBEEF";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(tx1, seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                Ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_REGKEY: Batch: inner txn must include empty
        // SigningPubKey.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::SigningPubKey] = strHex(alice.pk());
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2));

            env(jt.jv, Ter(temBAD_REGKEY));
            env.close();
        }

        // temINVALID_INNER_BATCH: Batch: inner txn preflight failed.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // amount can't be negative
                batch::Inner(pay(alice, bob, XRP(-1)), seq + 2),
                Ter(temINVALID_INNER_BATCH));
            env.close();
        }

        // temBAD_FEE: Batch: inner txn must have a fee of 0.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::Fee] = to_string(env.current()->fees().base);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                Ter(temBAD_FEE));
            env.close();
        }

        // temBAD_FEE: Inner txn with negative fee
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::Fee] = "-1";
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                Ter(temBAD_FEE));
            env.close();
        }

        // temBAD_FEE: Inner txn with non-integer fee
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::Fee] = "1.5";
            env.setParseFailureExpected(true);
            try
            {
                env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                    tx1,
                    batch::Inner(pay(alice, bob, XRP(2)), seq + 2));
                fail("Expected parse_error for fractional fee");
            }
            catch (jtx::ParseError const&)
            {
                BEAST_EXPECT(true);
            }
            env.setParseFailureExpected(false);
        }

        // temSEQ_AND_TICKET: Batch: inner txn cannot have both Sequence
        // and TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(1)), 0, 1);
            tx1[jss::Sequence] = seq + 1;
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                Ter(temSEQ_AND_TICKET));
            env.close();
        }

        // temSEQ_AND_TICKET: Batch: inner txn must have either Sequence or
        // TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                Ter(temSEQ_AND_TICKET));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 1),
                Ter(temREDUNDANT));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate ticket found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), 0, seq + 1),
                Ter(temREDUNDANT));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate ticket & sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 1),
                Ter(temREDUNDANT));
            env.close();
        }

        // DEFENSIVE: temARRAY_TOO_LARGE: Batch: signers array exceeds 8
        // entries.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::Sig(bob, carol, alice, bob, carol, alice, bob, carol, alice, alice),
                Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temBAD_SIGNER: Batch: signer cannot be the outer account
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Sig(alice, bob),
                Ter(temBAD_SIGNER));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate signer found
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Sig(bob, bob),
                Ter(temREDUNDANT));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        // Note: Extra signature by bob
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::Sig(bob),
                Ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNER: Batch: no account signature for inner txn.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Sig(carol),
                Ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: invalid batch txn signature.
        {
            auto const seq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), bobSeq));

            Serializer msg;
            serializeBatch(msg, tfAllOrNothing, jt.stx->getBatchTransactionIDs());
            auto const sig = xrpl::sign(bob.pk(), bob.sk(), msg.slice());
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName][sfAccount.jsonName] =
                bob.human();
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName][sfSigningPubKey.jsonName] =
                strHex(alice.pk());
            jt.jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName][sfTxnSignature.jsonName] =
                strHex(Slice{sig.data(), sig.size()});

            env(jt.jv, Ter(temBAD_SIGNATURE));
            env.close();
        }

        // temBAD_SIGNER: Batch: invalid batch signers.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Inner(pay(carol, alice, XRP(5)), env.seq(carol)),
                batch::Sig(bob),
                Ter(temBAD_SIGNER));
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

        test::jtx::Env env{*this, features};

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
        // checkSign.checkSingleSign

        // tefBAD_AUTH: Bob is not authorized to sign for Alice
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(20)), seq + 2),
                Sig(bob),
                Ter(tefBAD_AUTH));
            env.close();
        }

        //----------------------------------------------------------------------
        // checkBatchSign.checkMultiSign

        // tefNOT_MULTI_SIGNING: SignersList not enabled
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {dave, carol}),
                Ter(tefNOT_MULTI_SIGNING));
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
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, frank}),
                Ter(tefBAD_SIGNATURE));
            env.close();
        }

        // tefBAD_SIGNATURE: Wrong publicKey type
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, Account("dave", KeyType::Ed25519)}),
                Ter(tefBAD_SIGNATURE));
            env.close();
        }

        // tefMASTER_DISABLED: Master key disabled
        {
            env(regkey(elsa, frank));
            env(fset(elsa, asfDisableMaster), Sig(elsa));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, elsa}),
                Ter(tefMASTER_DISABLED));
            env.close();
        }

        // tefBAD_SIGNATURE: Signer does not exist
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, phantom}),
                Ter(tefBAD_SIGNATURE));
            env.close();
        }

        // tefBAD_SIGNATURE: Signer has not enabled RegularKey
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::Ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, Reg{dave, davo}}),
                Ter(tefBAD_SIGNATURE));
            env.close();
        }

        // tefBAD_SIGNATURE: Wrong RegularKey Set
        {
            env(regkey(dave, frank));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            Account const davo{"davo", KeyType::Ed25519};
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, Reg{dave, davo}}),
                Ter(tefBAD_SIGNATURE));
            env.close();
        }

        // tefBAD_QUORUM
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol}),
                Ter(tefBAD_QUORUM));
            env.close();
        }

        // tesSUCCESS: BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, dave}),
                Ter(tesSUCCESS));
            env.close();
        }

        // tesSUCCESS: Multisign + BatchSigners.Signers
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 4, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::Msig(bob, {carol, dave}),
                Msig(bob, carol),
                Ter(tesSUCCESS));
            env.close();
        }

        //----------------------------------------------------------------------
        // checkBatchSign.checkSingleSign

        // tefBAD_AUTH: Inner Account is not signer
        {
            auto const ledSeq = env.current()->seq();
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, phantom, XRP(1000)), seq + 1),
                batch::Inner(noop(phantom), ledSeq),
                batch::Sig(Reg{phantom, carol}),
                Ter(tefBAD_AUTH));
            env.close();
        }

        // tefBAD_AUTH: Account is not signer
        {
            auto const ledSeq = env.current()->seq();
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1000)), seq + 1),
                batch::Inner(noop(bob), ledSeq),
                batch::Sig(Reg{bob, carol}),
                Ter(tefBAD_AUTH));
            env.close();
        }

        // tesSUCCESS: Signed With Regular Key
        {
            env(regkey(bob, carol));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::Sig(Reg{bob, carol}),
                Ter(tesSUCCESS));
            env.close();
        }

        // tesSUCCESS: Signed With Master Key
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::Sig(bob),
                Ter(tesSUCCESS));
            env.close();
        }

        // tefMASTER_DISABLED: Signed With Master Key Disabled
        {
            env(regkey(bob, carol));
            env(fset(bob, asfDisableMaster), Sig(bob));
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(2)), env.seq(bob)),
                batch::Sig(bob),
                Ter(tefMASTER_DISABLED));
            env.close();
        }
    }

    void
    testBadRawTxn(FeatureBitset features)
    {
        testcase("bad raw txn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);

        // Invalid: sfTransactionType
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::TransactionType);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfAccount
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Account);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfSequence
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Sequence);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfFee
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::Fee);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(telENV_RPC_FAILED));
            env.close();
        }

        // Invalid: sfSigningPubKey
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto tx1 = batch::Inner(pay(alice, bob, XRP(10)), seq + 1);
            tx1.removeMember(jss::SigningPubKey);
            auto jt = env.jtnofill(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2));

            env(jt.jv, batch::Sig(bob), Ter(telENV_RPC_FAILED));
            env.close();
        }
    }

    void
    testBadSequence(FeatureBitset features)
    {
        testcase("bad sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(100)));
        env.close();

        env(noop(bob), Ter(tesSUCCESS));
        env.close();

        // Invalid: Alice Sequence is a past sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), preAliceSeq - 10),
                batch::Inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
        }

        // Invalid: Alice Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), preAliceSeq + 10),
                batch::Inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
        }

        // Invalid: Bob Sequence is a past sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), preBobSeq - 10),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
        }

        // Invalid: Bob Sequence is a future sequence
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), preBobSeq + 10),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
        }

        // Invalid: Outer and Inner Sequence are the same
        {
            auto const preAliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobSeq = env.seq(bob);
            auto const preBob = env.balance(bob);
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), preAliceSeq),
                batch::Inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }

            // Alice pays fee & Bob should not be affected.
            BEAST_EXPECT(env.seq(alice) == preAliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.seq(bob) == preBobSeq);
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
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
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            env(noop(bob), Ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 0, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 1);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            env(noop(bob), Ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 2, 2)
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(15)), aliceSeq + 2),
                Msig(bob, carol),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            env(noop(bob), Ter(tesSUCCESS));
            env.close();

            env(signers(alice, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 3, 2)
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::Sig(bob),
                Msig(bob, carol),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With MultiSign + BatchSigners.Signers
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            env(noop(bob), Ter(tesSUCCESS));
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
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::Msig(bob, {alice, carol}),
                Msig(bob, carol),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee With BatchSigners
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            env(noop(bob), Ter(tesSUCCESS));
            env.close();

            // Bad Fee: Should be batch::calcBatchFee(env, 1, 2)
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::Sig(bob),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        // Bad Fee Dynamic Fee Calculation
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const usd = gw["USD"];

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            auto const ammCreate = [&alice](STAmount const& amount, STAmount const& amount2) {
                json::Value jv;
                jv[jss::Account] = alice.human();
                jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
                jv[jss::Amount2] = amount2.getJson(JsonOptions::Values::None);
                jv[jss::TradingFee] = 0;
                jv[jss::TransactionType] = jss::AMMCreate;
                return jv;
            };

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(ammCreate(XRP(10), usd(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 2),
                Ter(telINSUF_FEE_P));
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
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: txns array exceeds 8 entries.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq));

            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto const result = xrpl::apply(env.app(), view, *jt.stx, TapNone, j);
                BEAST_EXPECT(!result.applied && result.ter == temARRAY_TOO_LARGE);
                return result.applied;
            });
        }

        // telENV_RPC_FAILED: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 9, 2);
            env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::Sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob),
                Ter(telENV_RPC_FAILED));
            env.close();
        }

        // temARRAY_TOO_LARGE: Batch: signers array exceeds 8 entries.
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 9);
            auto const aliceSeq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(5)), aliceSeq + 2),
                batch::Sig(bob, bob, bob, bob, bob, bob, bob, bob, bob, bob));

            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto const result = xrpl::apply(env.app(), view, *jt.stx, TapNone, j);
                BEAST_EXPECT(!result.applied && result.ter == temARRAY_TOO_LARGE);
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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        // all
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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

            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // terPRE_TICKET: ticket does not exist
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), 0, seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            };
            validateClosedLedger(env, testCases);

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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, dave, gw);
        env.close();

        // all transactions fail
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 3);
            auto const seq = env.seq(alice);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tecUNFUNDED_PAYMENT", txIDs[0], batchID},
                {2, "Payment", "tecUNFUNDED_PAYMENT", txIDs[1], batchID},
                {3, "Payment", "tecUNFUNDED_PAYMENT", txIDs[2], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tecUNFUNDED_PAYMENT", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                // terPRE_TICKET: ticket does not exist
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), 0, seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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

            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfOnlyOne),
                batch::Inner(
                    offer(alice, alice["USD"](100), XRP(100), tfImmediateOrCancel), seq + 1),
                batch::Inner(
                    offer(alice, alice["USD"](100), XRP(100), tfImmediateOrCancel), seq + 2),
                batch::Inner(
                    offer(alice, alice["USD"](100), XRP(100), tfImmediateOrCancel), seq + 3),
                batch::Inner(pay(alice, bob, XRP(100)), seq + 4),
                batch::Inner(pay(alice, carol, XRP(100)), seq + 5),
                batch::Inner(pay(alice, dave, XRP(100)), seq + 6));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "OfferCreate", "tecKILLED", txIDs[0], batchID},
                {2, "OfferCreate", "tecKILLED", txIDs[1], batchID},
                {3, "OfferCreate", "tecKILLED", txIDs[2], batchID},
                {4, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, dave, gw);
        env.close();

        // first transaction fails
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tecUNFUNDED_PAYMENT", txIDs[0], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(4)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "Payment", "tesSUCCESS", txIDs[2], batchID},
                {4, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "Payment", "tecUNFUNDED_PAYMENT", txIDs[2], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // terPRE_TICKET: ticket does not exist
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), 0, seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfUntilFailure),
                batch::Inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::Inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::Inner(
                    offer(alice, alice["USD"](100), XRP(100), tfImmediateOrCancel), seq + 3),
                batch::Inner(pay(alice, dave, XRP(100)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "OfferCreate", "tecKILLED", txIDs[2], batchID},
            };
            validateClosedLedger(env, testCases);

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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();

        // multiple transactions fail
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 4);
            auto const seq = env.seq(alice);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tecUNFUNDED_PAYMENT", txIDs[1], batchID},
                {3, "Payment", "tecUNFUNDED_PAYMENT", txIDs[2], batchID},
                {4, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::Inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "Payment", "tecUNFUNDED_PAYMENT", txIDs[2], batchID},
                {4, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2),
                // terPRE_TICKET: ticket does not exist
                batch::Inner(trust(alice, usd(1000), tfSetfAuth), 0, seq + 3),
                batch::Inner(pay(alice, bob, XRP(3)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "Payment", "tesSUCCESS", txIDs[3], batchID},
            };
            validateClosedLedger(env, testCases);

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::Inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::Inner(
                    offer(alice, alice["USD"](100), XRP(100), tfImmediateOrCancel), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                {3, "OfferCreate", "tecKILLED", txIDs[2], batchID},
            };
            validateClosedLedger(env, testCases);

            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(200) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(100));
            BEAST_EXPECT(env.balance(carol) == preCarol + XRP(100));
        }
    }

    void
    doTestInnerSubmitRPC(FeatureBitset features, bool withBatch)
    {
        bool const withInnerSigFix = features[fixBatchInnerSigs];

        std::string const testName = [&]() {
            std::stringstream ss;
            ss << "inner submit rpc: batch " << (withBatch ? "enabled" : "disabled")
               << ", inner sig fix: " << (withInnerSigFix ? "enabled" : "disabled") << ": ";
            return ss.str();
        }();

        auto const amend = withBatch ? features : features - featureBatch;

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, amend};
        if (!BEAST_EXPECT(amend[featureBatch] == withBatch))
            return;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);
        env.close();

        auto submitAndValidate = [&](std::string caseName,
                                     Slice const& slice,
                                     int line,
                                     std::optional<std::string> expectedEnabled = std::nullopt,
                                     std::optional<std::string> expectedDisabled = std::nullopt,
                                     bool expectInvalidFlag = false) {
            testcase << testName << caseName
                     << (expectInvalidFlag ? " - Expected to reach tx engine!" : "");
            auto const jrr = env.rpc("submit", strHex(slice))[jss::result];
            auto const expected = withBatch
                ? expectedEnabled.value_or(
                      "fails local checks: Malformed: Invalid inner batch "
                      "transaction.")
                : expectedDisabled.value_or("fails local checks: Empty SigningPubKey.");
            if (expectInvalidFlag)
            {
                expect(
                    jrr[jss::status] == "success" && jrr[jss::engine_result] == "temINVALID_FLAG",
                    pretty(jrr),
                    __FILE__,
                    line);
            }
            else
            {
                expect(
                    jrr[jss::status] == "error" && jrr[jss::error] == "invalidTransaction" &&
                        jrr[jss::error_exception] == expected,
                    pretty(jrr),
                    __FILE__,
                    line);
            }
            env.close();
        };

        // Invalid RPC Submission: TxnSignature
        // + has `TxnSignature` field
        // - has no `SigningPubKey` field
        // - has no `Signers` field
        // + has `tfInnerBatchTxn` flag
        {
            auto txn = batch::Inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfTxnSignature] = "DEADBEEF";
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate("TxnSignature set", s.slice(), __LINE__);
        }

        // Invalid RPC Submission: SigningPubKey
        // - has no `TxnSignature` field
        // + has `SigningPubKey` field
        // - has no `Signers` field
        // + has `tfInnerBatchTxn` flag
        {
            auto txn = batch::Inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfSigningPubKey] = strHex(alice.pk());
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate(
                "SigningPubKey set",
                s.slice(),
                __LINE__,
                std::nullopt,
                "fails local checks: Invalid signature.");
        }

        // Invalid RPC Submission: Signers
        // - has no `TxnSignature` field
        // + has empty `SigningPubKey` field
        // + has `Signers` field
        // + has `tfInnerBatchTxn` flag
        {
            auto txn = batch::Inner(pay(alice, bob, XRP(1)), env.seq(alice));
            txn[sfSigners] = json::ValueType::Array;
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate(
                "Signers set",
                s.slice(),
                __LINE__,
                std::nullopt,
                "fails local checks: Invalid Signers array size.");
        }

        {
            // Fully signed inner batch transaction
            auto const txn = batch::Inner(pay(alice, bob, XRP(1)), env.seq(alice));
            auto const jt = env.jt(txn.getTxn());

            STParsedJSONObject parsed("test", jt.jv);
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate(
                "Fully signed", s.slice(), __LINE__, std::nullopt, std::nullopt, !withBatch);
        }

        // Invalid RPC Submission: tfInnerBatchTxn
        // - has no `TxnSignature` field
        // + has empty `SigningPubKey` field
        // - has no `Signers` field
        // + has `tfInnerBatchTxn` flag
        {
            auto txn = batch::Inner(pay(alice, bob, XRP(1)), env.seq(alice));
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate(
                "No signing fields set",
                s.slice(),
                __LINE__,
                "fails local checks: Empty SigningPubKey.",
                "fails local checks: Empty SigningPubKey.",
                withBatch && !withInnerSigFix);
        }

        // Invalid RPC Submission: tfInnerBatchTxn pseudo-transaction
        // - has no `TxnSignature` field
        // + has empty `SigningPubKey` field
        // - has no `Signers` field
        // + has `tfInnerBatchTxn` flag
        {
            STTx const amendTx(ttAMENDMENT, [seq = env.closed()->header().seq + 1](auto& obj) {
                obj.setAccountID(sfAccount, AccountID());
                obj.setFieldH256(sfAmendment, fixBatchInnerSigs);
                obj.setFieldU32(sfLedgerSequence, seq);
                obj.setFieldU32(sfFlags, tfInnerBatchTxn);
            });
            auto txn = batch::Inner(amendTx.getJson(JsonOptions::Values::None), env.seq(alice));
            STParsedJSONObject parsed("test", txn.getTxn());
            Serializer s;
            parsed.object->add(s);  // NOLINT(bugprone-unchecked-optional-access)
            submitAndValidate(
                "Pseudo-transaction",
                s.slice(),
                __LINE__,
                withInnerSigFix ? "fails local checks: Empty SigningPubKey."
                                : "fails local checks: Cannot submit pseudo transactions.",
                "fails local checks: Empty SigningPubKey.");
        }
    }

    void
    testInnerSubmitRPC(FeatureBitset features)
    {
        for (bool const withBatch : {true, false})
        {
            doTestInnerSubmitRPC(features, withBatch);
        }
    }

    void
    testAccountActivation(FeatureBitset features)
    {
        testcase("account activation");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice);
        env.close();
        env.memoize(bob);

        auto const preAlice = env.balance(alice);
        auto const ledSeq = env.current()->seq();
        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::Inner(pay(alice, bob, XRP(1000)), seq + 1),
            batch::Inner(fset(bob, asfAllowTrustLineClawback), ledSeq),
            batch::Sig(bob));
        env.close();

        std::vector<TestLedgerData> const testCases = {
            {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
            {2, "AccountSet", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

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

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        auto tx1 = batch::Inner(noop(alice), seq + 1);
        std::string domain = "example.com";
        tx1[sfDomain] = strHex(domain);
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, seq, batchFee, tfAllOrNothing),
            tx1,
            batch::Inner(pay(alice, bob, XRP(1)), seq + 2));
        env.close();

        std::vector<TestLedgerData> const testCases = {
            {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {1, "AccountSet", "tesSUCCESS", txIDs[0], batchID},
            {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

        auto const sle = env.le(keylet::account(alice));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldVL(sfDomain) == Blob(domain.begin(), domain.end()));

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
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            for (int i = 0; i < 5; ++i)
                env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(acctdelete(alice, bob), seq + 2),
                // terNO_ACCOUNT: alice does not exist
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "AccountDelete", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice does not exist; Bob receives Alice's XRP
            BEAST_EXPECT(!env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob + (preAlice - batchFee));
        }

        // tfIndependent: account delete fails
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            for (int i = 0; i < 5; ++i)
                env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env.trust(bob["USD"](1000), alice);
            env.close();

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecHAS_OBLIGATIONS: alice has obligations
                batch::Inner(acctdelete(alice, bob), seq + 2),
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "AccountDelete", "tecHAS_OBLIGATIONS", txIDs[1], batchID},
                {3, "Payment", "tesSUCCESS", txIDs[2], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice does not exist; Bob receives XRP
            BEAST_EXPECT(env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // tfAllOrNothing: account delete fails
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            for (int i = 0; i < 5; ++i)
                env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2) + env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(acctdelete(alice, bob), seq + 2),
                // terNO_ACCOUNT: alice does not exist
                batch::Inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            };
            validateClosedLedger(env, testCases);

            // Alice still exists; Bob is unchanged
            BEAST_EXPECT(env.le(keylet::account(alice)));
            BEAST_EXPECT(env.balance(bob) == preBob);
        }
    }

    void
    testLoan(FeatureBitset features)
    {
        testcase("loan");

        bool const lendingBatchEnabled = !std::ranges::any_of(
            Batch::kDisabledTxTypes,
            [](auto const& disabled) { return disabled == ttLOAN_BROKER_SET; });

        using namespace test::jtx;

        test::jtx::Env env{*this, features};

        Account const issuer{"issuer"};
        // For simplicity, lender will be the sole actor for the vault &
        // brokers.
        Account const lender{"lender"};
        // Borrower only wants to borrow
        Account const borrower{"borrower"};

        // Fund the accounts and trust lines with the same amount so that tests
        // can use the same values regardless of the asset.
        env.fund(XRP(100'000), issuer, noripple(lender, borrower));
        env.close();

        // Just use an XRP asset
        PrettyAsset const asset{xrpIssue(), 1'000'000};

        Vault const vault{env};

        auto const deposit = asset(50'000);
        auto const debtMaximumValue = asset(25'000).value();
        auto const coverDepositValue = asset(1000).value();

        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = deposit}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(lender.id(), env.seq(lender));

        {
            using namespace loanBroker;
            env(set(lender, vaultKeylet.key),
                kManagementFeeRate(TenthBips16(100)),
                kDebtMaximum(debtMaximumValue),
                kCoverRateMinimum(TenthBips32(percentageToTenthBips(10))),
                kCoverRateLiquidation(TenthBips32(percentageToTenthBips(25))));

            env(coverDeposit(lender, brokerKeylet.key, coverDepositValue));

            env.close();
        }

        {
            using namespace loan;
            using namespace std::chrono_literals;

            auto const lenderSeq = env.seq(lender);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            auto const loanKeylet = keylet::loan(brokerKeylet.key, 1);
            {
                auto const [txIDs, batchID] = submitBatch(
                    env,
                    lendingBatchEnabled ? temBAD_SIGNATURE : temINVALID_INNER_BATCH,
                    batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                    batch::Inner(
                        env.json(
                            set(lender, brokerKeylet.key, asset(1000).value()),
                            // Not allowed to include the counterparty signature
                            Sig(sfCounterpartySignature, borrower),
                            Sig(kNone),
                            Fee(kNone),
                            Seq(kNone)),
                        lenderSeq + 1),
                    batch::Inner(
                        pay(lender, loanKeylet.key, STAmount{asset, asset(500).value()}),
                        lenderSeq + 2));
            }
            {
                auto const [txIDs, batchID] = submitBatch(
                    env,
                    temINVALID_INNER_BATCH,
                    batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                    batch::Inner(
                        env.json(
                            set(lender, brokerKeylet.key, asset(1000).value()),
                            // Counterparty must be set
                            Sig(kNone),
                            Fee(kNone),
                            Seq(kNone)),
                        lenderSeq + 1),
                    batch::Inner(
                        pay(lender, loanKeylet.key, STAmount{asset, asset(500).value()}),
                        lenderSeq + 2));
            }
            {
                auto const [txIDs, batchID] = submitBatch(
                    env,
                    lendingBatchEnabled ? temBAD_SIGNER : temINVALID_INNER_BATCH,
                    batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                    batch::Inner(
                        env.json(
                            set(lender, brokerKeylet.key, asset(1000).value()),
                            // Counterparty must sign the outer transaction
                            kCounterparty(borrower.id()),
                            Sig(kNone),
                            Fee(kNone),
                            Seq(kNone)),
                        lenderSeq + 1),
                    batch::Inner(
                        pay(lender, loanKeylet.key, STAmount{asset, asset(500).value()}),
                        lenderSeq + 2));
            }
            {
                // LoanSet normally charges at least 2x base fee, but since the
                // signature check is done by the batch, it only charges the
                // base fee.
                auto const batchFee = batch::calcBatchFee(env, 1, 2);
                auto const [txIDs, batchID] = submitBatch(
                    env,
                    lendingBatchEnabled ? TER(tesSUCCESS) : TER(temINVALID_INNER_BATCH),
                    batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                    batch::Inner(
                        env.json(
                            set(lender, brokerKeylet.key, asset(1000).value()),
                            kCounterparty(borrower.id()),
                            Sig(kNone),
                            Fee(kNone),
                            Seq(kNone)),
                        lenderSeq + 1),
                    batch::Inner(
                        pay(
                            // However, this inner transaction will fail,
                            // because the lender is not allowed to draw the
                            // transaction
                            lender,
                            loanKeylet.key,
                            STAmount{asset, asset(500).value()}),
                        lenderSeq + 2),
                    batch::Sig(borrower));
            }
            env.close();
            BEAST_EXPECT(env.le(brokerKeylet));
            BEAST_EXPECT(!env.le(loanKeylet));
            {
                // LoanSet normally charges at least 2x base fee, but since the
                // signature check is done by the batch, it only charges the
                // base fee.
                auto const lenderSeq = env.seq(lender);
                auto const batchFee = batch::calcBatchFee(env, 1, 2);
                auto const [txIDs, batchID] = submitBatch(
                    env,
                    lendingBatchEnabled ? TER(tesSUCCESS) : TER(temINVALID_INNER_BATCH),
                    batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                    batch::Inner(
                        env.json(
                            set(lender, brokerKeylet.key, asset(1000).value()),
                            kCounterparty(borrower.id()),
                            Sig(kNone),
                            Fee(kNone),
                            Seq(kNone)),
                        lenderSeq + 1),
                    batch::Inner(manage(lender, loanKeylet.key, tfLoanImpair), lenderSeq + 2),
                    batch::Sig(borrower));
            }
            env.close();
            BEAST_EXPECT(env.le(brokerKeylet));
            if (auto const sleLoan = env.le(loanKeylet);
                lendingBatchEnabled ? BEAST_EXPECT(sleLoan) : !BEAST_EXPECT(!sleLoan))
            {
                BEAST_EXPECT(sleLoan->isFlag(lsfLoanImpaired));
            }
        }
    }

    void
    testObjectCreateSequence(FeatureBitset features)
    {
        testcase("object create w/ sequence");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(100)));
        env.close();

        // success
        {
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(check::create(bob, alice, usd(10)), bobSeq),
                batch::Inner(check::cash(alice, chkID, usd(10)), aliceSeq + 1),
                batch::Sig(bob));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "CheckCreate", "tesSUCCESS", txIDs[0], batchID},
                {2, "CheckCash", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(bob) == bobSeq + 1);

            // Alice pays Fee; Bob XRP Unchanged
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);

            // Alice pays USD & Bob receives USD
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD + usd(10));
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD - usd(10));
        }

        // failure
        {
            env(fset(alice, asfRequireDest));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preAliceUSD = env.balance(alice, usd.issue());
            auto const preBobUSD = env.balance(bob, usd.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfIndependent),
                // tecDST_TAG_NEEDED - alice has enabled asfRequireDest
                batch::Inner(check::create(bob, alice, usd(10)), bobSeq),
                batch::Inner(check::cash(alice, chkID, usd(10)), aliceSeq + 1),
                batch::Sig(bob));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "CheckCreate", "tecDST_TAG_NEEDED", txIDs[0], batchID},
                {2, "CheckCash", "tecNO_ENTRY", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

            // Bob consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(bob) == bobSeq + 1);

            // Alice pays Fee; Bob XRP Unchanged
            BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob);

            // Alice pays USD & Bob receives USD
            BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD);
        }
    }

    void
    testObjectCreateTicket(FeatureBitset features)
    {
        testcase("object create w/ ticket");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(100)));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preAliceUSD = env.balance(alice, usd.issue());
        auto const preBobUSD = env.balance(bob, usd.issue());

        auto const batchFee = batch::calcBatchFee(env, 1, 3);
        uint256 const chkID{getCheckIndex(bob, bobSeq + 1)};
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::Inner(ticket::create(bob, 10), bobSeq),
            batch::Inner(check::create(bob, alice, usd(10)), 0, bobSeq + 1),
            batch::Inner(check::cash(alice, chkID, usd(10)), aliceSeq + 1),
            batch::Sig(bob));
        env.close();

        std::vector<TestLedgerData> const testCases = {
            {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {1, "TicketCreate", "tesSUCCESS", txIDs[0], batchID},
            {2, "CheckCreate", "tesSUCCESS", txIDs[1], batchID},
            {3, "CheckCash", "tesSUCCESS", txIDs[2], batchID},
        };
        validateClosedLedger(env, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 10 + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice - batchFee);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD + usd(10));
        BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD - usd(10));
    }

    void
    testObjectCreate3rdParty(FeatureBitset features)
    {
        testcase("object create w/ 3rd party");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();

        env.trust(usd(1000), alice, bob);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(100)));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const bobSeq = env.seq(bob);
        auto const carolSeq = env.seq(carol);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const preCarol = env.balance(carol);
        auto const preAliceUSD = env.balance(alice, usd.issue());
        auto const preBobUSD = env.balance(bob, usd.issue());

        auto const batchFee = batch::calcBatchFee(env, 2, 2);
        uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
            batch::Inner(check::create(bob, alice, usd(10)), bobSeq),
            batch::Inner(check::cash(alice, chkID, usd(10)), aliceSeq),
            batch::Sig(alice, bob));
        env.close();

        std::vector<TestLedgerData> const testCases = {
            {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {1, "CheckCreate", "tesSUCCESS", txIDs[0], batchID},
            {2, "CheckCash", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        BEAST_EXPECT(env.seq(carol) == carolSeq + 1);
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(bob) == preBob);
        BEAST_EXPECT(env.balance(carol) == preCarol - batchFee);
        BEAST_EXPECT(env.balance(alice, usd.issue()) == preAliceUSD + usd(10));
        BEAST_EXPECT(env.balance(bob, usd.issue()) == preBobUSD - usd(10));
    }

    void
    testTickets(FeatureBitset features)
    {
        {
            testcase("tickets outer");

            using namespace test::jtx;
            using namespace std::literals;

            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");

            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq + 0),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                ticket::Use(aliceTicketSeq));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            auto const sle = env.le(keylet::account(alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 9);
            BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 9);

            BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        {
            testcase("tickets inner");

            using namespace test::jtx;
            using namespace std::literals;

            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");

            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq),
                batch::Inner(pay(alice, bob, XRP(2)), 0, aliceTicketSeq + 1));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            auto const sle = env.le(keylet::account(alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
            BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        {
            testcase("tickets outer inner");

            using namespace test::jtx;
            using namespace std::literals;

            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");

            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::Use(aliceTicketSeq));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            auto const sle = env.le(keylet::account(alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == 8);
            BEAST_EXPECT(sle->getFieldU32(sfTicketCount) == 8);

            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }
    }

    void
    testSequenceOpenLedger(FeatureBitset features)
    {
        testcase("sequence open ledger");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        // Before Batch Txn w/ retry following ledger
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction. Because the
            // terPRE_SEQ is outside of the batch this noop transaction will ge
            // reapplied in the following ledger
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const carolSeq = env.seq(carol);

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), Seq(aliceSeq + 2));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, Ter(terPRE_SEQ));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::Sig(alice));
            env.close();

            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger contains noop txn
                std::vector<TestLedgerData> const testCases = {
                    {0, "AccountSet", "tesSUCCESS", noopTxnID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }
        }

        // Before Batch Txn w/ same sequence
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), Seq(aliceSeq + 1));
            env(noopTxn, Ter(terPRE_SEQ));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 2));
            env.close();

            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // After Batch Txn w/ same sequence
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 2));

            auto const noopTxn = env.jt(noop(alice), Seq(aliceSeq + 1));
            env(noopTxn, Ter(tesSUCCESS));
            env.close();

            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // Outer Batch terPRE_SEQ
        {
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const carolSeq = env.seq(carol);

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                terPRE_SEQ,
                batch::outer(carol, carolSeq + 1, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::Sig(alice));

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(carol), Seq(carolSeq));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, Ter(tesSUCCESS));
            env.close();

            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "AccountSet", "tesSUCCESS", noopTxnID, std::nullopt},
                    {1, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {2, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {3, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger contains no transactions
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
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
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), ticket::Use(aliceTicketSeq + 1));
            env(noopTxn, Ter(tesSUCCESS));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::Use(aliceTicketSeq));
            env.close();

            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // After Batch Txn w/ same ticket
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::Use(aliceTicketSeq));

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), ticket::Use(aliceTicketSeq + 1));
            env(noopTxn);

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
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

        // Consume Object Before Batch Txn
        {
            // IMPORTANT: The initial result of `CheckCash` is tecNO_ENTRY
            // because the create transaction has not been applied because the
            // batch will run in the close ledger process. The batch will be
            // allied and then retry this transaction in the current ledger.

            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // CheckCash Txn
            uint256 const chkID{getCheckIndex(alice, aliceSeq)};
            auto const objTxn = env.jt(check::cash(bob, chkID, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, Ter(tecNO_ENTRY));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::Use(aliceTicketSeq));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "CheckCreate", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                    {3, "CheckCash", "tesSUCCESS", objTxnID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> const testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // Create Object Before Batch Txn
        {
            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);

            // CheckCreate Txn
            uint256 const chkID{getCheckIndex(alice, aliceSeq)};
            auto const objTxn = env.jt(check::create(alice, bob, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, Ter(tesSUCCESS));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(check::cash(bob, chkID, XRP(10)), bobSeq),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::Use(aliceTicketSeq),
                batch::Sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "CheckCreate", "tesSUCCESS", objTxnID, std::nullopt},
                    {1, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {2, "CheckCash", "tesSUCCESS", txIDs[0], batchID},
                    {3, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }
        }

        // After Batch Txn
        {
            // IMPORTANT: The initial result of `CheckCash` is tecNO_ENTRY
            // because the create transaction has not been applied because the
            // batch will run in the close ledger process. The batch will be
            // applied and then retry this transaction in the current ledger.

            test::jtx::Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            uint256 const chkID{getCheckIndex(alice, aliceSeq)};
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::Inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::Inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::Use(aliceTicketSeq));

            // CheckCash Txn
            auto const objTxn = env.jt(check::cash(bob, chkID, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, Ter(tecNO_ENTRY));

            env.close();
            {
                std::vector<TestLedgerData> const testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "CheckCreate", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                    {3, "CheckCash", "tesSUCCESS", objTxnID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }
        }
    }

    void
    testPseudoTxn(FeatureBitset features)
    {
        testcase("pseudo txn with tfInnerBatchTxn");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
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
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const result = xrpl::apply(env.app(), view, stx, TapNone, j);
            BEAST_EXPECT(!result.applied && result.ter == temINVALID_FLAG);
            return result.applied;
        });
    }

    void
    testOpenLedger(FeatureBitset features)
    {
        testcase("batch open ledger");
        // IMPORTANT: When a transaction is submitted outside of a batch and
        // another transaction is part of the batch, the batch might fail
        // because the sequence is out of order. This is because the canonical
        // order of transactions is determined by the account first. So in this
        // case, alice's batch comes after bob's self submitted transaction even
        // though the payment was submitted after the batch.

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, features};
        XRPAmount const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);
        env.close();

        env(noop(bob), Ter(tesSUCCESS));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const bobSeq = env.seq(bob);

        // Alice Pays Bob (Open Ledger)
        auto const payTxn1 = env.jt(pay(alice, bob, XRP(10)), Seq(aliceSeq));
        auto const payTxn1ID = to_string(payTxn1.stx->getTransactionID());
        env(payTxn1, Ter(tesSUCCESS));

        // Alice & Bob Atomic Batch
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, aliceSeq + 1, batchFee, tfAllOrNothing),
            batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 2),
            batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
            batch::Sig(bob));

        // Bob pays Alice (Open Ledger)
        auto const payTxn2 = env.jt(pay(bob, alice, XRP(5)), Seq(bobSeq + 1));
        auto const payTxn2ID = to_string(payTxn2.stx->getTransactionID());
        env(payTxn2, Ter(terPRE_SEQ));
        env.close();

        std::vector<TestLedgerData> const testCases = {
            {0, "Payment", "tesSUCCESS", payTxn1ID, std::nullopt},
            {1, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {2, "Payment", "tesSUCCESS", txIDs[0], batchID},
            {3, "Payment", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

        env.close();
        {
            // next ledger includes the payment txn
            std::vector<TestLedgerData> const testCases = {
                {0, "Payment", "tesSUCCESS", payTxn2ID, std::nullopt},
            };
            validateClosedLedger(env, testCases);
        }

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 3);

        // Alice consumes sequences (# of txns)
        BEAST_EXPECT(env.seq(bob) == bobSeq + 2);

        // Alice pays XRP & Fee; Bob receives XRP & pays Fee
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10) - batchFee - baseFee);
        BEAST_EXPECT(env.balance(bob) == preBob + XRP(10) - baseFee);
    }

    void
    testBatchTxQueue(FeatureBitset features)
    {
        testcase("batch tx queue");

        using namespace test::jtx;
        using namespace std::literals;

        // only outer batch transactions are counter towards the queue size
        {
            test::jtx::Env env{
                *this,
                makeSmallQueueConfig({{"minimum_txn_in_ledger_standalone", "2"}}),
                features,
                nullptr,
                beast::Severity::Error};

            auto alice = Account("alice");
            auto bob = Account("bob");
            auto carol = Account("carol");

            // Fund across several ledgers so the TxQ metrics stay restricted.
            env.fund(XRP(10000), noripple(alice, bob));
            env.close(env.now() + 5s, 10000ms);
            env.fund(XRP(10000), noripple(carol));
            env.close(env.now() + 5s, 10000ms);

            // Fill the ledger
            env(noop(alice));
            env(noop(alice));
            env(noop(alice));
            checkMetrics(*this, env, 0, std::nullopt, 3, 2);

            env(noop(carol), Ter(terQUEUED));
            checkMetrics(*this, env, 1, std::nullopt, 3, 2);

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            // Queue Batch
            {
                env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                    batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                    batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                    batch::Sig(bob),
                    Ter(terQUEUED));
            }

            checkMetrics(*this, env, 2, std::nullopt, 3, 2);

            // Replace Queued Batch
            {
                env(batch::outer(alice, aliceSeq, openLedgerFee(env, batchFee), tfAllOrNothing),
                    batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                    batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                    batch::Sig(bob),
                    Ter(tesSUCCESS));
                env.close();
            }

            checkMetrics(*this, env, 0, 12, 1, 6);
        }

        // inner batch transactions are counter towards the ledger tx count
        {
            test::jtx::Env env{
                *this,
                makeSmallQueueConfig({{"minimum_txn_in_ledger_standalone", "2"}}),
                features,
                nullptr,
                beast::Severity::Error};

            auto alice = Account("alice");
            auto bob = Account("bob");
            auto carol = Account("carol");

            // Fund across several ledgers so the TxQ metrics stay restricted.
            env.fund(XRP(10000), noripple(alice, bob));
            env.close(env.now() + 5s, 10000ms);
            env.fund(XRP(10000), noripple(carol));
            env.close(env.now() + 5s, 10000ms);

            // Fill the ledger leaving room for 1 queued transaction
            env(noop(alice));
            env(noop(alice));
            checkMetrics(*this, env, 0, std::nullopt, 2, 2);

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            // Batch Successful
            {
                env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                    batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                    batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                    batch::Sig(bob),
                    Ter(tesSUCCESS));
            }

            checkMetrics(*this, env, 0, std::nullopt, 3, 2);

            env(noop(carol), Ter(terQUEUED));
            checkMetrics(*this, env, 1, std::nullopt, 3, 2);
        }
    }

    void
    testBatchNetworkOps(FeatureBitset features)
    {
        testcase("batch network ops");

        using namespace test::jtx;
        using namespace std::literals;

        Env env(*this, envconfig(), features, nullptr, beast::Severity::Disabled);

        auto alice = Account("alice");
        auto bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto submitTx = [&](std::uint32_t flags) -> uint256 {
            auto jt = env.jt(pay(alice, bob, XRP(1)), Txflags(flags));
            Serializer s;
            jt.stx->add(s);
            env.app().getOPs().submitTransaction(jt.stx);
            return jt.stx->getTransactionID();
        };

        auto processTxn = [&](std::uint32_t flags) -> uint256 {
            auto jt = env.jt(pay(alice, bob, XRP(1)), Txflags(flags));
            Serializer s;
            jt.stx->add(s);
            std::string reason;
            auto transaction = std::make_shared<Transaction>(jt.stx, reason, env.app());
            env.app().getOPs().processTransaction(
                transaction, false, true, NetworkOPs::FailHard::Yes);
            return transaction->getID();
        };

        // Validate: NetworkOPs::submitTransaction()
        {
            // Submit a tx with tfInnerBatchTxn
            uint256 const txBad = submitTx(tfInnerBatchTxn);
            BEAST_EXPECT(env.app().getHashRouter().getFlags(txBad) == HashRouterFlags::UNDEFINED);
        }

        // Validate: NetworkOPs::processTransaction()
        {
            uint256 const txid = processTxn(tfInnerBatchTxn);
            // HashRouter::getFlags() should return LedgerFlags::BAD
            BEAST_EXPECT(env.app().getHashRouter().getFlags(txid) == HashRouterFlags::BAD);
        }
    }

    void
    testBatchDelegate(FeatureBitset features)
    {
        testcase("batch delegate");

        using namespace test::jtx;
        using namespace std::literals;

        // delegated non atomic inner
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);

            auto tx = batch::Inner(pay(alice, bob, XRP(1)), seq + 1);
            tx[jss::Delegate] = bob.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx,
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(3) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(3));
        }

        // delegated atomic inner
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account("gw");
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();

            env(delegate::set(bob, carol, {"Payment"}));
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preCarol = env.balance(carol);

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);

            auto tx = batch::Inner(pay(bob, alice, XRP(1)), bobSeq);
            tx[jss::Delegate] = carol.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                tx,
                batch::Inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::Sig(bob));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);
            BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(1));
            // NOTE: Carol would normally pay the fee for delegated txns, but
            // because the batch is atomic, the fee is paid by the batch
            BEAST_EXPECT(env.balance(carol) == preCarol);
        }

        // delegated non atomic inner (AccountSet)
        // this also makes sure tfInnerBatchTxn won't block delegated AccountSet
        // with granular permission
        {
            test::jtx::Env env{*this, features};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.close();

            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);

            auto tx = batch::Inner(noop(alice), seq + 1);
            std::string const domain = "example.com";
            tx[sfDomain.jsonName] = strHex(domain);
            tx[jss::Delegate] = bob.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx,
                batch::Inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "AccountSet", "tesSUCCESS", txIDs[0], batchID},
                {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);

            // Alice consumes sequences (# of txns)
            BEAST_EXPECT(env.seq(alice) == seq + 3);

            // Alice pays XRP & Fee; Bob receives XRP
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(2) - batchFee);
            BEAST_EXPECT(env.balance(bob) == preBob + XRP(2));
        }

        // delegated non atomic inner (MPTokenIssuanceSet)
        // this also makes sure tfInnerBatchTxn won't block delegated
        // MPTokenIssuanceSet with granular permission
        {
            test::jtx::Env env{*this, features};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            auto const mptID = makeMptID(env.seq(alice), alice);
            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"}));
            env.close();

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            json::Value jv1;
            jv1[sfTransactionType] = jss::MPTokenIssuanceSet;
            jv1[sfAccount] = alice.human();
            jv1[sfDelegate] = bob.human();
            jv1[sfSequence] = seq + 1;
            jv1[sfMPTokenIssuanceID] = to_string(mptID);
            jv1[sfFlags] = tfMPTLock;

            json::Value jv2;
            jv2[sfTransactionType] = jss::MPTokenIssuanceSet;
            jv2[sfAccount] = alice.human();
            jv2[sfDelegate] = bob.human();
            jv2[sfSequence] = seq + 2;
            jv2[sfMPTokenIssuanceID] = to_string(mptID);
            jv2[sfFlags] = tfMPTUnlock;

            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, seq + 1),
                batch::Inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "MPTokenIssuanceSet", "tesSUCCESS", txIDs[0], batchID},
                {2, "MPTokenIssuanceSet", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);
        }

        // delegated non atomic inner (TrustSet)
        // this also makes sure tfInnerBatchTxn won't block delegated TrustSet
        // with granular permission
        {
            test::jtx::Env env{*this, features};
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();

            env(delegate::set(gw, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
            env.close();

            auto const seq = env.seq(gw);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jv1 = trust(gw, gw["USD"](0), alice, tfSetfAuth);
            jv1[sfDelegate] = bob.human();
            auto jv2 = trust(gw, gw["USD"](0), alice, tfSetFreeze);
            jv2[sfDelegate] = bob.human();

            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(gw, seq, batchFee, tfAllOrNothing),
                batch::Inner(jv1, seq + 1),
                batch::Inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "TrustSet", "tesSUCCESS", txIDs[0], batchID},
                {2, "TrustSet", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);
        }

        // inner transaction not authorized by the delegating account.
        {
            test::jtx::Env env{*this, features};
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();

            env(delegate::set(gw, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
            env.close();

            auto const seq = env.seq(gw);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jv1 = trust(gw, gw["USD"](0), alice, tfSetFreeze);
            jv1[sfDelegate] = bob.human();
            auto jv2 = trust(gw, gw["USD"](0), alice, tfClearFreeze);
            jv2[sfDelegate] = bob.human();

            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(gw, seq, batchFee, tfIndependent),
                batch::Inner(jv1, seq + 1),
                // terNO_DELEGATE_PERMISSION: not authorized to clear freeze
                batch::Inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> const testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "TrustSet", "tesSUCCESS", txIDs[0], batchID},
                // jv2 fails with terNO_DELEGATE_PERMISSION.
            };
            validateClosedLedger(env, testCases);

            // verify jv2 is not present in the closed ledger.
            BEAST_EXPECT(env.rpc("tx", txIDs[1])[jss::result][jss::error] == "txnNotFound");
        }
    }

    void
    testValidateRPCResponse(FeatureBitset features)
    {
        // Verifying that the RPC response from submit includes
        // the account_sequence_available, account_sequence_next,
        // open_ledger_cost and validated_ledger_index fields.
        testcase("Validate RPC response");

        using namespace jtx;
        Env env(*this, features);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // tes
        {
            auto const baseFee = env.current()->fees().base;
            auto const aliceSeq = env.seq(alice);
            auto jtx = env.jt(pay(alice, bob, XRP(1)));

            Serializer s;
            jtx.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()))[jss::result];
            env.close();

            BEAST_EXPECT(jr.isMember(jss::account_sequence_available));
            BEAST_EXPECT(jr[jss::account_sequence_available].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::account_sequence_next));
            BEAST_EXPECT(jr[jss::account_sequence_next].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(jr[jss::open_ledger_cost] == to_string(baseFee));
            BEAST_EXPECT(jr.isMember(jss::validated_ledger_index));
        }

        // tec failure
        {
            auto const baseFee = env.current()->fees().base;
            auto const aliceSeq = env.seq(alice);
            env(fset(bob, asfRequireDest));
            auto jtx = env.jt(pay(alice, bob, XRP(1)), Seq(aliceSeq));

            Serializer s;
            jtx.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()))[jss::result];
            env.close();

            BEAST_EXPECT(jr.isMember(jss::account_sequence_available));
            BEAST_EXPECT(jr[jss::account_sequence_available].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::account_sequence_next));
            BEAST_EXPECT(jr[jss::account_sequence_next].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(jr[jss::open_ledger_cost] == to_string(baseFee));
            BEAST_EXPECT(jr.isMember(jss::validated_ledger_index));
        }

        // tem failure
        {
            auto const baseFee = env.current()->fees().base;
            auto const aliceSeq = env.seq(alice);
            auto jtx = env.jt(pay(alice, bob, XRP(1)), Seq(aliceSeq + 1));

            Serializer s;
            jtx.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()))[jss::result];
            env.close();

            BEAST_EXPECT(jr.isMember(jss::account_sequence_available));
            BEAST_EXPECT(jr[jss::account_sequence_available].asUInt() == aliceSeq);
            BEAST_EXPECT(jr.isMember(jss::account_sequence_next));
            BEAST_EXPECT(jr[jss::account_sequence_next].asUInt() == aliceSeq);
            BEAST_EXPECT(jr.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(jr[jss::open_ledger_cost] == to_string(baseFee));
            BEAST_EXPECT(jr.isMember(jss::validated_ledger_index));
        }
    }

    void
    testBatchCalculateBaseFee(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        auto getBaseFee = [&](JTx const& jtx) -> XRPAmount {
            Serializer s;
            jtx.stx->add(s);
            return Batch::calculateBaseFee(*env.current(), *jtx.stx);
        };

        // bad: Inner Batch transaction found
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2));
            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(kInitialXrp));
        }

        // bad: Raw Transactions array exceeds max entries.
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 3),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 4),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 5),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 6),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 7),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 8),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 9));

            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(kInitialXrp));
        }

        // bad: Signers array exceeds max entries.
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(5)), seq + 2),
                batch::Sig(bob, carol, alice, bob, carol, alice, bob, carol, alice, alice));
            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(kInitialXrp));
        }

        // good:
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(bob, alice, XRP(2)), seq + 2));
            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == batchFee);
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
        testLoan(features);
        testObjectCreateSequence(features);
        testObjectCreateTicket(features);
        testObjectCreate3rdParty(features);
        testTickets(features);
        testSequenceOpenLedger(features);
        testTicketsOpenLedger(features);
        testObjectsOpenLedger(features);
        testPseudoTxn(features);
        testOpenLedger(features);
        testBatchTxQueue(features);
        testBatchNetworkOps(features);
        testBatchDelegate(features);
        testValidateRPCResponse(features);
        testBatchCalculateBaseFee(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;

        auto const sa = testableAmendments();
        testWithFeats(sa - fixBatchInnerSigs);
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Batch, app, xrpl);

}  // namespace xrpl::test
