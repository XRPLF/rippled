//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/tx/apply.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STTx.h>
#include <ripple/test/jtx.h>
#include <ripple/test/jtx/ticket.h>

namespace ripple {
namespace test {

class TxQ_test : public beast::unit_test::suite
{
    void
    checkMetrics(
        jtx::Env& env,
        std::size_t expectedCount,
        boost::optional<std::size_t> expectedMaxCount,
        std::size_t expectedInLedger,
        std::size_t expectedPerLedger,
        std::uint64_t expectedMinFeeLevel,
        std::uint64_t expectedMedFeeLevel)
    {
        auto metrics = env.app().getTxQ().getMetrics(*env.current());
        expect(metrics.referenceFeeLevel == 256, "referenceFeeLevel");
        expect(metrics.txCount == expectedCount, "txCount");
        expect(metrics.txQMaxSize == expectedMaxCount, "txQMaxSize");
        expect(metrics.txInLedger == expectedInLedger, "txInLedger");
        expect(metrics.txPerLedger == expectedPerLedger, "txPerLedger");
        expect(metrics.minFeeLevel == expectedMinFeeLevel, "minFeeLevel");
        expect(metrics.medFeeLevel == expectedMedFeeLevel, "medFeeLevel");
        auto expectedCurFeeLevel = expectedInLedger > expectedPerLedger ?
            metrics.referenceFeeLevel * expectedMedFeeLevel *
                expectedInLedger * expectedInLedger /
                    (expectedPerLedger * expectedPerLedger) :
                        metrics.referenceFeeLevel;
        expect(metrics.expFeeLevel == expectedCurFeeLevel, "expFeeLevel");
    }

    static
    std::unique_ptr<Config>
    makeConfig(std::map<std::string, std::string> extra = {})
    {
        auto p = std::make_unique<Config>();
        setupConfigForUnitTests(*p);
        auto& section = p->section("transaction_queue");
        section.set("ledgers_in_queue", "2");
        section.set("min_ledgers_to_compute_size_limit", "3");
        section.set("max_ledger_counts_to_store", "100");
        section.set("retry_sequence_percent", "25");
        for (auto const& value : extra)
        {
            section.set(value.first, value.second);
        }
        return std::move(p);
    }

public:
    void testQueue()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, makeConfig({ {"minimum_txn_in_ledger_standalone", "3"} }),
            features(featureFeeEscalation));
        auto& txq = env.app().getTxQ();

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto elmo = Account("elmo");
        auto fred = Account("fred");
        auto gwen = Account("gwen");
        auto hank = Account("hank");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 3, 256, 500);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 3, 256, 500);

        // Alice - price starts exploding: held
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 4, 3, 256, 500);

        auto openLedgerFee =
            [&]()
            {
                return fee(txq.openLedgerFee(*env.current()));
            };
        /*
        With multi-transaction support, this txn will get
        queued, which screws up the rest of the test.

        // Alice's next transaction -
        // fails because the item in the TxQ hasn't applied.
        env(noop(alice), openLedgerFee(),
            seq(env.seq(alice) + 1), ter(terPRE_SEQ));
        checkMetrics(env, 1, boost::none, 4, 3, 256, 500);
        */

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee());
        checkMetrics(env, 1, boost::none, 5, 3, 256, 500);

        // Daria with low fee: hold
        env(noop(daria), fee(1000), queued);
        checkMetrics(env, 2, boost::none, 5, 3, 256, 500);

        env.close();
        // Verify that the held transactions got applied
        auto lastMedian = 500;
        checkMetrics(env, 0, 10, 2, 5, 256, lastMedian);

        //////////////////////////////////////////////////////////////

        // Make some more accounts. We'll need them later to abuse the queue.
        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(env, 0, 10, 6, 5, 256, lastMedian);

        // Now get a bunch of transactions held.
        env(noop(alice), fee(12), queued);
        checkMetrics(env, 1, 10, 6, 5, 256, lastMedian);

        env(noop(bob), fee(10), queued); // won't clear the queue
        env(noop(charlie), fee(20), queued);
        env(noop(daria), fee(15), queued);
        env(noop(elmo), fee(11), queued);
        env(noop(fred), fee(19), queued);
        env(noop(gwen), fee(16), queued);
        env(noop(hank), fee(18), queued);
        checkMetrics(env, 8, 10, 6, 5, 256, lastMedian);

        env.close();
        // Verify that the held transactions got applied
        lastMedian = 500;
        checkMetrics(env, 1, 12, 7, 6, 256, lastMedian);

        // Bob's transaction is still stuck in the queue.

        //////////////////////////////////////////////////////////////

        // Hank sends another txn
        env(noop(hank), fee(10), queued);
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn  got held and bumps the fee,
        // but doesn't even bump it enough to requeue
        env(noop(hank), fee(11), ter(telINSUF_FEE_P));
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn got held and bumps the fee,
        // enough to requeue, but doesn't bump it enough to
        // apply to the ledger
        env(noop(hank), fee(6000), queued);
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn got held and bumps the fee,
        // high enough to get into the open ledger, because
        // he doesn't want to wait.
        env(noop(hank), openLedgerFee());
        checkMetrics(env, 1, 12, 8, 6, 256, lastMedian);

        // Hank then sends another, less important txn
        // (In addition to the metrics, this will verify that
        //  the original txn got removed.)
        env(noop(hank), fee(6000), queued);
        checkMetrics(env, 2, 12, 8, 6, 256, lastMedian);

        env.close();

        // Verify that bob and hank's txns were applied
        lastMedian = 500;
        checkMetrics(env, 0, 16, 2, 8, 256, lastMedian);

        // Close again with a simulated time leap to
        // reset the escalation limit down to minimum
        lastMedian = 76928;
        env.close(env.now() + 5s, 10000ms);
        checkMetrics(env, 0, 16, 0, 3, 256, lastMedian);
        // Then close once more without the time leap
        // to reset the queue maxsize down to minimum
        lastMedian = 500;
        env.close();
        checkMetrics(env, 0, 6, 0, 3, 256, lastMedian);

        //////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(hank));
        env(noop(gwen));
        env(noop(fred));
        env(noop(elmo));
        checkMetrics(env, 0, 6, 4, 3, 256, lastMedian);

        // Use explicit fees so we can control which txn
        // will get dropped
        // This one gets into the queue, but gets dropped when the
        // higher fee one is added later.
        env(noop(daria), fee(15), queued);
        // These stay in the queue.
        env(noop(elmo), fee(16), queued);
        env(noop(fred), fee(17), queued);
        env(noop(gwen), fee(18), queued);
        env(noop(hank), fee(19), queued);
        env(noop(alice), fee(20), queued);

        // Queue is full now.
        checkMetrics(env, 6, 6, 4, 3, 385, lastMedian);

        // Try to add another transaction with the default (low) fee,
        // it should fail because the queue is full.
        env(noop(charlie), ter(telINSUF_FEE_P));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), queued);

        // Queue is still full, of course, but the min fee has gone up
        checkMetrics(env, 6, 6, 4, 3, 410, lastMedian);

        // Close out the ledger, the transactions are accepted, the
        // queue is cleared, then the localTxs are retried. At this
        // point, daria's transaction that was dropped from the queue
        // is put back in. Neat.
        env.close();
        lastMedian = 500;
        checkMetrics(env, 2, 8, 5, 4, 256, lastMedian);

        lastMedian = 500;
        env.close();
        checkMetrics(env, 0, 10, 2, 5, 256, lastMedian);

        //////////////////////////////////////////////////////////////
        // Cleanup:

        // Create a few more transactions, so that
        // we can be sure that there's one in the queue when the
        // test ends and the TxQ is destructed.

        auto metrics = txq.getMetrics(*env.current());
        expect(metrics.txCount == 0, "txCount");
        auto txnsNeeded = metrics.txPerLedger - metrics.txInLedger;

        // Stuff the ledger.
        for (int i = 0; i <= txnsNeeded; ++i)
        {
            env(noop(env.master));
        }

        // Queue one straightforward transaction
        env(noop(env.master), fee(20), queued);
        ++metrics.txCount;

        checkMetrics(env, metrics.txCount,
            metrics.txQMaxSize, metrics.txPerLedger + 1,
            metrics.txPerLedger,
            256, lastMedian);
    }

    void testLocalTxRetry()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }),
            features(featureFeeEscalation));
        auto& txq = env.app().getTxQ();

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        auto openLedgerFee =
            [&]()
            {
                return fee(txq.openLedgerFee(*env.current()));
            };
        // Future transaction for Alice - fails
        env(noop(alice), openLedgerFee(),
            seq(env.seq(alice) + 1), ter(terPRE_SEQ));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        // Current transaction for Alice: held
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Alice - sequence is too far ahead, so won't queue.
        env(noop(alice), seq(env.seq(alice) + 2),
            ter(terPRE_SEQ));
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee());
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

        // Daria with low fee: hold
        env(noop(charlie), fee(1000), queued);
        checkMetrics(env, 2, boost::none, 4, 2, 256, 500);

        // Alice with normal fee: hold
        env(noop(alice), seq(env.seq(alice) + 1),
            queued);
        checkMetrics(env, 3, boost::none, 4, 2, 256, 500);

        env.close();
        // Verify that the held transactions got applied
        auto lastMedian = 500;
        // Alice's bad transaction applied from the
        // Local Txs.
        checkMetrics(env, 0, 8, 4, 4, 256, lastMedian);
    }

    void testLastLedgerSeq()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }),
            features(featureFeeEscalation));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto edgar = Account("edgar");
        auto felicia = Account("felicia");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(charlie, daria));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(edgar, felicia));
        env.close(env.now() + 5s, 10000ms);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);
        env(noop(bob));
        env(noop(charlie));
        env(noop(daria));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        // Queue an item with a LastLedgerSeq.
        env(noop(alice), json(R"({"LastLedgerSequence":7})"),
            queued);
        // Queue items with higher fees to force the previous
        // txn to wait.
        env(noop(bob), fee(20), queued);
        env(noop(charlie), fee(20), queued);
        env(noop(daria), fee(20), queued);
        env(noop(edgar), fee(20), queued);
        checkMetrics(env, 5, boost::none, 3, 2, 256, 500);

        env.close();
        checkMetrics(env, 1, 6, 4, 3, 256, 500);

        // Keep alice's transaction waiting.
        env(noop(bob), fee(20), queued);
        env(noop(charlie), fee(20), queued);
        env(noop(daria), fee(20), queued);
        env(noop(edgar), fee(20), queued);
        env(noop(felicia), fee(20), queued);
        checkMetrics(env, 6, 6, 4, 3, 257, 500);

        env.close();
        // alice's transaction expired without getting
        // into the ledger, so the queue is now empty.
        checkMetrics(env, 0, 8, 5, 4, 256, 512);
        expect(env.seq(alice) == 1);
    }

    void testZeroFeeTxn()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }),
            features(featureFeeEscalation));

        auto& txq = env.app().getTxQ();

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto carol = Account("carol");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(carol));
        env.close(env.now() + 5s, 10000ms);

        // Fill the ledger
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        env(noop(bob), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Even though this transaction has a 0 fee,
        // SetRegularKey::calculateBaseFee indicates this is
        // a "free" transaction, so it has an "infinite" fee
        // level and goes into the open ledger.
        env(regkey(alice, bob), fee(0));
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

        // Close out this ledger so we can get a maxsize
        env.close();
        checkMetrics(env, 0, 8, 1, 4, 256, 500);

        for (int i = 0; i < 4; ++i)
            env(noop(bob));
        checkMetrics(env, 0, 8, 5, 4, 256, 500);

        auto feeBob = 30;
        auto seqBob = env.seq(bob);
        for (int i = 0; i < 4; ++i)
        {
            env(noop(bob), fee(feeBob),
                seq(seqBob), queued);
            feeBob = (feeBob + 1) * 125 / 100;
            ++seqBob;
        }
        checkMetrics(env, 4, 8, 5, 4, 256, 500);

        // This transaction also has an "infinite" fee level,
        // but since bob has txns in the queue, it gets queued.
        env(regkey(bob, alice), fee(0),
            seq(seqBob), queued);
        ++seqBob;
        checkMetrics(env, 5, 8, 5, 4, 256, 500);

        // Unfortunately bob can't get any more txns into
        // the queue, because of the multiTxnPercent.
        // TANSTAAFL
        env(noop(bob), fee(XRP(100)),
            seq(seqBob), ter(telINSUF_FEE_P));

        // Let carol overfill the queue, and kick out all
        // of the transactions, except bob's "infinite".
        auto feeCarol = feeBob;
        auto seqCarol = env.seq(carol);
        for (int i = 0; i < 7; ++i)
        {
            env(noop(carol), fee(feeCarol),
                seq(seqCarol), queued);
            feeCarol = (feeCarol + 1) * 125 / 100;
            ++seqCarol;
        }
        checkMetrics(env, 8, 8, 5, 4, feeBob * 256 / 10 + 1, 500);

        // Carol can not take that 8th entry away from Bob.
        env(noop(carol), fee(feeCarol),
            seq(seqCarol), ter(telCAN_NOT_QUEUE));

        env.close();
        // All the "lost" transactions are reapplied
        // to the queue from the Local Txs.
        checkMetrics(env, 7, 10, 6, 5, 256, 500);

        env.close();
        auto lastMedian = 3520;
        checkMetrics(env, 0, 12, 8, 6, 256, lastMedian);

        env.close();
        lastMedian = 1395;
        checkMetrics(env, 0, 16, 0, 8, 256, lastMedian);
    }

    void testPreclaimFailures()
    {
        using namespace jtx;

        Env env(*this, makeConfig(), features(featureFeeEscalation));

        auto alice = Account("alice");
        auto bob = Account("bob");

        env.fund(XRP(1000), noripple(alice));

        // These types of checks are tested elsewhere, but
        // this verifies that TxQ handles the failures as
        // expected.

        // Fail in preflight
        env(pay(alice, bob, XRP(-1000)),
            ter(temBAD_AMOUNT));

        // Fail in preclaim
        env(noop(alice), fee(XRP(100000)),
            ter(terINSUF_FEE_B));
    }

    void testQueuedFailure()
    {
        using namespace jtx;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }),
            features(featureFeeEscalation));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        env.fund(XRP(1000), noripple(alice, bob));

        checkMetrics(env, 0, boost::none, 2, 2, 256, 500);

        // Fill the ledger
        env(noop(alice));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        // Put a transaction in the queue
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Now cheat, and bypass the queue.
        {
            auto const& jt = env.jt(noop(alice));
            expect(jt.stx);

            bool didApply;
            TER ter;

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j)
                {
                    std::tie(ter, didApply) =
                        ripple::apply(env.app(),
                            view, *jt.stx, tapNONE,
                                env.journal);
                    return didApply;
                }
                );
            env.postconditions(jt, ter, didApply);
        }
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

        env.close();
        // Alice's queued transaction failed in TxQ::accept
        // with tefPAST_SEQ
        checkMetrics(env, 0, 8, 0, 4, 256, 500);
    }

    void testMultiTxnPerAccount()
    {
        using namespace jtx;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }),
            features(featureFeeEscalation));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 3, 256, lastMedian);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 3, 256, lastMedian);

        // Alice - price starts exploding: held
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 4, 3, 256, lastMedian);

        // Alice - try to queue a second transaction, but leave a gap
        env(noop(alice), seq(env.seq(alice) + 2), fee(100),
            ter(terPRE_SEQ));
        checkMetrics(env, 1, boost::none, 4, 3, 256, lastMedian);

        // Alice - queue a second transaction. Yay.
        env(noop(alice), seq(env.seq(alice) + 1), fee(13),
            queued);
        checkMetrics(env, 2, boost::none, 4, 3, 256, lastMedian);

        // Alice - queue a third transaction. Yay.
        env(noop(alice), seq(env.seq(alice) + 2), fee(17),
            queued);
        checkMetrics(env, 3, boost::none, 4, 3, 256, lastMedian);

        // Bob - queue a transaction
        env(noop(bob), queued);
        checkMetrics(env, 4, boost::none, 4, 3, 256, lastMedian);

        // Bob - queue a second transaction
        env(noop(bob), seq(env.seq(bob) + 1), fee(50),
            queued);
        checkMetrics(env, 5, boost::none, 4, 3, 256, lastMedian);

        // Charlie - queue a transaction, with a higher fee
        // than default
        env(noop(charlie), fee(15), queued);
        checkMetrics(env, 6, boost::none, 4, 3, 256, lastMedian);

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto charlieSeq = env.seq(charlie);

        env.close();
        // Verify that all of but one of the queued transactions
        // got applied.
        lastMedian = 500;
        checkMetrics(env, 1, 8, 5, 4, 256, lastMedian);

        // Verify that the stuck transaction is Bob's second.
        // Even though it had a higher fee than Alice's and
        // Charlie's, it didn't get attempted until the fee escalated.
        expect(env.seq(alice) == aliceSeq + 3);
        expect(env.seq(bob) == bobSeq + 1);
        expect(env.seq(charlie) == charlieSeq + 1);

        // Alice - fill up the queue
        std::int64_t aliceFee = 10;
        aliceSeq = env.seq(alice);
        auto lastLedgerSeq = env.closed()->info().seq + 2;
        for (auto i = 0; i < 7; i++)
        {
            env(noop(alice), seq(aliceSeq),
                json(jss::LastLedgerSequence, lastLedgerSeq + i),
                    fee(aliceFee), queued);
            aliceFee = (aliceFee + 1) * 125 / 100;
            ++aliceSeq;
        }
        checkMetrics(env, 8, 8, 5, 4, 257, lastMedian);

        // Alice attempts to add another item to the queue,
        // but you can't force your own earlier txn off the
        // queue.
        env(noop(alice), seq(aliceSeq),
            json(jss::LastLedgerSequence, lastLedgerSeq + 7),
                fee(aliceFee), ter(telCAN_NOT_QUEUE));
        checkMetrics(env, 8, 8, 5, 4, 257, lastMedian);

        // Charlie - add another item to the queue, which
        // causes Alice's cheap txn to drop
        env(noop(charlie), fee(30), queued);
        checkMetrics(env, 8, 8, 5, 4, 333, lastMedian);

        // Alice - now attempt to add one more to the queue,
        // which fails because the earliest txn is gone, so
        // there is no complete chain, and rippled protects
        // itself against wasting more resources.
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(terPRE_SEQ));
        aliceFee = (aliceFee + 1) * 125 / 100;
        ++aliceSeq;
        checkMetrics(env, 8, 8, 5, 4, 333, lastMedian);

        env.close();
        lastMedian = 500;
        // Alice's transactions stayed in the queue,
        // and the lost ones are replayed and added back
        // to the queue or open ledger.
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Try to replace a middle item in the queue
        // without enough fee.
        aliceSeq = env.seq(alice) + 2;
        aliceFee = 27;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telINSUF_FEE_P));
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Replace a middle item from the queue successfully
        ++aliceFee;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), queued);
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Try to replace the next item in the queue
        // without enough fee.
        ++aliceSeq;
        aliceFee = aliceFee * 125 / 100 - 1;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telINSUF_FEE_P));
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Replace a middle item from the queue successfully
        ++aliceFee;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), queued);
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Try to replace that item with a transaction that will
        // bankrupt Alice. Fails, because an account can't have
        // more than the minimum reserve in flight before the
        // last queued transaction
        aliceFee = env.le(alice)->getFieldAmount(sfBalance).xrp().drops()
            - (198);
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telCAN_NOT_QUEUE));
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Try to spend more than Alice can afford with all the other txs.
        aliceSeq = env.seq(alice) + 6;
        aliceFee = env.le(alice)->getFieldAmount(sfBalance).xrp().drops()
            - (174);
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(terINSUF_FEE_B));
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Replace the last queued item with a transaction that will
        // bankrupt Alice
        --aliceFee;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), queued);
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        // Alice - Attempt to queue a last transaction, but it
        // fails because the fee in flight is too high, before
        // the fee is checked against the balance
        aliceFee = aliceFee * 125 / 100 + 1;
        env(noop(alice), seq(env.seq(alice) + 7),
            fee(aliceFee), ter(telCAN_NOT_QUEUE));
        checkMetrics(env, 7, 10, 3, 5, 256, lastMedian);

        env.close();
        // All of Alice's transactions applied.
        lastMedian = 768;
        checkMetrics(env, 0, 10, 7, 5, 256, lastMedian);

        env.close();
        lastMedian = 896;
        checkMetrics(env, 0, 14, 0, 7, 256, lastMedian);

        // Alice is still broke
        env.require(balance(alice, XRP(0)));
        env(noop(alice), ter(terINSUF_FEE_B));
        checkMetrics(env, 0, 14, 0, 7, 256, lastMedian);
    }

    void testTieBreaking()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "4" } }),
            features(featureFeeEscalation));
        auto& txq = env.app().getTxQ();

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto elmo = Account("elmo");
        auto fred = Account("fred");
        auto gwen = Account("gwen");
        auto hank = Account("hank");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 4, 256, lastMedian);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 4, 256, lastMedian);

        env.close();
        checkMetrics(env, 0, 8, 0, 4, 256, lastMedian);

        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(env, 0, 8, 4, 4, 256, lastMedian);

        env.close();
        checkMetrics(env, 0, 8, 0, 4, 256, lastMedian);

        //////////////////////////////////////////////////////////////

        // TODO: Duplicate the full queue test case with equal fees.
        // Verify that the last transaction added is always the first
        // dropped.

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(gwen));
        env(noop(hank));
        env(noop(gwen));
        env(noop(fred));
        env(noop(elmo));
        checkMetrics(env, 0, 8, 5, 4, 256, lastMedian);

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto charlieSeq = env.seq(charlie);
        auto dariaSeq = env.seq(daria);
        auto elmoSeq = env.seq(elmo);
        auto fredSeq = env.seq(fred);
        auto gwenSeq = env.seq(gwen);
        auto hankSeq = env.seq(hank);

        // This time, use identical fees.
        env(noop(alice), fee(15), queued);
        env(noop(bob), fee(15), queued);
        env(noop(charlie), fee(15), queued);
        env(noop(daria), fee(15), queued);
        env(noop(elmo), fee(15), queued);
        env(noop(fred), fee(15), queued);
        env(noop(gwen), fee(15), queued);
        // This one gets into the queue, but gets dropped when the
        // higher fee one is added later.
        env(noop(hank), fee(15), queued);

        // Queue is full now. Minimum fee now reflects the
        // lowest fee in the queue.
        checkMetrics(env, 8, 8, 5, 4, 385, lastMedian);

        // Try to add another transaction with the default (low) fee,
        // it should fail because it can't replace the one already
        // there.
        env(noop(charlie), ter(telINSUF_FEE_P));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), seq(charlieSeq + 1), queued);

        // Queue is still full.
        checkMetrics(env, 8, 8, 5, 4, 385, lastMedian);

        // alice, bob, charlie, daria, and elmo's txs
        // are processed out of the queue into the ledger,
        // leaving fred and gwen's txs. hank's tx is
        // retried from localTxs, and put back into the
        // queue.
        env.close();
        lastMedian = 500;
        checkMetrics(env, 3, 10, 6, 5, 256, lastMedian);

        expect(aliceSeq + 1 == env.seq(alice));
        expect(bobSeq + 1 == env.seq(bob));
        expect(charlieSeq + 2 == env.seq(charlie));
        expect(dariaSeq + 1 == env.seq(daria));
        expect(elmoSeq + 1 == env.seq(elmo));
        expect(fredSeq == env.seq(fred));
        expect(gwenSeq == env.seq(gwen));
        expect(hankSeq == env.seq(hank));

        aliceSeq = env.seq(alice);
        bobSeq = env.seq(bob);
        charlieSeq = env.seq(charlie);
        dariaSeq = env.seq(daria);
        elmoSeq = env.seq(elmo);

        // Fill up the queue again
        env(noop(alice), fee(15), queued);
        env(noop(alice), seq(aliceSeq + 1), fee(15), queued);
        env(noop(alice), seq(aliceSeq + 2), fee(15), queued);
        env(noop(bob), fee(15), queued);
        env(noop(charlie), fee(15), queued);
        env(noop(daria), fee(15), queued);
        // This one gets into the queue, but gets dropped when the
        // higher fee one is added later.
        env(noop(elmo), fee(15), queued);
        checkMetrics(env, 10, 10, 6, 5, 385, lastMedian);

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(alice), fee(100), seq(aliceSeq + 3), queued);

        lastMedian = 500;
        env.close();
        checkMetrics(env, 4, 12, 7, 6, 256, lastMedian);

        expect(fredSeq + 1 == env.seq(fred));
        expect(gwenSeq + 1 == env.seq(gwen));
        expect(hankSeq + 1 == env.seq(hank));
        expect(aliceSeq + 4 == env.seq(alice));
        expect(bobSeq == env.seq(bob));
        expect(charlieSeq == env.seq(charlie));
        expect(dariaSeq == env.seq(daria));
        expect(elmoSeq == env.seq(elmo));
    }

    void testDisabled()
    {
        using namespace jtx;

        Env env(*this);
        size_t constexpr txPerLedger = 1000;

        auto alice = Account("alice");

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, txPerLedger, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice));
        checkMetrics(env, 0, boost::none, 1, txPerLedger, 256, lastMedian);

        // If the queue was enabled, most of these would
        // return terQUEUED. (The required fee for the last
        // would be 10 * 500 * 11^2 / 5^2 = 24,200.)
        for (int i = 0; i < 10; ++i)
            env(noop(alice), fee(30));

        // Either way, we get metrics.
        checkMetrics(env, 0, boost::none, 11, txPerLedger, 256, lastMedian);

        env.close();
        // If the queue was enabled, it would have a limit, and the
        // lastMedian would be 256*3 = 768.
        checkMetrics(env, 0, boost::none, 0, txPerLedger, 256, lastMedian);
    }

    void testAcctTxnID()
    {
        using namespace jtx;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "1" } }),
            features(featureFeeEscalation));

        auto alice = Account("alice");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 1, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice));
        checkMetrics(env, 0, boost::none, 1, 1, 256, lastMedian);

        env(fset(alice, asfAccountTxnID));
        checkMetrics(env, 0, boost::none, 2, 1, 256, lastMedian);

        // Immediately after the fset, the sfAccountTxnID field
        // is still uninitialized, so preflight succeeds here,
        // and this txn fails because it can't be stored in the queue.
        env(noop(alice), json(R"({"AccountTxnID": "0"})"),
            ter(telINSUF_FEE_P));

        checkMetrics(env, 0, boost::none, 2, 1, 256, lastMedian);
        env.close();
        // The failed transaction is retried from LocalTx
        // and succeeds.
        checkMetrics(env, 0, 4, 1, 2, 256, lastMedian);

        env(noop(alice));
        checkMetrics(env, 0, 4, 2, 2, 256, lastMedian);

        env(noop(alice), json(R"({"AccountTxnID": "0"})"),
            ter(tefWRONG_PRIOR));
    }

    void testMaximum()
    {
        using namespace jtx;

        Env env(*this, makeConfig(
            { {"minimum_txn_in_ledger_standalone", "2"},
                {"target_txn_in_ledger", "4"},
                    {"maximum_txn_in_ledger", "5"} }),
                        features(featureFeeEscalation));
        auto& txq = env.app().getTxQ();

        auto alice = Account("alice");
        auto queued = ter(terQUEUED);
        auto lastMedian = 500;

        auto openLedgerFee =
            [&]()
        {
            return fee(txq.openLedgerFee(*env.current()));
        };

        checkMetrics(env, 0, boost::none, 0, 2, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice));
        checkMetrics(env, 0, boost::none, 1, 2, 256, lastMedian);

        for (int i = 0; i < 10; ++i)
            env(noop(alice), fee(openLedgerFee()));

        checkMetrics(env, 0, boost::none, 11, 2, 256, lastMedian);

        env.close();
        lastMedian = 800025;
        // If not for the maximum, the per ledger would be 11.
        checkMetrics(env, 0, 10, 0, 5, 256, lastMedian);

    }

    void testUnexpectedBalanceChange()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }),
                features(featureFeeEscalation));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        auto openLedgerFee =
            [&]()
        {
            return fee(env.app().getTxQ().openLedgerFee(*env.current()));
        };

        expect(env.current()->fees().base == 10);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 3, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice, bob));
        checkMetrics(env, 0, boost::none, 2, 3, 256, lastMedian);
        auto USD = bob["USD"];

        env(offer(alice, USD(5000), XRP(50000)), require(owners(alice, 1)));
        checkMetrics(env, 0, boost::none, 3, 3, 256, lastMedian);

        env.close();
        checkMetrics(env, 0, 6, 0, 3, 256, lastMedian);

        // Fill up the ledger
        for (int i = 0; i < 4; ++i)
            env(noop(alice));
        checkMetrics(env, 0, 6, 4, 3, 256, lastMedian);

        // Queue up a couple of transactions, plus one
        // really expensive one.
        auto aliceSeq = env.seq(alice);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), fee(XRP(1000)),
            seq(aliceSeq), queued);
        checkMetrics(env, 4, 6, 4, 3, 256, lastMedian);

        // This offer should take Alice's offer
        // up to Alice's reserve.
        env(offer(bob, XRP(50000), USD(5000)),
            openLedgerFee(), require(balance("alice", XRP(250)),
                owners(alice, 1), lines(alice, 1)));
        checkMetrics(env, 4, 6, 5, 3, 256, lastMedian);

        // Try adding a new transaction.
        // Too many fees in flight.
        env(noop(alice), fee(XRP(2000)), seq(aliceSeq+1),
            ter(telCAN_NOT_QUEUE));
        checkMetrics(env, 4, 6, 5, 3, 256, lastMedian);

        // Close the ledger. All of Alice's transactions
        // take a fee, except the last one.
        env.close();
        checkMetrics(env, 1, 10, 3, 5, 256, lastMedian);
        env.require(balance(alice, XRP(250) - drops(30)));

        // Still can't add a new transaction for Alice,
        // no matter the fee.
        env(noop(alice), fee(XRP(2000)), seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE));
        checkMetrics(env, 1, 10, 3, 5, 256, lastMedian);

        /* At this point, Alice's transaction is indefinitely
            stuck in the queue. Eventually it will either
            expire, get forced off the end by more valuable
            transactions, get replaced by Alice, or Alice
            will get more XRP, and it'll process.
        */

        for (int i = 0; i < 9; ++i)
        {
            env.close();
            checkMetrics(env, 1, 10, 0, 5, 256, lastMedian);
        }

        // And Alice's transaction expires (via the retry limit,
        // not LastLedgerSequence).
        env.close();
        checkMetrics(env, 0, 10, 0, 5, 256, lastMedian);
    }

    void testBlockers()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }),
            features(featureFeeEscalation), features(featureMultiSign));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 3, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice, bob));
        env.memoize(charlie);
        env.memoize(daria);
        checkMetrics(env, 0, boost::none, 2, 3, 256, lastMedian);

        // Fill up the open ledger
        env(noop(alice));
        // Set a regular key just to clear the password spent flag
        env(regkey(alice, charlie));
        checkMetrics(env, 0, boost::none, 4, 3, 256, lastMedian);

        // Put some "normal" txs in the queue
        auto aliceSeq = env.seq(alice);
        env(noop(alice), queued);
        env(noop(alice), seq(aliceSeq + 1), queued);
        env(noop(alice), seq(aliceSeq + 2), queued);

        // Can't replace the first tx with a blocker
        env(fset(alice, asfAccountTxnID), fee(20), ter(telINSUF_FEE_P));
        // Can't replace the second / middle tx with a blocker
        env(regkey(alice, bob), seq(aliceSeq + 1), fee(20),
            ter(telCAN_NOT_QUEUE));
        env(signers(alice, 2, { {bob}, {charlie}, {daria} }), fee(20),
            seq(aliceSeq + 1), ter(telCAN_NOT_QUEUE));
        // CAN replace the last tx with a blocker
        env(signers(alice, 2, { { bob },{ charlie },{ daria } }), fee(20),
            seq(aliceSeq + 2), queued);
        env(regkey(alice, bob), seq(aliceSeq + 2), fee(30),
            queued);

        // Can't queue up any more transactions after the blocker
        env(noop(alice), seq(aliceSeq + 3), ter(telCAN_NOT_QUEUE));

        // Other accounts are not affected
        env(noop(bob), queued);

        // Can replace the tranactions before the blocker
        env(noop(alice), fee(14), queued);

        // Can replace the blocker itself
        env(noop(alice), seq(aliceSeq + 2), fee(40), queued);

        // And now there's no block.
        env(noop(alice), seq(aliceSeq + 3), queued);
    }

    void testInFlightBalance()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }),
            features(featureFeeEscalation), features(featureTickets));

        auto alice = Account("alice");
        auto charlie = Account("charlie");
        auto gw = Account("gw");

        auto queued = ter(terQUEUED);

        expect(env.current()->fees().base == 10);
        expect(env.current()->fees().reserve == 200 * 1000000);
        expect(env.current()->fees().increment == 50 * 1000000);

        auto lastMedian = 500;
        checkMetrics(env, 0, boost::none, 0, 3, 256, lastMedian);

        env.fund(XRP(50000), noripple(alice, charlie), gw);
        checkMetrics(env, 0, boost::none, 4, 3, 256, lastMedian);

        auto USD = gw["USD"];
        auto BUX = charlie["BUX"];

        //////////////////////////////////////////
        auto aliceSeq = env.seq(alice);
        auto aliceBal = env.balance(alice);

        env.require(balance(alice, XRP(50000)),
            owners(alice, 0));

        // If this offer crosses, all of alice's
        // XRP will be taken (except the reserve).
        env(offer(alice, BUX(5000), XRP(50000)),
            queued);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice), seq(aliceSeq + 1), ter(terINSUF_FEE_B));

        env.close();
        checkMetrics(env, 0, 8, 2, 4, 256, lastMedian);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(20)),
            owners(alice, 1));

        //////////////////////////////////////////
        for (auto i = 2; i < 5; ++i)
        {
            env(noop(alice));
        }
        checkMetrics(env, 0, 8, 5, 4, 256, lastMedian);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this payment succeeds, alice will
        // send her entire balance to charlie
        // (minus the reserve).
        env(pay(alice, charlie, XRP(50000)),
            queued);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice), seq(aliceSeq + 1), ter(terINSUF_FEE_B));

        env.close();
        checkMetrics(env, 0, 10, 2, 5, 256, lastMedian);

        // But once we close the ledger, we find alice
        // still has most of her balance, because the
        // payment was unfunded!
        env.require(balance(alice, aliceBal - drops(20)),
            owners(alice, 1));

        //////////////////////////////////////////
        auto const amount = USD(500000);
        env(trust(alice, USD(50000000)));
        env(trust(charlie, USD(50000000)));
        checkMetrics(env, 0, 10, 4, 5, 256, lastMedian);
        env(pay(gw, alice, amount));
        checkMetrics(env, 0, 10, 5, 5, 256, lastMedian);

        for (auto i = 5; i < 6; ++i)
        {
            env(noop(alice));
        }
        checkMetrics(env, 0, 10, 6, 5, 256, lastMedian);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        auto aliceUSD = env.balance(alice, USD);

        // If this payment succeeds, alice will
        // send her entire USD balance to charlie.
        env(pay(alice, charlie, amount),
            queued);

        // But that's fine, because it doesn't affect
        // alice's XRP balance (other than the fee, of course).
        env(noop(alice), seq(aliceSeq + 1), queued);

        env.close();
        checkMetrics(env, 0, 12, 2, 6, 256, lastMedian);

        // So once we close the ledger, alice has her
        // XRP balance, but not her USD balance
        env.require(balance(alice, aliceBal - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, aliceUSD),
            owners(alice, 2));

        //////////////////////////////////////////


    }

    void run()
    {
        testQueue();
        testLocalTxRetry();
        testLastLedgerSeq();
        testZeroFeeTxn();
        testPreclaimFailures();
        testQueuedFailure();
        testMultiTxnPerAccount();
        testTieBreaking();
        testDisabled();
        testAcctTxnID();
        testMaximum();
        testUnexpectedBalanceChange();
        testBlockers();
        testInFlightBalance();
    }
};

BEAST_DEFINE_TESTSUITE(TxQ,app,ripple);

}
}
