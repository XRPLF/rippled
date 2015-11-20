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
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STTx.h>
#include <ripple/test/jtx.h>

namespace ripple {
namespace test {

class TxQ_test : public TestSuite
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
        auto metrics = env.app().getTxQ().getMetrics(*env.open());
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

    void
    close(jtx::Env& env, size_t expectedTxSetSize, bool timeLeap = false)
    {
        {
            auto const view = env.open();
            expect(view->txCount() == expectedTxSetSize, "TxSet size mismatch");
            // Update fee computations.
            // Note implementing this way assumes that everything
            // in the open ledger _will_ make it into the closed
            // ledger, but for metrics that's probably good enough.
            env.app().getTxQ().processValidatedLedger(
                env.app(), *view, timeLeap, tapENABLE_TESTING);
        }

        env.close(
            [&](OpenView& view, beast::Journal j)
            {
                // Stuff the ledger with transactions from the queue.
                return env.app().getTxQ().accept(env.app(), view,
                    tapENABLE_TESTING);
            }
            );
    }

    void
    submit(jtx::Env& env, jtx::JTx const& jt)
    {
        // Env checks this, but this test shouldn't
        // generate any malformed txns.
        expect(jt.stx);

        bool didApply;
        TER ter;

        env.openLedger.modify(
            [&](OpenView& view, beast::Journal j)
            {
                std::tie(ter, didApply) =
                    env.app().getTxQ().apply(env.app(),
                        view, jt.stx, tapENABLE_TESTING,
                            env.journal);

                return didApply;
            }
        );

        env.postconditions(jt, ter, didApply);
    }

    static
    std::unique_ptr<Config const>
    makeConfig()
    {
        auto p = std::make_unique<Config>();
        setupConfigForUnitTests(*p);
        auto& section = p->section("transaction_queue");
        section.set("ledgers_in_queue", "2");
        section.set("min_ledgers_to_compute_size_limit", "3");
        section.set("max_ledger_counts_to_store", "100");
        section.set("retry_sequence_percent", "125");
        return std::move(p);
    }

public:
    void testQueue()
    {
        using namespace jtx;

        Env env(*this, makeConfig());

        auto& txq = env.app().getTxQ();
        txq.setMinimumTx(3);

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto elmo = Account("elmo");
        auto fred = Account("fred");
        auto gwen = Account("gwen");
        auto hank = Account("hank");

        auto queued = ter(terQUEUED);

        expectEquals(env.open()->fees().base, 10);

        checkMetrics(env, 0, boost::none, 0, 3, 256, 500);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 3, 256, 500);

        // Alice - price starts exploding: held
        submit(env,
            env.jt(noop(alice), queued));
        checkMetrics(env, 1, boost::none, 4, 3, 256, 500);

        // Alice - Alice is already in the queue, so can't hold.
        submit(env,
            env.jt(noop(alice), seq(env.seq(alice) + 1),
                ter(telINSUF_FEE_P)));
        checkMetrics(env, 1, boost::none, 4, 3, 256, 500);

        auto openLedgerFee = 
            [&]()
            {
                return fee(txq.openLedgerFee(*env.open()));
            };
        // Alice's next transaction -
        // fails because the item in the TxQ hasn't applied.
        submit(env,
            env.jt(noop(alice), openLedgerFee(),
                seq(env.seq(alice) + 1), ter(terPRE_SEQ)));
        checkMetrics(env, 1, boost::none, 4, 3, 256, 500);

        // Bob with really high fee - applies
        submit(env,
            env.jt(noop(bob), openLedgerFee()));
        checkMetrics(env, 1, boost::none, 5, 3, 256, 500);

        // Daria with low fee: hold
        submit(env,
            env.jt(noop(daria), fee(1000), queued));
        checkMetrics(env, 2, boost::none, 5, 3, 256, 500);

        close(env, 5);
        // Verify that the held transactions got applied
        auto lastMedian = 500;
        checkMetrics(env, 0, 10, 2, 5, 256, lastMedian);

        //////////////////////////////////////////////////////////////

        // Make some more accounts. We'll need them later to abuse the queue.
        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(env, 0, 10, 6, 5, 256, lastMedian);

        // Now get a bunch of transactions held.
        submit(env,
            env.jt(noop(alice), fee(12), queued));
        checkMetrics(env, 1, 10, 6, 5, 256, lastMedian);

        submit(env,
            env.jt(noop(bob), fee(10), queued)); // won't clear the queue
        submit(env,
            env.jt(noop(charlie), fee(20), queued));
        submit(env,
            env.jt(noop(daria), fee(15), queued));
        submit(env,
            env.jt(noop(elmo), fee(11), queued));
        submit(env,
            env.jt(noop(fred), fee(19), queued));
        submit(env,
            env.jt(noop(gwen), fee(16), queued));
        submit(env,
            env.jt(noop(hank), fee(18), queued));
        checkMetrics(env, 8, 10, 6, 5, 256, lastMedian);

        close(env, 6);
        // Verify that the held transactions got applied
        lastMedian = 500;
        checkMetrics(env, 1, 12, 7, 6, 256, lastMedian);

        // Bob's transaction is still stuck in the queue.

        //////////////////////////////////////////////////////////////

        // Hank sends another txn
        submit(env,
            env.jt(noop(hank), fee(10), queued));
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn  got held and bumps the fee,
        // but doesn't even bump it enough to requeue
        submit(env,
            env.jt(noop(hank), fee(11), ter(telINSUF_FEE_P)));
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn got held and bumps the fee,
        // enough to requeue, but doesn't bump it enough to
        // apply to the ledger
        submit(env,
            env.jt(noop(hank), fee(6000), queued));
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256, lastMedian);

        // Hank sees his txn got held and bumps the fee,
        // high enough to get into the open ledger, because
        // he doesn't want to wait.
        submit(env,
            env.jt(noop(hank), openLedgerFee()));
        checkMetrics(env, 1, 12, 8, 6, 256, lastMedian);

        // Hank then sends another, less important txn
        // (In addition to the metrics, this will verify that
        //  the original txn got removed.)
        submit(env,
            env.jt(noop(hank), fee(6000), queued));
        checkMetrics(env, 2, 12, 8, 6, 256, lastMedian);

        close(env, 8);

        // Verify that bob and hank's txns were applied
        lastMedian = 500;
        checkMetrics(env, 0, 16, 2, 8, 256, lastMedian);

        // Close again with a simulated time leap to
        // reset the escalation limit down to minimum
        lastMedian = 76928;
        close(env, 2, true);
        checkMetrics(env, 0, 16, 0, 3, 256, lastMedian);
        // Then close once more without the time leap
        // to reset the queue maxsize down to minimum
        lastMedian = 500;
        close(env, 0);
        checkMetrics(env, 0, 6, 0, 3, 256, lastMedian);

        //////////////////////////////////////////////////////////////

        // At this point, the queue should have a limit of 6.
        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        submit(env,
            env.jt(noop(hank)));
        submit(env,
            env.jt(noop(gwen)));
        submit(env,
            env.jt(noop(fred)));
        submit(env,
            env.jt(noop(elmo)));
        checkMetrics(env, 0, 6, 4, 3, 256, lastMedian);

        // Use explicit fees so we can control which txn
        // will get dropped
        submit(env,
            env.jt(noop(alice), fee(20), queued));
        submit(env,
            env.jt(noop(hank), fee(19), queued));
        submit(env,
            env.jt(noop(gwen), fee(18), queued));
        submit(env,
            env.jt(noop(fred), fee(17), queued));
        submit(env,
            env.jt(noop(elmo), fee(16), queued));
        // This one gets into the queue, but gets dropped when the
        // higher fee one is added later.
        submit(env,
            env.jt(noop(daria), fee(15), queued));

        // Queue is full now.
        checkMetrics(env, 6, 6, 4, 3, 385, lastMedian);

        // Try to add another transaction with the default (low) fee,
        // it should fail because the queue is full.
        submit(env,
            env.jt(noop(charlie), ter(telINSUF_FEE_P)));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        submit(env,
            env.jt(noop(charlie), fee(100), queued));

        // Queue is still full, of course, but the min fee has gone up
        checkMetrics(env, 6, 6, 4, 3, 410, lastMedian);

        close(env, 4);
        lastMedian = 500;
        checkMetrics(env, 1, 8, 5, 4, 256, lastMedian);

        lastMedian = 500;
        close(env, 5);
        checkMetrics(env, 0, 10, 1, 5, 256, lastMedian);

        //////////////////////////////////////////////////////////////
        // Cleanup:

        // Create a few more transactions, so that
        // we can be sure that there's one in the queue when the
        // test ends and the TxQ is destructed.

        auto metrics = txq.getMetrics(*env.open());
        expect(metrics.txCount == 0, "txCount");
        auto txnsNeeded = metrics.txPerLedger - metrics.txInLedger;

        // Stuff the ledger.
        for (int i = 0; i <= txnsNeeded; ++i)
        {
            submit(env,
                env.jt(noop(env.master)));
        }

        // Queue one straightforward transaction
        submit(env,
            env.jt(noop(env.master), fee(20), queued));
        ++metrics.txCount;

        checkMetrics(env, metrics.txCount,
            metrics.txQMaxSize, metrics.txPerLedger + 1,
            metrics.txPerLedger,
            256, lastMedian);
    }

    void testLastLedgerSeq()
    {
        using namespace jtx;

        Env env(*this, makeConfig());

        auto& txq = env.app().getTxQ();
        txq.setMinimumTx(2);

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto edgar = Account("edgar");
        auto felicia = Account("felicia");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        // Fund these accounts and close the ledger without
        // involving the queue, so that stats aren't affected.
        env.fund(XRP(1000), noripple(alice, bob, charlie, daria, edgar, felicia));
        env.close();

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);
        submit(env,
            env.jt(noop(bob)));
        submit(env,
            env.jt(noop(charlie)));
        submit(env,
            env.jt(noop(daria)));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        // Queue an item with a LastLedgerSeq.
        submit(env,
            env.jt(noop(alice), json(R"({"LastLedgerSequence":4})"),
                queued));
        // Queue items with higher fees to force the previous
        // txn to wait.
        submit(env,
            env.jt(noop(bob), fee(20), queued));
        submit(env,
            env.jt(noop(charlie), fee(20), queued));
        submit(env,
            env.jt(noop(daria), fee(20), queued));
        submit(env,
            env.jt(noop(edgar), fee(20), queued));
        checkMetrics(env, 5, boost::none, 3, 2, 256, 500);

        close(env, 3);
        checkMetrics(env, 1, 6, 4, 3, 256, 500);

        // Keep alice's transaction waiting.
        submit(env,
            env.jt(noop(bob), fee(20), queued));
        submit(env,
            env.jt(noop(charlie), fee(20), queued));
        submit(env,
            env.jt(noop(daria), fee(20), queued));
        submit(env,
            env.jt(noop(edgar), fee(20), queued));
        submit(env,
            env.jt(noop(felicia), fee(20), queued));
        checkMetrics(env, 6, 6, 4, 3, 257, 500);

        close(env, 4);
        // alice's transaction expired without getting
        // into the ledger, so the queue is now empty.
        checkMetrics(env, 0, 8, 5, 4, 256, 512);
        expect(env.seq(alice) == 1);
    }

    void testZeroFeeTxn()
    {
        using namespace jtx;

        Env env(*this, makeConfig());

        auto& txq = env.app().getTxQ();
        txq.setMinimumTx(2);

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        // Fund these accounts and close the ledger without
        // involving the queue, so that stats aren't affected.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close();

        // Fill the ledger
        submit(env, env.jt(noop(alice)));
        submit(env, env.jt(noop(alice)));
        submit(env, env.jt(noop(alice)));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        submit(env,
            env.jt(noop(bob), queued));
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Even though this transaction has a 0 fee,
        // SetRegularKey::calculateBaseFee indicates this is
        // a "free" transaction, so it has an "infinite" fee
        // level and goes into the open ledger.
        submit(env,
            env.jt(regkey(alice, bob), fee(0)));
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

        // This transaction also has an "infinite" fee level,
        // but since bob has a txn in the queue, and multiple
        // transactions aren't yet supported, this one fails
        // with terPRE_SEQ (notably, *not* telINSUF_FEE_P).
        // This implicitly relies on preclaim succeeding and
        // canBeHeld failing under the hood.
        submit(env,
            env.jt(regkey(bob, alice), fee(0),
                seq(env.seq(bob) + 1), ter(terPRE_SEQ)));
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

    }

    void testPreclaimFailures()
    {
        using namespace jtx;

        Env env(*this, makeConfig());

        auto alice = Account("alice");
        auto bob = Account("bob");

        env.fund(XRP(1000), noripple(alice));

        // These types of checks are tested elsewhere, but
        // this verifies that TxQ handles the failures as
        // expected.

        // Fail in preflight
        submit(env,
            env.jt(pay(alice, bob, XRP(-1000)),
                ter(temBAD_AMOUNT)));

        // Fail in preclaim
        submit(env,
            env.jt(noop(alice), fee(XRP(100000)),
                ter(terINSUF_FEE_B)));
    }

    void testQueuedFailure()
    {
        using namespace jtx;

        Env env(*this, makeConfig());

        auto& txq = env.app().getTxQ();
        txq.setMinimumTx(2);

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256, 500);

        env.fund(XRP(1000), noripple(alice, bob));

        checkMetrics(env, 0, boost::none, 2, 2, 256, 500);

        // Fill the ledger
        submit(env, env.jt(noop(alice)));
        checkMetrics(env, 0, boost::none, 3, 2, 256, 500);

        // Put a transaction in the queue
        submit(env, env.jt(noop(alice), queued));
        checkMetrics(env, 1, boost::none, 3, 2, 256, 500);

        // Now cheat, and bypass the queue.
        env(noop(alice));
        checkMetrics(env, 1, boost::none, 4, 2, 256, 500);

        close(env, 4);
        // Alice's queued transaction failed in TxQ::accept
        // with tefPAST_SEQ
        checkMetrics(env, 0, 8, 0, 4, 256, 500);

    }

    void run()
    {
        testQueue();
        testLastLedgerSeq();
        testZeroFeeTxn();
        testPreclaimFailures();
        testQueuedFailure();
    }
};

BEAST_DEFINE_TESTSUITE(TxQ,app,ripple);

}
}
