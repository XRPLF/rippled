#include <test/jtx.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/utility.h>

#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/Batch.h>

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
        std::optional<std::string> batchID;
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
        std::string const& batchID,
        TestLedgerData const& ledgerResult)
    {
        Json::Value const jrr = env.rpc("tx", ledgerResult.txHash)[jss::result];
        BEAST_EXPECT(jrr[sfTransactionType.jsonName] == ledgerResult.txType);
        BEAST_EXPECT(
            jrr[jss::meta][sfTransactionResult.jsonName] ==
            ledgerResult.result);
        BEAST_EXPECT(jrr[jss::meta][sfParentBatchID.jsonName] == batchID);
    }

    void
    validateClosedLedger(
        jtx::Env& env,
        std::vector<TestLedgerData> const& ledgerResults)
    {
        auto const jrr = getLastLedger(env);
        auto const transactions =
            jrr[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.size() == ledgerResults.size());
        for (TestLedgerData const& ledgerResult : ledgerResults)
        {
            auto const txn = getTxByIndex(jrr, ledgerResult.index);
            BEAST_EXPECT(txn[jss::hash].asString() == ledgerResult.txHash);
            BEAST_EXPECT(txn.isMember(jss::metaData));
            Json::Value const meta = txn[jss::metaData];
            BEAST_EXPECT(
                txn[sfTransactionType.jsonName] == ledgerResult.txType);
            BEAST_EXPECT(
                meta[sfTransactionResult.jsonName] == ledgerResult.result);
            if (ledgerResult.batchID)
                validateInnerTxn(env, *ledgerResult.batchID, ledgerResult);
        }
    }

    template <typename... Args>
    std::pair<std::vector<std::string>, std::string>
    submitBatch(jtx::Env& env, TER const& result, Args&&... args)
    {
        auto batchTxn = env.jt(std::forward<Args>(args)...);
        env(batchTxn, jtx::ter(result));

        auto const ids = batchTxn.stx->getBatchTransactionIDs();
        std::vector<std::string> txIDs;
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
            env.fund(XRP(10000), alice, bob, carol);
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
        env.fund(XRP(10000), alice, bob, carol);
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

        // temREDUNDANT: Batch: duplicate Txn found.
        {
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1));

            env(jt.jv, batch::sig(bob), ter(temREDUNDANT));
            env.close();
        }

        // DEFENSIVE: temINVALID: Batch: batch cannot have inner batch txn.
        // ACTUAL: telENV_RPC_FAILED: isRawTransactionOkay()
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(
                    batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(telENV_RPC_FAILED));
            env.close();
        }

        // temINVALID_FLAG: Batch: inner txn must have the
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

            env(jt.jv, batch::sig(bob), ter(temINVALID_FLAG));
            env.close();
        }

        // temBAD_SIGNATURE: Batch: inner txn cannot include TxnSignature.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jt = env.jt(pay(alice, bob, XRP(1)));
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(jt.jv, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                ter(temBAD_SIGNATURE));
            env.close();
        }

        // temBAD_SIGNER: Batch: inner txn cannot include Signers.
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
                ter(temBAD_SIGNER));
            env.close();
        }

        // temBAD_REGKEY: Batch: inner txn must include empty
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

            env(jt.jv, ter(temBAD_REGKEY));
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

        // temBAD_FEE: Batch: inner txn must have a fee of 0.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::inner(pay(alice, bob, XRP(1)), seq + 1);
            tx1[jss::Fee] = to_string(env.current()->fees().base);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temBAD_FEE));
            env.close();
        }

        // temSEQ_AND_TICKET: Batch: inner txn cannot have both Sequence
        // and TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto tx1 = batch::inner(pay(alice, bob, XRP(1)), 0, 1);
            tx1[jss::Sequence] = seq + 1;
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx1,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temSEQ_AND_TICKET));
            env.close();
        }

        // temSEQ_AND_TICKET: Batch: inner txn must have either Sequence or
        // TicketSequence.
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                ter(temSEQ_AND_TICKET));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 1),
                ter(temREDUNDANT));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate ticket found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), 0, seq + 1),
                ter(temREDUNDANT));
            env.close();
        }

        // temREDUNDANT: Batch: duplicate ticket & sequence found:
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 1),
                ter(temREDUNDANT));
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

        // temREDUNDANT: Batch: duplicate signer found
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 2, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(bob, alice, XRP(5)), env.seq(bob)),
                batch::sig(bob, bob),
                ter(temREDUNDANT));
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
        // checkSign.checkSingleSign

        // tefBAD_AUTH: Bob is not authorized to sign for Alice
        {
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 3, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), seq + 1),
                batch::inner(pay(alice, bob, XRP(20)), seq + 2),
                sig(bob),
                ter(tefBAD_AUTH));
            env.close();
        }

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

        // tefBAD_AUTH: Inner Account is not signer
        {
            auto const ledSeq = env.current()->seq();
            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, phantom, XRP(1000)), seq + 1),
                batch::inner(noop(phantom), ledSeq),
                batch::sig(Reg{phantom, carol}),
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

        env.fund(XRP(10000), alice, bob);

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

        env.fund(XRP(10000), alice, bob, gw);
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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq - 10),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 10),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq - 10),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq + 1),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq + 10),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }

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
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, preAliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(10)), preAliceSeq),
                batch::inner(pay(bob, alice, XRP(5)), preBobSeq),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }

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
            env.fund(XRP(10000), alice, bob);
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
            env.fund(XRP(10000), alice, bob, carol);
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
            env.fund(XRP(10000), alice, bob, carol);
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
            env.fund(XRP(10000), alice, bob, carol);
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
            env.fund(XRP(10000), alice, bob);
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

            env.fund(XRP(10000), alice, bob, gw);
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
            env.fund(XRP(10000), alice, bob);
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
            env.fund(XRP(10000), alice, bob);
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
            env.fund(XRP(10000), alice, bob);
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
            env.fund(XRP(10000), alice, bob);
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // terPRE_TICKET: ticket does not exist
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
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
                batch::inner(pay(alice, bob, XRP(9999)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(9999)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            env.close();

            std::vector<TestLedgerData> testCases = {
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

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
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
                batch::inner(pay(alice, bob, XRP(9999)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3),
                batch::inner(pay(alice, bob, XRP(4)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // terPRE_TICKET: ticket does not exist
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            env.close();

            std::vector<TestLedgerData> testCases = {
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

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tecUNFUNDED_PAYMENT: alice does not have enough XRP
                batch::inner(pay(alice, bob, XRP(9999)), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 4));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // tefNO_AUTH_REQUIRED: trustline auth is not required
                batch::inner(trust(alice, USD(1000), tfSetfAuth), seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                // terPRE_TICKET: ticket does not exist
                batch::inner(trust(alice, USD(1000), tfSetfAuth), 0, seq + 3),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
                batch::inner(pay(alice, bob, XRP(100)), seq + 1),
                batch::inner(pay(alice, carol, XRP(100)), seq + 2),
                batch::inner(
                    offer(
                        alice,
                        alice["USD"](100),
                        XRP(100),
                        tfImmediateOrCancel),
                    seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
    testInnerSubmitRPC(FeatureBitset features)
    {
        testcase("inner submit rpc");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);
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
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, seq, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(1000)), seq + 1),
            batch::inner(fset(bob, asfAllowTrustLineClawback), ledSeq),
            batch::sig(bob));
        env.close();

        std::vector<TestLedgerData> testCases = {
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

        test::jtx::Env env{*this, envconfig()};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);

        auto const seq = env.seq(alice);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        auto tx1 = batch::inner(noop(alice), seq + 1);
        std::string domain = "example.com";
        tx1[sfDomain] = strHex(domain);
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, seq, batchFee, tfAllOrNothing),
            tx1,
            batch::inner(pay(alice, bob, XRP(1)), seq + 2));
        env.close();

        std::vector<TestLedgerData> testCases = {
            {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {1, "AccountSet", "tesSUCCESS", txIDs[0], batchID},
            {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

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
            env.fund(XRP(10000), alice, bob);
            env.close();

            incLgrSeqForAccDel(env, alice);
            for (int i = 0; i < 5; ++i)
                env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2) +
                env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(acctdelete(alice, bob), seq + 2),
                // terNO_ACCOUNT: alice does not exist
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};

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
            auto const batchFee = batch::calcBatchFee(env, 0, 2) +
                env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfIndependent),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                // tecHAS_OBLIGATIONS: alice has obligations
                batch::inner(acctdelete(alice, bob), seq + 2),
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};

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
            auto const batchFee = batch::calcBatchFee(env, 0, 2) +
                env.current()->fees().increment;
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(acctdelete(alice, bob), seq + 2),
                // terNO_ACCOUNT: alice does not exist
                batch::inner(pay(alice, bob, XRP(2)), seq + 3));
            env.close();

            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
            };
            validateClosedLedger(env, testCases);

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

        env.fund(XRP(10000), alice, bob, gw);
        env.close();

        env.trust(USD(1000), alice, bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        // success
        {
            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(check::create(bob, alice, USD(10)), bobSeq),
                batch::inner(check::cash(alice, chkID, USD(10)), aliceSeq + 1),
                batch::sig(bob));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
        }

        // failure
        {
            env(fset(alice, asfRequireDest));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            auto const preAliceUSD = env.balance(alice, USD.issue());
            auto const preBobUSD = env.balance(bob, USD.issue());

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfIndependent),
                // tecDST_TAG_NEEDED - alice has enabled asfRequireDest
                batch::inner(check::create(bob, alice, USD(10)), bobSeq),
                batch::inner(check::cash(alice, chkID, USD(10)), aliceSeq + 1),
                batch::sig(bob));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD);
        }
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

        env.fund(XRP(10000), alice, bob, gw);
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

        auto const batchFee = batch::calcBatchFee(env, 1, 3);
        uint256 const chkID{getCheckIndex(bob, bobSeq + 1)};
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
            batch::inner(ticket::create(bob, 10), bobSeq),
            batch::inner(check::create(bob, alice, USD(10)), 0, bobSeq + 1),
            batch::inner(check::cash(alice, chkID, USD(10)), aliceSeq + 1),
            batch::sig(bob));
        env.close();

        std::vector<TestLedgerData> testCases = {
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

        env.fund(XRP(10000), alice, bob, carol, gw);
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
        uint256 const chkID{getCheckIndex(bob, env.seq(bob))};
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
            batch::inner(check::create(bob, alice, USD(10)), bobSeq),
            batch::inner(check::cash(alice, chkID, USD(10)), aliceSeq),
            batch::sig(alice, bob));
        env.close();

        std::vector<TestLedgerData> testCases = {
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
        BEAST_EXPECT(env.balance(alice, USD.issue()) == preAliceUSD + USD(10));
        BEAST_EXPECT(env.balance(bob, USD.issue()) == preBobUSD - USD(10));
    }

    void
    testTickets(FeatureBitset features)
    {
        {
            testcase("tickets outer");

            using namespace test::jtx;
            using namespace std::literals;

            test::jtx::Env env{*this, envconfig()};

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
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 0),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                ticket::use(aliceTicketSeq));
            env.close();

            std::vector<TestLedgerData> testCases = {
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

            test::jtx::Env env{*this, envconfig()};

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
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq),
                batch::inner(pay(alice, bob, XRP(2)), 0, aliceTicketSeq + 1));
            env.close();

            std::vector<TestLedgerData> testCases = {
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

            test::jtx::Env env{*this, envconfig()};

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
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::use(aliceTicketSeq));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const carolSeq = env.seq(carol);

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 2));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, ter(terPRE_SEQ));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(carol, carolSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::sig(alice));
            env.close();

            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger contains noop txn
                std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 1));
            env(noopTxn, ter(terPRE_SEQ));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 2));
            env.close();

            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // After Batch Txn w/ same sequence
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 2));

            auto const noopTxn = env.jt(noop(alice), seq(aliceSeq + 1));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, ter(tesSUCCESS));
            env.close();

            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // Outer Batch terPRE_SEQ
        {
            test::jtx::Env env{*this, envconfig()};
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
                batch::inner(pay(alice, bob, XRP(1)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::sig(alice));

            // AccountSet Txn
            auto const noopTxn = env.jt(noop(carol), seq(carolSeq));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, ter(tesSUCCESS));
            env.close();

            {
                std::vector<TestLedgerData> testCases = {
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
                std::vector<TestLedgerData> testCases = {};
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
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // AccountSet Txn
            auto const noopTxn =
                env.jt(noop(alice), ticket::use(aliceTicketSeq + 1));
            auto const noopTxnID = to_string(noopTxn.stx->getTransactionID());
            env(noopTxn, ter(tesSUCCESS));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::use(aliceTicketSeq));
            env.close();

            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // After Batch Txn w/ same ticket
        {
            // IMPORTANT: The batch txn is applied first, then the noop txn.
            // Because of this ordering, the noop txn is not applied and is
            // overwritten by the payment in the batch transaction.
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq),
                ticket::use(aliceTicketSeq));

            // AccountSet Txn
            auto const noopTxn =
                env.jt(noop(alice), ticket::use(aliceTicketSeq + 1));
            env(noopTxn);

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
                    {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                    {1, "Payment", "tesSUCCESS", txIDs[0], batchID},
                    {2, "Payment", "tesSUCCESS", txIDs[1], batchID},
                };
                validateClosedLedger(env, testCases);
            }

            env.close();
            {
                // next ledger is empty
                std::vector<TestLedgerData> testCases = {};
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

            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);

            // CheckCash Txn
            uint256 const chkID{getCheckIndex(alice, aliceSeq)};
            auto const objTxn = env.jt(check::cash(bob, chkID, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, ter(tecNO_ENTRY));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::use(aliceTicketSeq));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
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
                std::vector<TestLedgerData> testCases = {};
                validateClosedLedger(env, testCases);
            }
        }

        // Create Object Before Batch Txn
        {
            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);

            // CheckCreate Txn
            uint256 const chkID{getCheckIndex(alice, aliceSeq)};
            auto const objTxn = env.jt(check::create(alice, bob, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, ter(tesSUCCESS));

            // Batch Txn
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, 0, batchFee, tfAllOrNothing),
                batch::inner(check::cash(bob, chkID, XRP(10)), bobSeq),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::use(aliceTicketSeq),
                batch::sig(bob));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
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

            test::jtx::Env env{*this, envconfig()};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
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
                batch::inner(check::create(alice, bob, XRP(10)), aliceSeq),
                batch::inner(pay(alice, bob, XRP(1)), 0, aliceTicketSeq + 1),
                ticket::use(aliceTicketSeq));

            // CheckCash Txn
            auto const objTxn = env.jt(check::cash(bob, chkID, XRP(10)));
            auto const objTxnID = to_string(objTxn.stx->getTransactionID());
            env(objTxn, ter(tecNO_ENTRY));

            env.close();
            {
                std::vector<TestLedgerData> testCases = {
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

        test::jtx::Env env{*this, envconfig()};

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
        env.app().openLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const result = ripple::apply(env.app(), view, stx, tapNONE, j);
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
        // case, alice's batch comes after bobs self submitted transaction even
        // though the payment was submitted after the batch.

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, envconfig()};
        XRPAmount const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10000), alice, bob);
        env.close();

        env(noop(bob), ter(tesSUCCESS));
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        auto const bobSeq = env.seq(bob);

        // Alice Pays Bob (Open Ledger)
        auto const payTxn1 = env.jt(pay(alice, bob, XRP(10)), seq(aliceSeq));
        auto const payTxn1ID = to_string(payTxn1.stx->getTransactionID());
        env(payTxn1, ter(tesSUCCESS));

        // Alice & Bob Atomic Batch
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        auto const [txIDs, batchID] = submitBatch(
            env,
            tesSUCCESS,
            batch::outer(alice, aliceSeq + 1, batchFee, tfAllOrNothing),
            batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 2),
            batch::inner(pay(bob, alice, XRP(5)), bobSeq),
            batch::sig(bob));

        // Bob pays Alice (Open Ledger)
        auto const payTxn2 = env.jt(pay(bob, alice, XRP(5)), seq(bobSeq + 1));
        auto const payTxn2ID = to_string(payTxn2.stx->getTransactionID());
        env(payTxn2, ter(terPRE_SEQ));
        env.close();

        std::vector<TestLedgerData> testCases = {
            {0, "Payment", "tesSUCCESS", payTxn1ID, std::nullopt},
            {1, "Batch", "tesSUCCESS", batchID, std::nullopt},
            {2, "Payment", "tesSUCCESS", txIDs[0], batchID},
            {3, "Payment", "tesSUCCESS", txIDs[1], batchID},
        };
        validateClosedLedger(env, testCases);

        env.close();
        {
            // next ledger includes the payment txn
            std::vector<TestLedgerData> testCases = {
                {0, "Payment", "tesSUCCESS", payTxn2ID, std::nullopt},
            };
            validateClosedLedger(env, testCases);
        }

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

        // only outer batch transactions are counter towards the queue size
        {
            test::jtx::Env env{
                *this,
                makeSmallQueueConfig(
                    {{"minimum_txn_in_ledger_standalone", "2"}}),
                nullptr,
                beast::severities::kError};

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

            env(noop(carol), ter(terQUEUED));
            checkMetrics(*this, env, 1, std::nullopt, 3, 2);

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            // Queue Batch
            {
                env(batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                    batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                    batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                    batch::sig(bob),
                    ter(terQUEUED));
            }

            checkMetrics(*this, env, 2, std::nullopt, 3, 2);

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

            checkMetrics(*this, env, 0, 12, 1, 6);
        }

        // inner batch transactions are counter towards the ledger tx count
        {
            test::jtx::Env env{
                *this,
                makeSmallQueueConfig(
                    {{"minimum_txn_in_ledger_standalone", "2"}}),
                nullptr,
                beast::severities::kError};

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
                    batch::inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                    batch::inner(pay(bob, alice, XRP(5)), bobSeq),
                    batch::sig(bob),
                    ter(tesSUCCESS));
            }

            checkMetrics(*this, env, 0, std::nullopt, 3, 2);

            env(noop(carol), ter(terQUEUED));
            checkMetrics(*this, env, 1, std::nullopt, 3, 2);
        }
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
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto submitTx = [&](std::uint32_t flags) -> uint256 {
            auto jt = env.jt(pay(alice, bob, XRP(1)), txflags(flags));
            Serializer s;
            jt.stx->add(s);
            env.app().getOPs().submitTransaction(jt.stx);
            return jt.stx->getTransactionID();
        };

        auto processTxn = [&](std::uint32_t flags) -> uint256 {
            auto jt = env.jt(pay(alice, bob, XRP(1)), txflags(flags));
            Serializer s;
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
            // Submit a tx with tfInnerBatchTxn
            uint256 const txBad = submitTx(tfInnerBatchTxn);
            BEAST_EXPECT(
                env.app().getHashRouter().getFlags(txBad) ==
                HashRouterFlags::UNDEFINED);
        }

        // Validate: NetworkOPs::processTransaction()
        {
            uint256 const txid = processTxn(tfInnerBatchTxn);
            // HashRouter::getFlags() should return LedgerFlags::BAD
            BEAST_EXPECT(
                env.app().getHashRouter().getFlags(txid) ==
                HashRouterFlags::BAD);
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
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);

            auto tx = batch::inner(pay(alice, bob, XRP(1)), seq + 1);
            tx[jss::Delegate] = bob.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];
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

            auto tx = batch::inner(pay(bob, alice, XRP(1)), bobSeq);
            tx[jss::Delegate] = carol.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                tx,
                batch::inner(pay(alice, bob, XRP(2)), aliceSeq + 1),
                batch::sig(bob));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.close();

            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);

            auto tx = batch::inner(noop(alice), seq + 1);
            std::string const domain = "example.com";
            tx[sfDomain.jsonName] = strHex(domain);
            tx[jss::Delegate] = bob.human();
            auto const [txIDs, batchID] = submitBatch(
                env,
                tesSUCCESS,
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                tx,
                batch::inner(pay(alice, bob, XRP(2)), seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            auto const mptID = makeMptID(env.seq(alice), alice);
            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(delegate::set(
                alice, bob, {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"}));
            env.close();

            auto const seq = env.seq(alice);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            Json::Value jv1;
            jv1[sfTransactionType] = jss::MPTokenIssuanceSet;
            jv1[sfAccount] = alice.human();
            jv1[sfDelegate] = bob.human();
            jv1[sfSequence] = seq + 1;
            jv1[sfMPTokenIssuanceID] = to_string(mptID);
            jv1[sfFlags] = tfMPTLock;

            Json::Value jv2;
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
                batch::inner(jv1, seq + 1),
                batch::inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
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
            test::jtx::Env env{*this, envconfig()};
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();

            env(delegate::set(
                gw, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
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
                batch::inner(jv1, seq + 1),
                batch::inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "TrustSet", "tesSUCCESS", txIDs[0], batchID},
                {2, "TrustSet", "tesSUCCESS", txIDs[1], batchID},
            };
            validateClosedLedger(env, testCases);
        }

        // inner transaction not authorized by the delegating account.
        {
            test::jtx::Env env{*this, envconfig()};
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();

            env(delegate::set(
                gw, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
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
                batch::inner(jv1, seq + 1),
                // terNO_DELEGATE_PERMISSION: not authorized to clear freeze
                batch::inner(jv2, seq + 2));
            env.close();

            std::vector<TestLedgerData> testCases = {
                {0, "Batch", "tesSUCCESS", batchID, std::nullopt},
                {1, "TrustSet", "tesSUCCESS", txIDs[0], batchID},
            };
            validateClosedLedger(env, testCases);
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
        Env env(*this);
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
            BEAST_EXPECT(
                jr[jss::account_sequence_available].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::account_sequence_next));
            BEAST_EXPECT(
                jr[jss::account_sequence_next].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(jr[jss::open_ledger_cost] == to_string(baseFee));
            BEAST_EXPECT(jr.isMember(jss::validated_ledger_index));
        }

        // tec failure
        {
            auto const baseFee = env.current()->fees().base;
            auto const aliceSeq = env.seq(alice);
            env(fset(bob, asfRequireDest));
            auto jtx = env.jt(pay(alice, bob, XRP(1)), seq(aliceSeq));

            Serializer s;
            jtx.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()))[jss::result];
            env.close();

            BEAST_EXPECT(jr.isMember(jss::account_sequence_available));
            BEAST_EXPECT(
                jr[jss::account_sequence_available].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::account_sequence_next));
            BEAST_EXPECT(
                jr[jss::account_sequence_next].asUInt() == aliceSeq + 1);
            BEAST_EXPECT(jr.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(jr[jss::open_ledger_cost] == to_string(baseFee));
            BEAST_EXPECT(jr.isMember(jss::validated_ledger_index));
        }

        // tem failure
        {
            auto const baseFee = env.current()->fees().base;
            auto const aliceSeq = env.seq(alice);
            auto jtx = env.jt(pay(alice, bob, XRP(1)), seq(aliceSeq + 1));

            Serializer s;
            jtx.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()))[jss::result];
            env.close();

            BEAST_EXPECT(jr.isMember(jss::account_sequence_available));
            BEAST_EXPECT(
                jr[jss::account_sequence_available].asUInt() == aliceSeq);
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
        Env env(*this);
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
                batch::inner(
                    batch::outer(alice, seq, batchFee, tfAllOrNothing), seq),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2));
            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(INITIAL_XRP));
        }

        // bad: Raw Transactions array exceeds max entries.
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(1)), seq + 2),
                batch::inner(pay(alice, bob, XRP(1)), seq + 3),
                batch::inner(pay(alice, bob, XRP(1)), seq + 4),
                batch::inner(pay(alice, bob, XRP(1)), seq + 5),
                batch::inner(pay(alice, bob, XRP(1)), seq + 6),
                batch::inner(pay(alice, bob, XRP(1)), seq + 7),
                batch::inner(pay(alice, bob, XRP(1)), seq + 8),
                batch::inner(pay(alice, bob, XRP(1)), seq + 9));

            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(INITIAL_XRP));
        }

        // bad: Signers array exceeds max entries.
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);

            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
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
                    alice));
            XRPAmount const txBaseFee = getBaseFee(jtx);
            BEAST_EXPECT(txBaseFee == XRPAmount(INITIAL_XRP));
        }

        // good:
        {
            auto const seq = env.seq(alice);
            XRPAmount const batchFee = batch::calcBatchFee(env, 0, 2);
            auto jtx = env.jt(
                batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(bob, alice, XRP(2)), seq + 2));
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
        auto const sa = testable_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Batch, app, ripple);

}  // namespace test
}  // namespace ripple
