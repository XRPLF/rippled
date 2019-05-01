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
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/mulDiv.h>
#include <test/jtx/TestSuite.h>
#include <test/jtx/envconfig.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/st.h>
#include <test/jtx.h>
#include <test/jtx/ticket.h>
#include <boost/optional.hpp>
#include <test/jtx/WSClient.h>

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
        std::uint64_t expectedMedFeeLevel = 256 * 500)
    {
        auto const metrics = env.app().getTxQ().getMetrics(*env.current());
        BEAST_EXPECT(metrics.referenceFeeLevel == 256);
        BEAST_EXPECT(metrics.txCount == expectedCount);
        BEAST_EXPECT(metrics.txQMaxSize == expectedMaxCount);
        BEAST_EXPECT(metrics.txInLedger == expectedInLedger);
        BEAST_EXPECT(metrics.txPerLedger == expectedPerLedger);
        BEAST_EXPECT(metrics.minProcessingFeeLevel == expectedMinFeeLevel);
        BEAST_EXPECT(metrics.medFeeLevel == expectedMedFeeLevel);
        auto expectedCurFeeLevel = expectedInLedger > expectedPerLedger ?
            expectedMedFeeLevel * expectedInLedger * expectedInLedger /
                (expectedPerLedger * expectedPerLedger) :
                    metrics.referenceFeeLevel;
        BEAST_EXPECT(metrics.openLedgerFeeLevel == expectedCurFeeLevel);
    }

    void
    fillQueue(
        jtx::Env& env,
        jtx::Account const& account)
    {
        auto metrics = env.app().getTxQ().getMetrics(*env.current());
        for (int i = metrics.txInLedger; i <= metrics.txPerLedger; ++i)
            env(noop(account));
    }

    auto
    openLedgerFee(jtx::Env& env)
    {
        using namespace jtx;

        auto const& view = *env.current();
        auto metrics = env.app().getTxQ().getMetrics(view);

        // Don't care about the overflow flag
        return fee(mulDiv(metrics.openLedgerFeeLevel,
            view.fees().base, metrics.referenceFeeLevel).second + 1);
    }

    static
    std::unique_ptr<Config>
    makeConfig(std::map<std::string, std::string> extraTxQ = {},
        std::map<std::string, std::string> extraVoting = {})
    {
        auto p = test::jtx::envconfig();
        auto& section = p->section("transaction_queue");
        section.set("ledgers_in_queue", "2");
        section.set("minimum_queue_size", "2");
        section.set("min_ledgers_to_compute_size_limit", "3");
        section.set("max_ledger_counts_to_store", "100");
        section.set("retry_sequence_percent", "25");
        section.set("zero_basefee_transaction_feelevel", "100000000000");
        section.set("normal_consensus_increase_percent", "0");

        for (auto const& value : extraTxQ)
            section.set(value.first, value.second);

        // Some tests specify different fee settings that are enabled by
        // a FeeVote
        if (!extraVoting.empty())
        {

            auto& votingSection = p->section("voting");
            for (auto const & value : extraVoting)
            {
                votingSection.set(value.first, value.second);
            }

            // In order for the vote to occur, we must run as a validator
            p->section("validation_seed").legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
        }
        return p;
    }

    std::size_t
    initFee(jtx::Env& env, std::size_t expectedPerLedger,
        std::size_t ledgersInQueue, std::uint32_t base,
        std::uint32_t units, std::uint32_t reserve, std::uint32_t increment)
    {
        // Run past the flag ledger so that a Fee change vote occurs and
        // lowers the reserve fee. (It also activates all supported
        // amendments.) This will allow creating accounts with lower
        // reserves and balances.
        for(auto i = env.current()->seq(); i <= 257; ++i)
            env.close();
        // The ledger after the flag ledger creates all the
        // fee (1) and amendment (supportedAmendments().size())
        // pseudotransactions. They all have 0 fee, which is
        // treated as a high fee level by the queue, so the
        // medianFeeLevel is 100000000000.
        auto const flagPerLedger = 1 +
            ripple::detail::supportedAmendments().size();
        auto const flagMaxQueue = ledgersInQueue * flagPerLedger;
        checkMetrics(env, 0, flagMaxQueue, 0, flagPerLedger, 256,
            100000000000);

        // Pad a couple of txs with normal fees so the median comes
        // back down to normal
        env(noop(env.master));
        env(noop(env.master));

        // Close the ledger with a delay, which causes all the TxQ
        // metrics to reset to defaults, EXCEPT the maxQueue size.
        using namespace std::chrono_literals;
        env.close(env.now() + 5s, 10000ms);
        checkMetrics(env, 0, flagMaxQueue, 0, expectedPerLedger, 256);
        auto const fees = env.current()->fees();
        BEAST_EXPECT(fees.base == base);
        BEAST_EXPECT(fees.units == units);
        BEAST_EXPECT(fees.reserve == reserve);
        BEAST_EXPECT(fees.increment == increment);

        return flagMaxQueue;
    }

public:
    void testQueue()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            makeConfig({ {"minimum_txn_in_ledger_standalone", "3"} }));
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

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 3, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 3, 256);

        // Alice - price starts exploding: held
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 4, 3, 256);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee(env));
        checkMetrics(env, 1, boost::none, 5, 3, 256);

        // Daria with low fee: hold
        env(noop(daria), fee(1000), queued);
        checkMetrics(env, 2, boost::none, 5, 3, 256);

        env.close();
        // Verify that the held transactions got applied
        checkMetrics(env, 0, 10, 2, 5, 256);

        //////////////////////////////////////////////////////////////

        // Make some more accounts. We'll need them later to abuse the queue.
        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(env, 0, 10, 6, 5, 256);

        // Now get a bunch of transactions held.
        env(noop(alice), fee(12), queued);
        checkMetrics(env, 1, 10, 6, 5, 256);

        env(noop(bob), fee(10), queued); // won't clear the queue
        env(noop(charlie), fee(20), queued);
        env(noop(daria), fee(15), queued);
        env(noop(elmo), fee(11), queued);
        env(noop(fred), fee(19), queued);
        env(noop(gwen), fee(16), queued);
        env(noop(hank), fee(18), queued);
        checkMetrics(env, 8, 10, 6, 5, 256);

        env.close();
        // Verify that the held transactions got applied
        checkMetrics(env, 1, 12, 7, 6, 256);

        // Bob's transaction is still stuck in the queue.

        //////////////////////////////////////////////////////////////

        // Hank sends another txn
        env(noop(hank), fee(10), queued);
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256);

        // Hank sees his txn  got held and bumps the fee,
        // but doesn't even bump it enough to requeue
        env(noop(hank), fee(11), ter(telCAN_NOT_QUEUE_FEE));
        checkMetrics(env, 2, 12, 7, 6, 256);

        // Hank sees his txn got held and bumps the fee,
        // enough to requeue, but doesn't bump it enough to
        // apply to the ledger
        env(noop(hank), fee(6000), queued);
        // But he's not going to leave it in the queue
        checkMetrics(env, 2, 12, 7, 6, 256);

        // Hank sees his txn got held and bumps the fee,
        // high enough to get into the open ledger, because
        // he doesn't want to wait.
        env(noop(hank), openLedgerFee(env));
        checkMetrics(env, 1, 12, 8, 6, 256);

        // Hank then sends another, less important txn
        // (In addition to the metrics, this will verify that
        //  the original txn got removed.)
        env(noop(hank), fee(6000), queued);
        checkMetrics(env, 2, 12, 8, 6, 256);

        env.close();

        // Verify that bob and hank's txns were applied
        checkMetrics(env, 0, 16, 2, 8, 256);

        // Close again with a simulated time leap to
        // reset the escalation limit down to minimum
        env.close(env.now() + 5s, 10000ms);
        checkMetrics(env, 0, 16, 0, 3, 256);
        // Then close once more without the time leap
        // to reset the queue maxsize down to minimum
        env.close();
        checkMetrics(env, 0, 6, 0, 3, 256);

        //////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(hank), fee(7000));
        env(noop(gwen), fee(7000));
        env(noop(fred), fee(7000));
        env(noop(elmo), fee(7000));
        checkMetrics(env, 0, 6, 4, 3, 256);

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
        checkMetrics(env, 6, 6, 4, 3, 385);

        // Try to add another transaction with the default (low) fee,
        // it should fail because the queue is full.
        env(noop(charlie), ter(telCAN_NOT_QUEUE_FULL));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), queued);

        // Queue is still full, of course, but the min fee has gone up
        checkMetrics(env, 6, 6, 4, 3, 410);

        // Close out the ledger, the transactions are accepted, the
        // queue is cleared, then the localTxs are retried. At this
        // point, daria's transaction that was dropped from the queue
        // is put back in. Neat.
        env.close();
        checkMetrics(env, 2, 8, 5, 4, 256, 256 * 700);

        env.close();
        checkMetrics(env, 0, 10, 2, 5, 256);

        //////////////////////////////////////////////////////////////
        // Cleanup:

        // Create a few more transactions, so that
        // we can be sure that there's one in the queue when the
        // test ends and the TxQ is destructed.

        auto metrics = txq.getMetrics(*env.current());
        BEAST_EXPECT(metrics.txCount == 0);

        // Stuff the ledger.
        for (int i = metrics.txInLedger; i <= metrics.txPerLedger; ++i)
        {
            env(noop(env.master));
        }

        // Queue one straightforward transaction
        env(noop(env.master), fee(20), queued);
        ++metrics.txCount;

        checkMetrics(env, metrics.txCount,
            metrics.txQMaxSize, metrics.txPerLedger + 1,
            metrics.txPerLedger,
            256);
    }

    void testTecResult()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }));

        auto alice = Account("alice");
        auto gw = Account("gw");
        auto USD = gw["USD"];

        checkMetrics(env, 0, boost::none, 0, 2, 256);

        // Create accounts
        env.fund(XRP(50000), noripple(alice, gw));
        checkMetrics(env, 0, boost::none, 2, 2, 256);
        env.close();
        checkMetrics(env, 0, 4, 0, 2, 256);

        // Alice creates an unfunded offer while the ledger is not full
        env(offer(alice, XRP(1000), USD(1000)), ter(tecUNFUNDED_OFFER));
        checkMetrics(env, 0, 4, 1, 2, 256);

        fillQueue(env, alice);
        checkMetrics(env, 0, 4, 3, 2, 256);

        // Alice creates an unfunded offer that goes in the queue
        env(offer(alice, XRP(1000), USD(1000)), ter(terQUEUED));
        checkMetrics(env, 1, 4, 3, 2, 256);

        // The offer comes out of the queue
        env.close();
        checkMetrics(env, 0, 6, 1, 3, 256);
    }

    void testLocalTxRetry()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 2, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie));
        checkMetrics(env, 0, boost::none, 3, 2, 256);

        // Future transaction for Alice - fails
        env(noop(alice), openLedgerFee(env),
            seq(env.seq(alice) + 1), ter(terPRE_SEQ));
        checkMetrics(env, 0, boost::none, 3, 2, 256);

        // Current transaction for Alice: held
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256);

        // Alice - sequence is too far ahead, so won't queue.
        env(noop(alice), seq(env.seq(alice) + 2),
            ter(terPRE_SEQ));
        checkMetrics(env, 1, boost::none, 3, 2, 256);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee(env));
        checkMetrics(env, 1, boost::none, 4, 2, 256);

        // Daria with low fee: hold
        env(noop(charlie), fee(1000), queued);
        checkMetrics(env, 2, boost::none, 4, 2, 256);

        // Alice with normal fee: hold
        env(noop(alice), seq(env.seq(alice) + 1),
            queued);
        checkMetrics(env, 3, boost::none, 4, 2, 256);

        env.close();
        // Verify that the held transactions got applied
        // Alice's bad transaction applied from the
        // Local Txs.
        checkMetrics(env, 0, 8, 4, 4, 256);
    }

    void testLastLedgerSeq()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto edgar = Account("edgar");
        auto felicia = Account("felicia");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(charlie, daria));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(edgar, felicia));
        env.close(env.now() + 5s, 10000ms);

        checkMetrics(env, 0, boost::none, 0, 2, 256);
        env(noop(bob));
        env(noop(charlie));
        env(noop(daria));
        checkMetrics(env, 0, boost::none, 3, 2, 256);

        BEAST_EXPECT(env.current()->info().seq == 6);
        // Fail to queue an item with a low LastLedgerSeq
        env(noop(alice), json(R"({"LastLedgerSequence":7})"),
            ter(telCAN_NOT_QUEUE));
        // Queue an item with a sufficient LastLedgerSeq.
        env(noop(alice), json(R"({"LastLedgerSequence":8})"),
            queued);
        // Queue items with higher fees to force the previous
        // txn to wait.
        env(noop(bob), fee(7000), queued);
        env(noop(charlie), fee(7000), queued);
        env(noop(daria), fee(7000), queued);
        env(noop(edgar), fee(7000), queued);
        checkMetrics(env, 5, boost::none, 3, 2, 256);
        {
            auto& txQ = env.app().getTxQ();
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            BEAST_EXPECT(aliceStat.size() == 1);
            BEAST_EXPECT(aliceStat.begin()->second.feeLevel == 256);
            BEAST_EXPECT(aliceStat.begin()->second.lastValid &&
                *aliceStat.begin()->second.lastValid == 8);
            BEAST_EXPECT(!aliceStat.begin()->second.consequences);

            auto bobStat = txQ.getAccountTxs(bob.id(), *env.current());
            BEAST_EXPECT(bobStat.size() == 1);
            BEAST_EXPECT(bobStat.begin()->second.feeLevel == 7000 * 256 / 10);
            BEAST_EXPECT(!bobStat.begin()->second.lastValid);
            BEAST_EXPECT(!bobStat.begin()->second.consequences);

            auto noStat = txQ.getAccountTxs(Account::master.id(),
                *env.current());
            BEAST_EXPECT(noStat.empty());
        }

        env.close();
        checkMetrics(env, 1, 6, 4, 3, 256);

        // Keep alice's transaction waiting.
        env(noop(bob), fee(7000), queued);
        env(noop(charlie), fee(7000), queued);
        env(noop(daria), fee(7000), queued);
        env(noop(edgar), fee(7000), queued);
        env(noop(felicia), fee(7000), queued);
        checkMetrics(env, 6, 6, 4, 3, 257);

        env.close();
        // alice's transaction is still hanging around
        checkMetrics(env, 1, 8, 5, 4, 256, 700 * 256);
        BEAST_EXPECT(env.seq(alice) == 1);

        // Keep alice's transaction waiting.
        env(noop(bob), fee(8000), queued);
        env(noop(charlie), fee(8000), queued);
        env(noop(daria), fee(8000), queued);
        env(noop(daria), fee(8000), seq(env.seq(daria) + 1),
            queued);
        env(noop(edgar), fee(8000), queued);
        env(noop(felicia), fee(8000), queued);
        env(noop(felicia), fee(8000), seq(env.seq(felicia) + 1),
            queued);
        checkMetrics(env, 8, 8, 5, 4, 257, 700 * 256);

        env.close();
        // alice's transaction expired without getting
        // into the ledger, so her transaction is gone,
        // though one of felicia's is still in the queue.
        checkMetrics(env, 1, 10, 6, 5, 256, 700 * 256);
        BEAST_EXPECT(env.seq(alice) == 1);

        env.close();
        // And now the queue is empty
        checkMetrics(env, 0, 12, 1, 6, 256, 800 * 256);
        BEAST_EXPECT(env.seq(alice) == 1);
    }

    void testZeroFeeTxn()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto carol = Account("carol");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(carol));
        env.close(env.now() + 5s, 10000ms);

        // Fill the ledger
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(env, 0, boost::none, 3, 2, 256);

        env(noop(bob), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256);

        // Even though this transaction has a 0 fee,
        // SetRegularKey::calculateBaseFee indicates this is
        // a "free" transaction, so it has an "infinite" fee
        // level and goes into the open ledger.
        env(regkey(alice, bob), fee(0));
        checkMetrics(env, 1, boost::none, 4, 2, 256);

        // Close out this ledger so we can get a maxsize
        env.close();
        checkMetrics(env, 0, 8, 1, 4, 256);

        fillQueue(env, bob);
        checkMetrics(env, 0, 8, 5, 4, 256);

        auto feeBob = 30;
        auto seqBob = env.seq(bob);
        for (int i = 0; i < 4; ++i)
        {
            env(noop(bob), fee(feeBob),
                seq(seqBob), queued);
            feeBob = (feeBob + 1) * 125 / 100;
            ++seqBob;
        }
        checkMetrics(env, 4, 8, 5, 4, 256);

        // This transaction also has an "infinite" fee level,
        // but since bob has txns in the queue, it gets queued.
        env(regkey(bob, alice), fee(0),
            seq(seqBob), queued);
        ++seqBob;
        checkMetrics(env, 5, 8, 5, 4, 256);

        // Unfortunately bob can't get any more txns into
        // the queue, because of the multiTxnPercent.
        // TANSTAAFL
        env(noop(bob), fee(XRP(100)),
            seq(seqBob), ter(telINSUF_FEE_P));

        // Carol fills the queue, but can't kick out any
        // transactions.
        auto feeCarol = feeBob;
        auto seqCarol = env.seq(carol);
        for (int i = 0; i < 3; ++i)
        {
            env(noop(carol), fee(feeCarol),
                seq(seqCarol), queued);
            feeCarol = (feeCarol + 1) * 125 / 100;
            ++seqCarol;
        }
        checkMetrics(env, 8, 8, 5, 4, 3 * 256 + 1);

        // Carol doesn't submit high enough to beat Bob's
        // average fee. (Which is ~144,115,188,075,855,907
        // because of the zero fee txn.)
        env(noop(carol), fee(feeCarol),
            seq(seqCarol), ter(telCAN_NOT_QUEUE_FULL));

        env.close();
        // Some of Bob's transactions stay in the queue,
        // and Carol's low fee tx is reapplied from the
        // Local Txs.
        checkMetrics(env, 3, 10, 6, 5, 256);
        BEAST_EXPECT(env.seq(bob) == seqBob - 2);
        BEAST_EXPECT(env.seq(carol) == seqCarol);


        env.close();
        checkMetrics(env, 0, 12, 4, 6, 256);
        BEAST_EXPECT(env.seq(bob) == seqBob + 1);
        BEAST_EXPECT(env.seq(carol) == seqCarol + 1);

        env.close();
        checkMetrics(env, 0, 12, 0, 6, 256);
        BEAST_EXPECT(env.seq(bob) == seqBob + 1);
        BEAST_EXPECT(env.seq(carol) == seqCarol + 1);
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
        env(pay(alice, bob, XRP(-1000)),
            ter(temBAD_AMOUNT));

        // Fail in preclaim
        env(noop(alice), fee(XRP(100000)),
            ter(terINSUF_FEE_B));
    }

    void testQueuedFailure()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "2" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        checkMetrics(env, 0, boost::none, 0, 2, 256);

        env.fund(XRP(1000), noripple(alice, bob));

        checkMetrics(env, 0, boost::none, 2, 2, 256);

        // Fill the ledger
        env(noop(alice));
        checkMetrics(env, 0, boost::none, 3, 2, 256);

        // Put a transaction in the queue
        env(noop(alice), queued);
        checkMetrics(env, 1, boost::none, 3, 2, 256);

        // Now cheat, and bypass the queue.
        {
            auto const& jt = env.jt(noop(alice));
            BEAST_EXPECT(jt.stx);

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
        checkMetrics(env, 1, boost::none, 4, 2, 256);

        env.close();
        // Alice's queued transaction failed in TxQ::accept
        // with tefPAST_SEQ
        checkMetrics(env, 0, 8, 0, 4, 256);
    }

    void testMultiTxnPerAccount()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
                {{"account_reserve", "200"}, {"owner_reserve", "50"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 3, 256);

        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 10, 10, 200, 50);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(drops(2000), noripple(alice));
        env.fund(XRP(500000), noripple(bob, charlie, daria));
        checkMetrics(env, 0, initQueueMax, 4, 3, 256);

        // Alice - price starts exploding: held
        env(noop(alice), queued);
        checkMetrics(env, 1, initQueueMax, 4, 3, 256);

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto charlieSeq = env.seq(charlie);

        // Alice - try to queue a second transaction, but leave a gap
        env(noop(alice), seq(aliceSeq + 2), fee(100),
            ter(terPRE_SEQ));
        checkMetrics(env, 1, initQueueMax, 4, 3, 256);

        // Alice - queue a second transaction. Yay.
        env(noop(alice), seq(aliceSeq + 1), fee(13),
            queued);
        checkMetrics(env, 2, initQueueMax, 4, 3, 256);

        // Alice - queue a third transaction. Yay.
        env(noop(alice), seq(aliceSeq + 2), fee(17),
            queued);
        checkMetrics(env, 3, initQueueMax, 4, 3, 256);

        // Bob - queue a transaction
        env(noop(bob), queued);
        checkMetrics(env, 4, initQueueMax, 4, 3, 256);

        // Bob - queue a second transaction
        env(noop(bob), seq(bobSeq + 1), fee(50),
            queued);
        checkMetrics(env, 5, initQueueMax, 4, 3, 256);

        // Charlie - queue a transaction, with a higher fee
        // than default
        env(noop(charlie), fee(15), queued);
        checkMetrics(env, 6, initQueueMax, 4, 3, 256);

        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        BEAST_EXPECT(env.seq(charlie) == charlieSeq);

        env.close();
        // Verify that all of but one of the queued transactions
        // got applied.
        checkMetrics(env, 1, 8, 5, 4, 256);

        // Verify that the stuck transaction is Bob's second.
        // Even though it had a higher fee than Alice's and
        // Charlie's, it didn't get attempted until the fee escalated.
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 3);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        BEAST_EXPECT(env.seq(charlie) == charlieSeq + 1);

        // Alice - fill up the queue
        std::int64_t aliceFee = 20;
        aliceSeq = env.seq(alice);
        auto lastLedgerSeq = env.current()->info().seq + 2;
        for (auto i = 0; i < 7; i++)
        {
            env(noop(alice), seq(aliceSeq),
                json(jss::LastLedgerSequence, lastLedgerSeq + i),
                    fee(aliceFee), queued);
            ++aliceSeq;
        }
        checkMetrics(env, 8, 8, 5, 4, 513);
        {
            auto& txQ = env.app().getTxQ();
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            std::int64_t fee = 20;
            auto seq = env.seq(alice);
            BEAST_EXPECT(aliceStat.size() == 7);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.first == seq);
                BEAST_EXPECT(tx.second.feeLevel == mulDiv(fee, 256, 10).second);
                BEAST_EXPECT(tx.second.lastValid);
                BEAST_EXPECT((tx.second.consequences &&
                    tx.second.consequences->fee == drops(fee) &&
                    tx.second.consequences->potentialSpend == drops(0) &&
                    tx.second.consequences->category == TxConsequences::normal) ||
                    tx.first == env.seq(alice) + 6);
                ++seq;
            }
        }

        // Alice attempts to add another item to the queue,
        // but you can't force your own earlier txn off the
        // queue.
        env(noop(alice), seq(aliceSeq),
            json(jss::LastLedgerSequence, lastLedgerSeq + 7),
                fee(aliceFee), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Charlie - try to add another item to the queue,
        // which fails because fee is lower than Alice's
        // queued average.
        env(noop(charlie), fee(19), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Charlie - add another item to the queue, which
        // causes Alice's last txn to drop
        env(noop(charlie), fee(30), queued);
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Alice - now attempt to add one more to the queue,
        // which fails because the last tx was dropped, so
        // there is no complete chain.
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(terPRE_SEQ));
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Alice wants this tx more than the dropped tx,
        // so resubmits with higher fee, but the queue
        // is full, and her account is the cheapest.
        env(noop(alice), seq(aliceSeq - 1),
            fee(aliceFee), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Try to replace a middle item in the queue
        // without enough fee.
        aliceSeq = env.seq(alice) + 2;
        aliceFee = 25;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telCAN_NOT_QUEUE_FEE));
        checkMetrics(env, 8, 8, 5, 4, 513);

        // Replace a middle item from the queue successfully
        ++aliceFee;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), queued);
        checkMetrics(env, 8, 8, 5, 4, 513);

        env.close();
        // Alice's transactions processed, along with
        // Charlie's, and the lost one is replayed and
        // added back to the queue.
        checkMetrics(env, 4, 10, 6, 5, 256);

        aliceSeq = env.seq(alice) + 1;

        // Try to replace that item with a transaction that will
        // bankrupt Alice. Fails, because an account can't have
        // more than the minimum reserve in flight before the
        // last queued transaction
        aliceFee = env.le(alice)->getFieldAmount(sfBalance).xrp().drops()
            - (59);
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(env, 4, 10, 6, 5, 256);

        // Try to spend more than Alice can afford with all the other txs.
        aliceSeq += 2;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(terINSUF_FEE_B));
        checkMetrics(env, 4, 10, 6, 5, 256);

        // Replace the last queued item with a transaction that will
        // bankrupt Alice
        --aliceFee;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), queued);
        checkMetrics(env, 4, 10, 6, 5, 256);

        // Alice - Attempt to queue a last transaction, but it
        // fails because the fee in flight is too high, before
        // the fee is checked against the balance
        aliceFee /= 5;
        ++aliceSeq;
        env(noop(alice), seq(aliceSeq),
            fee(aliceFee), ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(env, 4, 10, 6, 5, 256);

        env.close();
        // All of Alice's transactions applied.
        checkMetrics(env, 0, 12, 4, 6, 256);

        env.close();
        checkMetrics(env, 0, 12, 0, 6, 256);

        // Alice is broke
        env.require(balance(alice, XRP(0)));
        env(noop(alice), ter(terINSUF_FEE_B));

        // Bob tries to queue up more than the single
        // account limit (10) txs.
        fillQueue(env, bob);
        bobSeq = env.seq(bob);
        checkMetrics(env, 0, 12, 7, 6, 256);
        for (int i = 0; i < 10; ++i)
            env(noop(bob), seq(bobSeq + i), queued);
        checkMetrics(env, 10, 12, 7, 6, 256);
        // Bob hit the single account limit
        env(noop(bob), seq(bobSeq + 10), ter(terPRE_SEQ));
        checkMetrics(env, 10, 12, 7, 6, 256);
        // Bob can replace one of the earlier txs regardless
        // of the limit
        env(noop(bob), seq(bobSeq + 5), fee(20), queued);
        checkMetrics(env, 10, 12, 7, 6, 256);
    }

    void testTieBreaking()
    {
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "4" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto elmo = Account("elmo");
        auto fred = Account("fred");
        auto gwen = Account("gwen");
        auto hank = Account("hank");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 4, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(env, 0, boost::none, 4, 4, 256);

        env.close();
        checkMetrics(env, 0, 8, 0, 4, 256);

        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(env, 0, 8, 4, 4, 256);

        env.close();
        checkMetrics(env, 0, 8, 0, 4, 256);

        //////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(gwen));
        env(noop(hank));
        env(noop(gwen));
        env(noop(fred));
        env(noop(elmo));
        checkMetrics(env, 0, 8, 5, 4, 256);

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
        checkMetrics(env, 8, 8, 5, 4, 385);

        // Try to add another transaction with the default (low) fee,
        // it should fail because it can't replace the one already
        // there.
        env(noop(charlie), ter(telCAN_NOT_QUEUE_FEE));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), seq(charlieSeq + 1), queued);

        // Queue is still full.
        checkMetrics(env, 8, 8, 5, 4, 385);

        // alice, bob, charlie, daria, and elmo's txs
        // are processed out of the queue into the ledger,
        // leaving fred and gwen's txs. hank's tx is
        // retried from localTxs, and put back into the
        // queue.
        env.close();
        checkMetrics(env, 3, 10, 6, 5, 256);

        BEAST_EXPECT(aliceSeq + 1 == env.seq(alice));
        BEAST_EXPECT(bobSeq + 1 == env.seq(bob));
        BEAST_EXPECT(charlieSeq + 2 == env.seq(charlie));
        BEAST_EXPECT(dariaSeq + 1 == env.seq(daria));
        BEAST_EXPECT(elmoSeq + 1 == env.seq(elmo));
        BEAST_EXPECT(fredSeq == env.seq(fred));
        BEAST_EXPECT(gwenSeq == env.seq(gwen));
        BEAST_EXPECT(hankSeq == env.seq(hank));

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
        checkMetrics(env, 10, 10, 6, 5, 385);

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(alice), fee(100), seq(aliceSeq + 3), queued);

        env.close();
        checkMetrics(env, 4, 12, 7, 6, 256);

        BEAST_EXPECT(fredSeq + 1 == env.seq(fred));
        BEAST_EXPECT(gwenSeq + 1 == env.seq(gwen));
        BEAST_EXPECT(hankSeq + 1 == env.seq(hank));
        BEAST_EXPECT(aliceSeq + 4 == env.seq(alice));
        BEAST_EXPECT(bobSeq == env.seq(bob));
        BEAST_EXPECT(charlieSeq == env.seq(charlie));
        BEAST_EXPECT(dariaSeq == env.seq(daria));
        BEAST_EXPECT(elmoSeq == env.seq(elmo));
    }

    void testAcctTxnID()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "1" } }));

        auto alice = Account("alice");

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 1, 256);

        env.fund(XRP(50000), noripple(alice));
        checkMetrics(env, 0, boost::none, 1, 1, 256);

        env(fset(alice, asfAccountTxnID));
        checkMetrics(env, 0, boost::none, 2, 1, 256);

        // Immediately after the fset, the sfAccountTxnID field
        // is still uninitialized, so preflight succeeds here,
        // and this txn fails because it can't be stored in the queue.
        env(noop(alice), json(R"({"AccountTxnID": "0"})"),
            ter(telCAN_NOT_QUEUE));

        checkMetrics(env, 0, boost::none, 2, 1, 256);
        env.close();
        // The failed transaction is retried from LocalTx
        // and succeeds.
        checkMetrics(env, 0, 4, 1, 2, 256);

        env(noop(alice));
        checkMetrics(env, 0, 4, 2, 2, 256);

        env(noop(alice), json(R"({"AccountTxnID": "0"})"),
            ter(tefWRONG_PRIOR));
    }

    void testMaximum()
    {
        using namespace jtx;
        using namespace std::string_literals;

        {
            Env env(*this, makeConfig(
                { {"minimum_txn_in_ledger_standalone", "2"},
                    {"target_txn_in_ledger", "4"},
                        {"maximum_txn_in_ledger", "5"} }));

            auto alice = Account("alice");

            checkMetrics(env, 0, boost::none, 0, 2, 256);

            env.fund(XRP(50000), noripple(alice));
            checkMetrics(env, 0, boost::none, 1, 2, 256);

            for (int i = 0; i < 10; ++i)
                env(noop(alice), openLedgerFee(env));

            checkMetrics(env, 0, boost::none, 11, 2, 256);

            env.close();
            // If not for the maximum, the per ledger would be 11.
            checkMetrics(env, 0, 10, 0, 5, 256, 800025);
        }

        try
        {
            Env env(*this, makeConfig(
                { {"minimum_txn_in_ledger", "200"},
                    {"minimum_txn_in_ledger_standalone", "200"},
                        {"target_txn_in_ledger", "4"},
                            {"maximum_txn_in_ledger", "5"} }));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s
            );
        }
        try
        {
            Env env(*this, makeConfig(
                { {"minimum_txn_in_ledger", "200"},
                    {"minimum_txn_in_ledger_standalone", "2"},
                        {"target_txn_in_ledger", "4"},
                            {"maximum_txn_in_ledger", "5"} }));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s
            );
        }
        try
        {
            Env env(*this, makeConfig(
                { {"minimum_txn_in_ledger", "2"},
                    {"minimum_txn_in_ledger_standalone", "200"},
                        {"target_txn_in_ledger", "4"},
                            {"maximum_txn_in_ledger", "5"} }));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger_standalone) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s
            );
        }
    }

    void testUnexpectedBalanceChange()
    {
        using namespace jtx;

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
                {{"account_reserve", "200"}, {"owner_reserve", "50"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 10, 10, 200, 50);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, initQueueMax, 0, 3, 256);

        env.fund(drops(5000), noripple(alice));
        env.fund(XRP(50000), noripple(bob));
        checkMetrics(env, 0, initQueueMax, 2, 3, 256);
        auto USD = bob["USD"];

        env(offer(alice, USD(5000), drops(5000)), require(owners(alice, 1)));
        checkMetrics(env, 0, initQueueMax, 3, 3, 256);

        env.close();
        checkMetrics(env, 0, 6, 0, 3, 256);

        // Fill up the ledger
        fillQueue(env, alice);
        checkMetrics(env, 0, 6, 4, 3, 256);

        // Queue up a couple of transactions, plus one
        // more expensive one.
        auto aliceSeq = env.seq(alice);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), fee(drops(1000)),
            seq(aliceSeq), queued);
        checkMetrics(env, 4, 6, 4, 3, 256);

        // This offer should take Alice's offer
        // up to Alice's reserve.
        env(offer(bob, drops(5000), USD(5000)),
            openLedgerFee(env), require(balance(alice, drops(250)),
                owners(alice, 1), lines(alice, 1)));
        checkMetrics(env, 4, 6, 5, 3, 256);

        // Try adding a new transaction.
        // Too many fees in flight.
        env(noop(alice), fee(drops(200)), seq(aliceSeq+1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(env, 4, 6, 5, 3, 256);

        // Close the ledger. All of Alice's transactions
        // take a fee, except the last one.
        env.close();
        checkMetrics(env, 1, 10, 3, 5, 256);
        env.require(balance(alice, drops(250 - 30)));

        // Still can't add a new transaction for Alice,
        // no matter the fee.
        env(noop(alice), fee(drops(200)), seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(env, 1, 10, 3, 5, 256);

        /* At this point, Alice's transaction is indefinitely
            stuck in the queue. Eventually it will either
            expire, get forced off the end by more valuable
            transactions, get replaced by Alice, or Alice
            will get more XRP, and it'll process.
        */

        for (int i = 0; i < 9; ++i)
        {
            env.close();
            checkMetrics(env, 1, 10, 0, 5, 256);
        }

        // And Alice's transaction expires (via the retry limit,
        // not LastLedgerSequence).
        env.close();
        checkMetrics(env, 0, 10, 0, 5, 256);
    }

    void testBlockers()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(env, 0, boost::none, 0, 3, 256);

        env.fund(XRP(50000), noripple(alice, bob));
        env.memoize(charlie);
        env.memoize(daria);
        checkMetrics(env, 0, boost::none, 2, 3, 256);

        // Fill up the open ledger
        env(noop(alice));
        // Set a regular key just to clear the password spent flag
        env(regkey(alice, charlie));
        checkMetrics(env, 0, boost::none, 4, 3, 256);

        // Put some "normal" txs in the queue
        auto aliceSeq = env.seq(alice);
        env(noop(alice), queued);
        env(noop(alice), seq(aliceSeq + 1), queued);
        env(noop(alice), seq(aliceSeq + 2), queued);

        // Can't replace the first tx with a blocker
        env(fset(alice, asfAccountTxnID), fee(20), ter(telCAN_NOT_QUEUE_BLOCKS));
        // Can't replace the second / middle tx with a blocker
        env(regkey(alice, bob), seq(aliceSeq + 1), fee(20),
            ter(telCAN_NOT_QUEUE_BLOCKS));
        env(signers(alice, 2, { {bob}, {charlie}, {daria} }), fee(20),
            seq(aliceSeq + 1), ter(telCAN_NOT_QUEUE_BLOCKS));
        // CAN replace the last tx with a blocker
        env(signers(alice, 2, { { bob },{ charlie },{ daria } }), fee(20),
            seq(aliceSeq + 2), queued);
        env(regkey(alice, bob), seq(aliceSeq + 2), fee(30),
            queued);

        // Can't queue up any more transactions after the blocker
        env(noop(alice), seq(aliceSeq + 3), ter(telCAN_NOT_QUEUE_BLOCKED));

        // Other accounts are not affected
        env(noop(bob), queued);

        // Can replace the txs before the blocker
        env(noop(alice), fee(14), queued);

        // Can replace the blocker itself
        env(noop(alice), seq(aliceSeq + 2), fee(40), queued);

        // And now there's no block.
        env(noop(alice), seq(aliceSeq + 3), queued);
    }

    void testInFlightBalance()
    {
        using namespace jtx;
        testcase("In-flight balance checks");

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } },
            {{"account_reserve", "200"}, {"owner_reserve", "50"}}));

        auto alice = Account("alice");
        auto charlie = Account("charlie");
        auto gw = Account("gw");

        auto queued = ter(terQUEUED);

        // Set the fee reserves _really_ low so transactions with fees
        // in the ballpark of the reserves can be queued. With default
        // reserves, a couple hundred transactions would have to be
        // queued before the open ledger fee approached the reserve,
        // which would unnecessarily slow down this test.
        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 10, 10, 200, 50);

        auto limit = 3;

        checkMetrics(env, 0, initQueueMax, 0, limit, 256);

        env.fund(XRP(50000), noripple(alice, charlie), gw);
        checkMetrics(env, 0, initQueueMax, limit + 1, limit, 256);

        auto USD = gw["USD"];
        auto BUX = gw["BUX"];

        //////////////////////////////////////////
        // Offer with high XRP out and low fee doesn't block
        auto aliceSeq = env.seq(alice);
        auto aliceBal = env.balance(alice);

        env.require(balance(alice, XRP(50000)),
            owners(alice, 0));

        // If this offer crosses, all of alice's
        // XRP will be taken (except the reserve).
        env(offer(alice, BUX(5000), XRP(50000)),
            queued);
        checkMetrics(env, 1, initQueueMax, limit + 1, limit, 256);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, initQueueMax, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit*2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(20)),
            owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with high XRP out and high total fee blocks later txs
        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        env.require(owners(alice, 0));

        // Alice creates an offer with a fee of half the reserve
        env(offer(alice, BUX(5000), XRP(50000)), fee(drops(100)),
            queued);
        checkMetrics(env, 1, limit * 2, limit + 1, limit, 256);

        // Alice creates another offer with a fee
        // that brings the total to just shy of the reserve
        env(noop(alice), fee(drops(99)), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice), fee(drops(51)), seq(aliceSeq + 2),
            ter(terINSUF_FEE_B));
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 3, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(250)),
            owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with high XRP out and super high fee blocks later txs
        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        env.require(owners(alice, 0));

        // Alice creates an offer with a fee larger than the reserve
        // This one can queue because it's the first in the queue for alice
        env(offer(alice, BUX(5000), XRP(50000)), fee(drops(300)),
            queued);
        checkMetrics(env, 1, limit * 2, limit + 1, limit, 256);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice), fee(drops(51)), seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(env, 1, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(351)),
            owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with low XRP out allows later txs
        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this offer crosses, just a bit
        // of alice's XRP will be taken.
        env(offer(alice, BUX(50), XRP(500)),
            queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(20)),
            owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Large XRP payment doesn't block later txs
        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this payment succeeds, alice will
        // send her entire balance to charlie
        // (minus the reserve).
        env(pay(alice, charlie, XRP(50000)),
            queued);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // still has most of her balance, because the
        // payment was unfunded!
        env.require(balance(alice, aliceBal - drops(20)),
            owners(alice, 0));

        //////////////////////////////////////////
        // Small XRP payment allows later txs
        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this payment succeeds, alice will
        // send just a bit of balance to charlie
        env(pay(alice, charlie, XRP(500)),
            queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // The payment succeeds
        env.require(balance(alice, aliceBal - XRP(500) - drops(20)),
            owners(alice, 0));

        //////////////////////////////////////////
        // Large IOU payment allows later txs
        auto const amount = USD(500000);
        env(trust(alice, USD(50000000)));
        env(trust(charlie, USD(50000000)));
        checkMetrics(env, 0, limit * 2, 4, limit, 256);
        // Close so we don't have to deal
        // with tx ordering in consensus.
        env.close();

        env(pay(gw, alice, amount));
        checkMetrics(env, 0, limit * 2, 1, limit, 256);
        // Close so we don't have to deal
        // with tx ordering in consensus.
        env.close();

        fillQueue(env, alice);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

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
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice has her
        // XRP balance, but her USD balance went to charlie.
        env.require(balance(alice, aliceBal - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, aliceUSD),
            owners(alice, 1),
            owners(charlie, 1));

        //////////////////////////////////////////
        // Large XRP to IOU payment doesn't block later txs.

        env(offer(gw, XRP(500000), USD(50000)));
        // Close so we don't have to deal
        // with tx ordering in consensus.
        env.close();

        fillQueue(env, charlie);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        auto charlieUSD = env.balance(charlie, USD);

        // If this payment succeeds, and uses the
        // entire sendMax, alice will send her
        // entire XRP balance to charlie in the
        // form of USD.
        BEAST_EXPECT(XRP(60000) > aliceBal);
        env(pay(alice, charlie, USD(1000)),
            sendmax(XRP(60000)), queued);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice sent a payment
        // to charlie using only a portion of her XRP balance
        env.require(balance(alice, aliceBal - XRP(10000) - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, charlieUSD + USD(1000)),
            owners(alice, 1),
            owners(charlie, 1));

        //////////////////////////////////////////
        // Small XRP to IOU payment allows later txs.

        fillQueue(env, charlie);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        charlieUSD = env.balance(charlie, USD);

        // If this payment succeeds, and uses the
        // entire sendMax, alice will only send
        // a portion of her XRP balance to charlie
        // in the form of USD.
        BEAST_EXPECT(aliceBal > XRP(6001));
        env(pay(alice, charlie, USD(500)),
            sendmax(XRP(6000)), queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice sent a payment
        // to charlie using only a portion of her XRP balance
        env.require(balance(alice, aliceBal - XRP(5000) - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, charlieUSD + USD(500)),
            owners(alice, 1),
            owners(charlie, 1));

        //////////////////////////////////////////
        // Edge case: what happens if the balance is below the reserve?
        env(noop(alice), fee(env.balance(alice) - drops(30)));
        env.close();

        fillQueue(env, charlie);
        checkMetrics(env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        BEAST_EXPECT(aliceBal == drops(30));

        env(noop(alice), fee(drops(25)), queued);
        env(noop(alice), seq(aliceSeq + 1), ter(terINSUF_FEE_B));
        BEAST_EXPECT(env.balance(alice) == drops(30));

        checkMetrics(env, 1, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(env, 0, limit * 2, 1, limit, 256);
        BEAST_EXPECT(env.balance(alice) == drops(5));

    }

    void testConsequences()
    {
        using namespace jtx;
        using namespace std::chrono;
        Env env(*this, supported_amendments().set(featureTickets));
        auto const alice = Account("alice");
        env.memoize(alice);
        env.memoize("bob");
        env.memoize("carol");
        {
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = 3;
            cancelOffer[jss::TransactionType] = jss::OfferCancel;
            auto const jtx = env.jt(cancelOffer,
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }

        {
            auto USD = alice["USD"];

            auto const jtx = env.jt(trust("carol", USD(50000000)),
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }

        {
            auto const jtx = env.jt(ticket::create(alice, "bob", 60),
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }

        {
            Json::Value cancelTicket;
            cancelTicket[jss::Account] = alice.human();
            cancelTicket["TicketID"] = to_string(uint256());
            cancelTicket[jss::TransactionType] = jss::TicketCancel;
            auto const jtx = env.jt(cancelTicket,
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }
    }

    void testRPC()
    {
        using namespace jtx;
        Env env(*this);

        auto fee = env.rpc("fee");

        if (BEAST_EXPECT(fee.isMember(jss::result)) &&
            BEAST_EXPECT(!RPC::contains_error(fee[jss::result])))
        {
            auto const& result = fee[jss::result];
            BEAST_EXPECT(result.isMember(jss::ledger_current_index)
                && result[jss::ledger_current_index] == 3);
            BEAST_EXPECT(result.isMember(jss::current_ledger_size));
            BEAST_EXPECT(result.isMember(jss::current_queue_size));
            BEAST_EXPECT(result.isMember(jss::expected_ledger_size));
            BEAST_EXPECT(!result.isMember(jss::max_queue_size));
            BEAST_EXPECT(result.isMember(jss::drops));
            auto const& drops = result[jss::drops];
            BEAST_EXPECT(drops.isMember(jss::base_fee));
            BEAST_EXPECT(drops.isMember(jss::median_fee));
            BEAST_EXPECT(drops.isMember(jss::minimum_fee));
            BEAST_EXPECT(drops.isMember(jss::open_ledger_fee));
            BEAST_EXPECT(result.isMember(jss::levels));
            auto const& levels = result[jss::levels];
            BEAST_EXPECT(levels.isMember(jss::median_level));
            BEAST_EXPECT(levels.isMember(jss::minimum_level));
            BEAST_EXPECT(levels.isMember(jss::open_ledger_level));
            BEAST_EXPECT(levels.isMember(jss::reference_level));
        }

        env.close();

        fee = env.rpc("fee");

        if (BEAST_EXPECT(fee.isMember(jss::result)) &&
            BEAST_EXPECT(!RPC::contains_error(fee[jss::result])))
        {
            auto const& result = fee[jss::result];
            BEAST_EXPECT(result.isMember(jss::ledger_current_index)
                && result[jss::ledger_current_index] == 4);
            BEAST_EXPECT(result.isMember(jss::current_ledger_size));
            BEAST_EXPECT(result.isMember(jss::current_queue_size));
            BEAST_EXPECT(result.isMember(jss::expected_ledger_size));
            BEAST_EXPECT(result.isMember(jss::max_queue_size));
            auto const& drops = result[jss::drops];
            BEAST_EXPECT(drops.isMember(jss::base_fee));
            BEAST_EXPECT(drops.isMember(jss::median_fee));
            BEAST_EXPECT(drops.isMember(jss::minimum_fee));
            BEAST_EXPECT(drops.isMember(jss::open_ledger_fee));
            BEAST_EXPECT(result.isMember(jss::levels));
            auto const& levels = result[jss::levels];
            BEAST_EXPECT(levels.isMember(jss::median_level));
            BEAST_EXPECT(levels.isMember(jss::minimum_level));
            BEAST_EXPECT(levels.isMember(jss::open_ledger_level));
            BEAST_EXPECT(levels.isMember(jss::reference_level));
        }
    }

    void testExpirationReplacement()
    {
        /* This test is based on a reported regression where a
            replacement candidate transaction found the tx it was trying
            to replace did not have `consequences` set

            Hypothesis: The queue had '22 through '25. At some point(s),
            both the original '22 and '23 expired and were removed from
            the queue. A second '22 was submitted, and the multi-tx logic
            did not kick in, because it matched the account's sequence
            number (a_seq == t_seq). The third '22 was submitted and found
            the '22 in the queue did not have consequences.
        */
        using namespace jtx;

        Env env(*this, makeConfig({ { "minimum_txn_in_ledger_standalone", "1" },
            {"ledgers_in_queue", "10"}, {"maximum_txn_per_account", "20"} }));

        // Alice will recreate the scenario. Bob will block.
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(500000), noripple(alice, bob));
        checkMetrics(env, 0, boost::none, 2, 1, 256);

        auto const aliceSeq = env.seq(alice);
        BEAST_EXPECT(env.current()->info().seq == 3);
        env(noop(alice), seq(aliceSeq), json(R"({"LastLedgerSequence":5})"), ter(terQUEUED));
        env(noop(alice), seq(aliceSeq + 1), json(R"({"LastLedgerSequence":5})"), ter(terQUEUED));
        env(noop(alice), seq(aliceSeq + 2), json(R"({"LastLedgerSequence":10})"), ter(terQUEUED));
        env(noop(alice), seq(aliceSeq + 3), json(R"({"LastLedgerSequence":11})"), ter(terQUEUED));
        checkMetrics(env, 4, boost::none, 2, 1, 256);
        auto const bobSeq = env.seq(bob);
        // Ledger 4 gets 3,
        // Ledger 5 gets 4,
        // Ledger 6 gets 5.
        for (int i = 0; i < 3 + 4 + 5; ++i)
        {
            env(noop(bob), seq(bobSeq + i), fee(200), ter(terQUEUED));
        }
        checkMetrics(env, 4 + 3 + 4 + 5, boost::none, 2, 1, 256);
        // Close ledger 3
        env.close();
        checkMetrics(env, 4 + 4 + 5, 20, 3, 2, 256);
        // Close ledger 4
        env.close();
        checkMetrics(env, 4 + 5, 30, 4, 3, 256);
        // Close ledger 5
        env.close();
        // Alice's first two txs expired.
        checkMetrics(env, 2, 40, 5, 4, 256);

        // Because aliceSeq is missing, aliceSeq + 1 fails
        env(noop(alice), seq(aliceSeq + 1), ter(terPRE_SEQ));

        // Queue up a new aliceSeq tx.
        // This will only do some of the multiTx validation to
        // improve the chances that the orphaned txs can be
        // recovered. Because the cost of relaying the later txs
        // has already been paid, this tx could potentially be a
        // blocker.
        env(fset(alice, asfAccountTxnID), seq(aliceSeq), ter(terQUEUED));
        checkMetrics(env, 3, 40, 5, 4, 256);

        // Even though consequences were not computed, we can replace it.
        env(noop(alice), seq(aliceSeq), fee(20), ter(terQUEUED));
        checkMetrics(env, 3, 40, 5, 4, 256);

        // Queue up a new aliceSeq + 1 tx.
        // This tx will also only do some of the multiTx validation.
        env(fset(alice, asfAccountTxnID), seq(aliceSeq + 1), ter(terQUEUED));
        checkMetrics(env, 4, 40, 5, 4, 256);

        // Even though consequences were not computed, we can replace it,
        // too.
        env(noop(alice), seq(aliceSeq +1), fee(20), ter(terQUEUED));
        checkMetrics(env, 4, 40, 5, 4, 256);

        // Close ledger 6
        env.close();
        // We expect that all of alice's queued tx's got into
        // the open ledger.
        checkMetrics(env, 0, 50, 4, 5, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 4);
    }

    void testSignAndSubmitSequence()
    {
        testcase("Autofilled sequence should account for TxQ");
        using namespace jtx;
        Env env(*this,
            makeConfig({ {"minimum_txn_in_ledger_standalone", "6"} }));
        Env_ss envs(env);
        auto const& txQ = env.app().getTxQ();

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(100000), alice, bob);

        fillQueue(env, alice);
        checkMetrics(env, 0, boost::none, 7, 6, 256);

        // Queue up several transactions for alice sign-and-submit
        auto const aliceSeq = env.seq(alice);
        auto const lastLedgerSeq = env.current()->info().seq + 2;

        auto submitParams = Json::Value(Json::objectValue);
        for (int i = 0; i < 5; ++i)
        {
            if (i == 2)
                envs(noop(alice), fee(1000), seq(none),
                    json(jss::LastLedgerSequence, lastLedgerSeq),
                        ter(terQUEUED))(submitParams);
            else
                envs(noop(alice), fee(1000), seq(none),
                    ter(terQUEUED))(submitParams);
        }
        checkMetrics(env, 5, boost::none, 7, 6, 256);
        {
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            auto seq = aliceSeq;
            BEAST_EXPECT(aliceStat.size() == 5);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.first == seq);
                BEAST_EXPECT(tx.second.feeLevel == 25600);
                if(seq == aliceSeq + 2)
                {
                    BEAST_EXPECT(tx.second.lastValid &&
                        *tx.second.lastValid == lastLedgerSeq);
                }
                else
                {
                    BEAST_EXPECT(!tx.second.lastValid);
                }
                ++seq;
            }
        }
        // Put some txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        for (int i = 0; i < 8; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(env, 13, boost::none, 7, 6, 256);

        env.close();
        checkMetrics(env, 5, 14, 8, 7, 256);
        // Put some more txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        fillQueue(env, bob);
        for(int i = 0; i < 9; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(env, 14, 14, 8, 7, 25601);
        env.close();
        // Put some more txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        fillQueue(env, bob);
        for (int i = 0; i < 10; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(env, 15, 16, 9, 8, 256);
        env.close();
        checkMetrics(env, 4, 18, 10, 9, 256);
        {
            // Bob has nothing left in the queue.
            auto bobStat = txQ.getAccountTxs(bob.id(), *env.current());
            BEAST_EXPECT(bobStat.empty());
        }
        // Verify alice's tx got dropped as we BEAST_EXPECT, and that there's
        // a gap in her queued txs.
        {
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            auto seq = aliceSeq;
            BEAST_EXPECT(aliceStat.size() == 4);
            for (auto const& tx : aliceStat)
            {
                // Skip over the missing one.
                if (seq == aliceSeq + 2)
                    ++seq;

                BEAST_EXPECT(tx.first == seq);
                BEAST_EXPECT(tx.second.feeLevel == 25600);
                BEAST_EXPECT(!tx.second.lastValid);
                ++seq;
            }
        }
        // Now, fill the gap.
        envs(noop(alice), fee(1000), seq(none), ter(terQUEUED))(submitParams);
        checkMetrics(env, 5, 18, 10, 9, 256);
        {
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            auto seq = aliceSeq;
            BEAST_EXPECT(aliceStat.size() == 5);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.first == seq);
                BEAST_EXPECT(tx.second.feeLevel == 25600);
                BEAST_EXPECT(!tx.second.lastValid);
                ++seq;
            }
        }

        env.close();
        checkMetrics(env, 0, 20, 5, 10, 256);
        {
            // Bob's data has been cleaned up.
            auto bobStat = txQ.getAccountTxs(bob.id(), *env.current());
            BEAST_EXPECT(bobStat.empty());
        }
        {
            auto aliceStat = txQ.getAccountTxs(alice.id(), *env.current());
            BEAST_EXPECT(aliceStat.empty());
        }
    }

    void testAccountInfo()
    {
        using namespace jtx;
        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }));
        Env_ss envs(env);

        Account const alice{ "alice" };
        env.fund(XRP(1000000), alice);
        env.close();

        auto const withQueue =
            R"({ "account": ")" + alice.human() +
            R"(", "queue": true })";
        auto const withoutQueue =
            R"({ "account": ")" + alice.human() +
            R"("})";
        auto const prevLedgerWithQueue =
            R"({ "account": ")" + alice.human() +
            R"(", "queue": true, "ledger_index": 3 })";
        BEAST_EXPECT(env.current()->info().seq > 3);

        {
            // account_info without the "queue" argument.
            auto const info = env.rpc("json", "account_info", withoutQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result].isMember(jss::queue_data));
        }
        {
            // account_info with the "queue" argument.
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 0);
            BEAST_EXPECT(!queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(!queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(!queue_data.isMember(jss::transactions));
        }
        checkMetrics(env, 0, 6, 0, 3, 256);

        fillQueue(env, alice);
        checkMetrics(env, 0, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 0);
            BEAST_EXPECT(!queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(!queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(!queue_data.isMember(jss::transactions));
        }

        auto submitParams = Json::Value(Json::objectValue);
        envs(noop(alice), fee(100), seq(none), ter(terQUEUED))(submitParams);
        envs(noop(alice), fee(100), seq(none), ter(terQUEUED))(submitParams);
        envs(noop(alice), fee(100), seq(none), ter(terQUEUED))(submitParams);
        envs(noop(alice), fee(100), seq(none), ter(terQUEUED))(submitParams);
        checkMetrics(env, 4, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 4);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(!queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(!queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data.isMember(jss::transactions));
            auto const& queued = queue_data[jss::transactions];
            BEAST_EXPECT(queued.size() == queue_data[jss::txn_count]);
            for (unsigned i = 0; i < queued.size(); ++i)
            {
                auto const& item = queued[i];
                BEAST_EXPECT(item[jss::seq] == data[jss::Sequence].asInt() + i);
                BEAST_EXPECT(item[jss::fee_level] == "2560");
                BEAST_EXPECT(!item.isMember(jss::LastLedgerSequence));

                if (i == queued.size() - 1)
                {
                    BEAST_EXPECT(!item.isMember(jss::fee));
                    BEAST_EXPECT(!item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(!item.isMember(jss::auth_change));
                }
                else
                {
                    BEAST_EXPECT(item.isMember(jss::fee));
                    BEAST_EXPECT(item[jss::fee] == "100");
                    BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                    BEAST_EXPECT(item.isMember(jss::auth_change));
                    BEAST_EXPECT(!item[jss::auth_change].asBool());
                }

            }
        }

        // Queue up a blocker
        envs(fset(alice, asfAccountTxnID), fee(100), seq(none),
            json(jss::LastLedgerSequence, 10),
                ter(terQUEUED))(submitParams);
        checkMetrics(env, 5, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 5);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(!queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(!queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data.isMember(jss::transactions));
            auto const& queued = queue_data[jss::transactions];
            BEAST_EXPECT(queued.size() == queue_data[jss::txn_count]);
            for (unsigned i = 0; i < queued.size(); ++i)
            {
                auto const& item = queued[i];
                BEAST_EXPECT(item[jss::seq] == data[jss::Sequence].asInt() + i);
                BEAST_EXPECT(item[jss::fee_level] == "2560");

                if (i == queued.size() - 1)
                {
                    BEAST_EXPECT(!item.isMember(jss::fee));
                    BEAST_EXPECT(!item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(!item.isMember(jss::auth_change));
                    BEAST_EXPECT(item.isMember(jss::LastLedgerSequence));
                    BEAST_EXPECT(item[jss::LastLedgerSequence] == 10);
                }
                else
                {
                    BEAST_EXPECT(item.isMember(jss::fee));
                    BEAST_EXPECT(item[jss::fee] == "100");
                    BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                    BEAST_EXPECT(item.isMember(jss::auth_change));
                    BEAST_EXPECT(!item[jss::auth_change].asBool());
                    BEAST_EXPECT(!item.isMember(jss::LastLedgerSequence));
                }

            }
        }

        envs(noop(alice), fee(100), seq(none), ter(telCAN_NOT_QUEUE_BLOCKED))(submitParams);
        checkMetrics(env, 5, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 5);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(queue_data[jss::auth_change_queued].asBool());
            BEAST_EXPECT(queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data[jss::max_spend_drops_total] == "500");
            BEAST_EXPECT(queue_data.isMember(jss::transactions));
            auto const& queued = queue_data[jss::transactions];
            BEAST_EXPECT(queued.size() == queue_data[jss::txn_count]);
            for (unsigned i = 0; i < queued.size(); ++i)
            {
                auto const& item = queued[i];
                BEAST_EXPECT(item[jss::seq] == data[jss::Sequence].asInt() + i);
                BEAST_EXPECT(item[jss::fee_level] == "2560");

                if (i == queued.size() - 1)
                {
                    BEAST_EXPECT(item.isMember(jss::fee));
                    BEAST_EXPECT(item[jss::fee] == "100");
                    BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                    BEAST_EXPECT(item.isMember(jss::auth_change));
                    BEAST_EXPECT(item[jss::auth_change].asBool());
                    BEAST_EXPECT(item.isMember(jss::LastLedgerSequence));
                    BEAST_EXPECT(item[jss::LastLedgerSequence] == 10);
                }
                else
                {
                    BEAST_EXPECT(item.isMember(jss::fee));
                    BEAST_EXPECT(item[jss::fee] == "100");
                    BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                    BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                    BEAST_EXPECT(item.isMember(jss::auth_change));
                    BEAST_EXPECT(!item[jss::auth_change].asBool());
                    BEAST_EXPECT(!item.isMember(jss::LastLedgerSequence));
                }

            }
        }

        {
            auto const info = env.rpc("json", "account_info", prevLedgerWithQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                RPC::contains_error(info[jss::result]));
        }

        env.close();
        checkMetrics(env, 1, 8, 5, 4, 256);
        env.close();
        checkMetrics(env, 0, 10, 1, 5, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 0);
            BEAST_EXPECT(!queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(!queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(!queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(!queue_data.isMember(jss::transactions));
        }
    }

    void testServerInfo()
    {
        using namespace jtx;
        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }));
        Env_ss envs(env);

        Account const alice{ "alice" };
        env.fund(XRP(1000000), alice);
        env.close();

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            BEAST_EXPECT(info.isMember(jss::load_factor) &&
                info[jss::load_factor] == 1);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(!info.isMember(jss::load_factor_fee_escalation));
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 256);
            BEAST_EXPECT(state.isMember(jss::load_base) &&
                state[jss::load_base] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        checkMetrics(env, 0, 6, 0, 3, 256);

        fillQueue(env, alice);
        checkMetrics(env, 0, 6, 4, 3, 256);

        auto aliceSeq = env.seq(alice);
        auto submitParams = Json::Value(Json::objectValue);
        for (auto i = 0; i < 4; ++i)
            envs(noop(alice), fee(100), seq(aliceSeq + i),
                ter(terQUEUED))(submitParams);
        checkMetrics(env, 4, 6, 4, 3, 256);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 888.88 &&
                    info[jss::load_factor] < 888.89);
            BEAST_EXPECT(info.isMember(jss::load_factor_server) &&
                info[jss::load_factor_server] == 1);
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                    info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 227555);
            BEAST_EXPECT(state.isMember(jss::load_base) &&
                state[jss::load_base] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.app().getFeeTrack().setRemoteFee(256000);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(info.isMember(jss::load_factor) &&
                info[jss::load_factor] == 1000);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(info.isMember(jss::load_factor_net) &&
                info[jss::load_factor_net] == 1000);
            BEAST_EXPECT(info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                    info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 256000);
            BEAST_EXPECT(state.isMember(jss::load_base) &&
                state[jss::load_base] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256000);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.app().getFeeTrack().setRemoteFee(256);

        // Increase the server load
        for (int i = 0; i < 5; ++i)
            env.app().getFeeTrack().raiseLocalFee();
        BEAST_EXPECT(env.app().getFeeTrack().getLoadFactor() == 625);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 888.88 &&
                    info[jss::load_factor] < 888.89);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 1.
            BEAST_EXPECT(info.isMember(jss::load_factor_server) &&
                info[jss::load_factor_server] > 1.245 &&
                info[jss::load_factor_server] < 2.4415);
            BEAST_EXPECT(info.isMember(jss::load_factor_local) &&
                info[jss::load_factor_local] > 1.245 &&
                info[jss::load_factor_local] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                    info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 227555);
            BEAST_EXPECT(state.isMember(jss::load_base) &&
                state[jss::load_base] == 256);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 256.
            BEAST_EXPECT(state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] >= 320 &&
                state[jss::load_factor_server] <= 625);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.close();

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.

            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 1.
            BEAST_EXPECT(info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 1.245 &&
                info[jss::load_factor] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(info.isMember(jss::load_factor_local) &&
                info[jss::load_factor_local] > 1.245 &&
                info[jss::load_factor_local] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(!info.isMember(jss::load_factor_fee_escalation));
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(state.isMember(jss::load_factor) &&
                state[jss::load_factor] >= 320 &&
                state[jss::load_factor] <= 625);
            BEAST_EXPECT(state.isMember(jss::load_base) &&
                state[jss::load_base] == 256);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 256.
            BEAST_EXPECT(state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] >= 320 &&
                state[jss::load_factor_server] <= 625);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }
    }

    void testServerSubscribe()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }));

        Json::Value stream;
        stream[jss::streams] = Json::arrayValue;
        stream[jss::streams].append("server");
        auto wsc = makeWSClient(env.app().config());
        {
            auto jv = wsc->invoke("subscribe", stream);
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        Account a{"a"}, b{"b"}, c{"c"}, d{"d"}, e{"e"}, f{"f"},
            g{"g"}, h{"h"}, i{"i"};


        // Fund the first few accounts at non escalated fee
        env.fund(XRP(50000), noripple(a,b,c,d));
        checkMetrics(env, 0, boost::none, 4, 3, 256);

        // First transaction establishes the messaging
        using namespace std::chrono_literals;
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
        {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 256 &&
                jv.isMember(jss::load_base) &&
                jv[jss::load_base] == 256 &&
                jv.isMember(jss::load_factor_server) &&
                jv[jss::load_factor_server] == 256 &&
                jv.isMember(jss::load_factor_fee_escalation) &&
                jv[jss::load_factor_fee_escalation] == 256 &&
                jv.isMember(jss::load_factor_fee_queue) &&
                jv[jss::load_factor_fee_queue] == 256 &&
                jv.isMember(jss::load_factor_fee_reference) &&
                jv[jss::load_factor_fee_reference] == 256;
        }));
        // Last transaction escalates the fee
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
        {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 227555 &&
                jv.isMember(jss::load_base) &&
                jv[jss::load_base] == 256 &&
                jv.isMember(jss::load_factor_server) &&
                jv[jss::load_factor_server] == 256 &&
                jv.isMember(jss::load_factor_fee_escalation) &&
                jv[jss::load_factor_fee_escalation] == 227555 &&
                jv.isMember(jss::load_factor_fee_queue) &&
                jv[jss::load_factor_fee_queue] == 256 &&
                jv.isMember(jss::load_factor_fee_reference) &&
                jv[jss::load_factor_fee_reference] == 256;
        }));

        env.close();

        // Closing ledger should publish a status update
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                return jv[jss::type] == "serverStatus" &&
                    jv.isMember(jss::load_factor) &&
                        jv[jss::load_factor] == 256 &&
                    jv.isMember(jss::load_base) &&
                        jv[jss::load_base] == 256 &&
                    jv.isMember(jss::load_factor_server) &&
                        jv[jss::load_factor_server] == 256 &&
                    jv.isMember(jss::load_factor_fee_escalation) &&
                        jv[jss::load_factor_fee_escalation] == 256 &&
                    jv.isMember(jss::load_factor_fee_queue) &&
                        jv[jss::load_factor_fee_queue] == 256 &&
                    jv.isMember(jss::load_factor_fee_reference) &&
                        jv[jss::load_factor_fee_reference] == 256;
            }));

        checkMetrics(env, 0, 8, 0, 4, 256);

        // Fund then next few accounts at non escalated fee
        env.fund(XRP(50000), noripple(e,f,g,h,i));

        // Extra transactions with low fee are queued
        auto queued = ter(terQUEUED);
        env(noop(a), fee(10), queued);
        env(noop(b), fee(10), queued);
        env(noop(c), fee(10), queued);
        env(noop(d), fee(10), queued);
        env(noop(e), fee(10), queued);
        env(noop(f), fee(10), queued);
        env(noop(g), fee(10), queued);
        checkMetrics(env, 7, 8, 5, 4, 256);

        // Last transaction escalates the fee
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
        {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 200000 &&
                jv.isMember(jss::load_base) &&
                jv[jss::load_base] == 256 &&
                jv.isMember(jss::load_factor_server) &&
                jv[jss::load_factor_server] == 256 &&
                jv.isMember(jss::load_factor_fee_escalation) &&
                jv[jss::load_factor_fee_escalation] == 200000 &&
                jv.isMember(jss::load_factor_fee_queue) &&
                jv[jss::load_factor_fee_queue] == 256 &&
                jv.isMember(jss::load_factor_fee_reference) &&
                jv[jss::load_factor_fee_reference] == 256;
        }));

        env.close();
        //  Ledger close publishes with escalated fees for queued transactions
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                return jv[jss::type] == "serverStatus" &&
                    jv.isMember(jss::load_factor) &&
                        jv[jss::load_factor] == 184320 &&
                    jv.isMember(jss::load_base) &&
                        jv[jss::load_base] == 256 &&
                    jv.isMember(jss::load_factor_server) &&
                        jv[jss::load_factor_server] == 256 &&
                    jv.isMember(jss::load_factor_fee_escalation) &&
                        jv[jss::load_factor_fee_escalation] == 184320 &&
                    jv.isMember(jss::load_factor_fee_queue) &&
                        jv[jss::load_factor_fee_queue] == 256 &&
                    jv.isMember(jss::load_factor_fee_reference) &&
                        jv[jss::load_factor_fee_reference] == 256;
            }));

        env.close();
        // ledger close clears queue so fee is back to normal
        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                return jv[jss::type] == "serverStatus" &&
                    jv.isMember(jss::load_factor) &&
                        jv[jss::load_factor] == 256 &&
                    jv.isMember(jss::load_base) &&
                        jv[jss::load_base] == 256 &&
                    jv.isMember(jss::load_factor_server) &&
                        jv[jss::load_factor_server] == 256 &&
                    jv.isMember(jss::load_factor_fee_escalation) &&
                        jv[jss::load_factor_fee_escalation] == 256 &&
                    jv.isMember(jss::load_factor_fee_queue) &&
                        jv[jss::load_factor_fee_queue] == 256 &&
                    jv.isMember(jss::load_factor_fee_reference) &&
                        jv[jss::load_factor_fee_reference] == 256;
            }));

        BEAST_EXPECT(!wsc->findMsg(1s,
            [&](auto const& jv)
            {
                return jv[jss::type] == "serverStatus";
            }));

        auto jv = wsc->invoke("unsubscribe", stream);
        BEAST_EXPECT(jv[jss::status] == "success");

    }

    void testClearQueuedAccountTxs()
    {
        using namespace jtx;

        Env env(*this,
            makeConfig({ { "minimum_txn_in_ledger_standalone", "3" } }));
        auto alice = Account("alice");
        auto bob = Account("bob");

        checkMetrics(env, 0, boost::none, 0, 3, 256);
        env.fund(XRP(50000000), alice, bob);

        fillQueue(env, alice);

        auto calcTotalFee = [&](
            std::int64_t alreadyPaid, boost::optional<std::size_t> numToClear = boost::none)
                -> std::uint64_t {
            auto totalFactor = 0;
            auto const metrics = env.app ().getTxQ ().getMetrics (
                *env.current ());
            if (!numToClear)
                numToClear.emplace(metrics.txCount + 1);
            for (int i = 0; i < *numToClear; ++i)
            {
                auto inLedger = metrics.txInLedger + i;
                totalFactor += inLedger * inLedger;
            }
            auto result =
                mulDiv (metrics.medFeeLevel * totalFactor /
                        (metrics.txPerLedger * metrics.txPerLedger),
                    env.current ()->fees ().base, metrics.referenceFeeLevel)
                    .second;
            // Subtract the fees already paid
            result -= alreadyPaid;
            // round up
            ++result;
            return result;
        };

        testcase("straightfoward positive case");
        {
            // Queue up some transactions at a too-low fee.
            auto aliceSeq = env.seq(alice);
            for (int i = 0; i < 2; ++i)
            {
                env(noop(alice), fee(100), seq(aliceSeq++), ter(terQUEUED));
            }

            // Queue up a transaction paying the open ledger fee
            // This will be the first tx to call the operative function,
            // but it won't succeed.
            env(noop(alice), openLedgerFee(env), seq(aliceSeq++),
                ter(terQUEUED));

            checkMetrics(env, 3, boost::none, 4, 3, 256);

            // Figure out how much it would cost to cover all the
            // queued txs + itself
            std::uint64_t totalFee1 = calcTotalFee (100 * 2 + 8889);
            --totalFee1;

            BEAST_EXPECT(totalFee1 == 60911);
            // Submit a transaction with that fee. It will get queued
            // because the fee level calculation rounds down. This is
            // the edge case test.
            env(noop(alice), fee(totalFee1), seq(aliceSeq++),
                ter(terQUEUED));

            checkMetrics(env, 4, boost::none, 4, 3, 256);

            // Now repeat the process including the new tx
            // and avoiding the rounding error
            std::uint64_t const totalFee2 = calcTotalFee (100 * 2 + 8889 + 60911);
            BEAST_EXPECT(totalFee2 == 35556);
            // Submit a transaction with that fee. It will succeed.
            env(noop(alice), fee(totalFee2), seq(aliceSeq++));

            checkMetrics(env, 0, boost::none, 9, 3, 256);
        }

        testcase("replace last tx with enough to clear queue");
        {
            // Queue up some transactions at a too-low fee.
            auto aliceSeq = env.seq(alice);
            for (int i = 0; i < 2; ++i)
            {
                env(noop(alice), fee(100), seq(aliceSeq++), ter(terQUEUED));
            }

            // Queue up a transaction paying the open ledger fee
            // This will be the first tx to call the operative function,
            // but it won't succeed.
            env(noop(alice), openLedgerFee(env), seq(aliceSeq++),
                ter(terQUEUED));

            checkMetrics(env, 3, boost::none, 9, 3, 256);

            // Figure out how much it would cost to cover all the
            // queued txs + itself
            auto const metrics = env.app ().getTxQ ().getMetrics (
                *env.current ());
            std::uint64_t const totalFee =
                calcTotalFee (100 * 2, metrics.txCount);
            BEAST_EXPECT(totalFee == 167578);
            // Replacing the last tx with the large fee succeeds.
            --aliceSeq;
            env(noop(alice), fee(totalFee), seq(aliceSeq++));

            // The queue is clear
            checkMetrics(env, 0, boost::none, 12, 3, 256);

            env.close();
            checkMetrics(env, 0, 24, 0, 12, 256);
        }

        testcase("replace middle tx with enough to clear queue");
        {
            fillQueue(env, alice);
            // Queue up some transactions at a too-low fee.
            auto aliceSeq = env.seq(alice);
            for (int i = 0; i < 5; ++i)
            {
                env(noop(alice), fee(100), seq(aliceSeq++), ter(terQUEUED));
            }

            checkMetrics(env, 5, 24, 13, 12, 256);

            // Figure out how much it would cost to cover 3 txns
            std::uint64_t const totalFee = calcTotalFee(100 * 2, 3);
            BEAST_EXPECT(totalFee == 20287);
            // Replacing the last tx with the large fee succeeds.
            aliceSeq -= 3;
            env(noop(alice), fee(totalFee), seq(aliceSeq++));

            checkMetrics(env, 2, 24, 16, 12, 256);
            auto const aliceQueue = env.app().getTxQ().getAccountTxs(
                alice.id(), *env.current());
            BEAST_EXPECT(aliceQueue.size() == 2);
            auto seq = aliceSeq;
            for (auto const& tx : aliceQueue)
            {
                BEAST_EXPECT(tx.first == seq);
                BEAST_EXPECT(tx.second.feeLevel == 2560);
                ++seq;
            }

            // Close the ledger to clear the queue
            env.close();
            checkMetrics(env, 0, 32, 2, 16, 256);
        }

        testcase("clear queue failure (load)");
        {
            fillQueue(env, alice);
            // Queue up some transactions at a too-low fee.
            auto aliceSeq = env.seq(alice);
            for (int i = 0; i < 2; ++i)
            {
                env(noop(alice), fee(200), seq(aliceSeq++), ter(terQUEUED));
            }
            for (int i = 0; i < 2; ++i)
            {
                env(noop(alice), fee(22), seq(aliceSeq++), ter(terQUEUED));
            }

            checkMetrics(env, 4, 32, 17, 16, 256);

            // Figure out how much it would cost to cover all the txns
            //  + 1
            std::uint64_t const totalFee = calcTotalFee (200 * 2 + 22 * 2);
            BEAST_EXPECT(totalFee == 35006);
            // This fee should be enough, but oh no! Server load went up!
            auto& feeTrack = env.app().getFeeTrack();
            auto const origFee = feeTrack.getRemoteFee();
            feeTrack.setRemoteFee(origFee * 5);
            // Instead the tx gets queued, and all of the queued
            // txs stay in the queue.
            env(noop(alice), fee(totalFee), seq(aliceSeq++), ter(terQUEUED));

            // The original last transaction is still in the queue
            checkMetrics(env, 5, 32, 17, 16, 256);

            // With high load, some of the txs stay in the queue
            env.close();
            checkMetrics(env, 3, 34, 2, 17, 256);

            // Load drops back down
            feeTrack.setRemoteFee(origFee);

            // Because of the earlier failure, alice can not clear the queue,
            // no matter how high the fee
            fillQueue(env, bob);
            checkMetrics(env, 3, 34, 18, 17, 256);

            env(noop(alice), fee(XRP(1)), seq(aliceSeq++), ter(terQUEUED));
            checkMetrics(env, 4, 34, 18, 17, 256);

            // With normal load, those txs get into the ledger
            env.close();
            checkMetrics(env, 0, 36, 4, 18, 256);
        }
    }

    void
    testScaling()
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        {
            Env env(*this,
                makeConfig({ { "minimum_txn_in_ledger_standalone", "3" },
                    { "normal_consensus_increase_percent", "25" },
                    { "slow_consensus_decrease_percent", "50" },
                    { "target_txn_in_ledger", "10" },
                    { "maximum_txn_per_account", "200" } }));
            auto alice = Account("alice");

            checkMetrics(env, 0, boost::none, 0, 3, 256);
            env.fund(XRP(50000000), alice);

            fillQueue(env, alice);
            checkMetrics(env, 0, boost::none, 4, 3, 256);
            auto seqAlice = env.seq(alice);
            auto txCount = 140;
            for (int i = 0; i < txCount; ++i)
                env(noop(alice), seq(seqAlice++), ter(terQUEUED));
            checkMetrics(env, txCount, boost::none, 4, 3, 256);

            // Close a few ledgers successfully, so the limit grows

            env.close();
            // 4 + 25% = 5
            txCount -= 6;
            checkMetrics(env, txCount, 10, 6, 5, 257);

            env.close();
            // 6 + 25% = 7
            txCount -= 8;
            checkMetrics(env, txCount, 14, 8, 7, 257);

            env.close();
            // 8 + 25% = 10
            txCount -= 11;
            checkMetrics(env, txCount, 20, 11, 10, 257);

            env.close();
            // 11 + 25% = 13
            txCount -= 14;
            checkMetrics(env, txCount, 26, 14, 13, 257);

            env.close();
            // 14 + 25% = 17
            txCount -= 18;
            checkMetrics(env, txCount, 34, 18, 17, 257);

            env.close();
            // 18 + 25% = 22
            txCount -= 23;
            checkMetrics(env, txCount, 44, 23, 22, 257);

            env.close();
            // 23 + 25% = 28
            txCount -= 29;
            checkMetrics(env, txCount, 56, 29, 28, 256);

            // From 3 expected to 28 in 7 "fast" ledgers.

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 15;
            checkMetrics(env, txCount, 56, 15, 14, 256);

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 8;
            checkMetrics(env, txCount, 56, 8, 7, 256);

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(env, txCount, 56, 4, 3, 256);

            // From 28 expected back down to 3 in 3 "slow" ledgers.

            // Confirm the minimum sticks
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(env, txCount, 56, 4, 3, 256);

            BEAST_EXPECT(!txCount);
        }

        {
            Env env(*this,
                makeConfig({ { "minimum_txn_in_ledger_standalone", "3" },
                    { "normal_consensus_increase_percent", "150" },
                    { "slow_consensus_decrease_percent", "150" },
                    { "target_txn_in_ledger", "10" },
                    { "maximum_txn_per_account", "200" } }));
            auto alice = Account("alice");

            checkMetrics(env, 0, boost::none, 0, 3, 256);
            env.fund(XRP(50000000), alice);

            fillQueue(env, alice);
            checkMetrics(env, 0, boost::none, 4, 3, 256);
            auto seqAlice = env.seq(alice);
            auto txCount = 43;
            for (int i = 0; i < txCount; ++i)
                env(noop(alice), seq(seqAlice++), ter(terQUEUED));
            checkMetrics(env, txCount, boost::none, 4, 3, 256);

            // Close a few ledgers successfully, so the limit grows

            env.close();
            // 4 + 150% = 10
            txCount -= 11;
            checkMetrics(env, txCount, 20, 11, 10, 257);

            env.close();
            // 11 + 150% = 27
            txCount -= 28;
            checkMetrics(env, txCount, 54, 28, 27, 256);

            // From 3 expected to 28 in 7 "fast" ledgers.

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(env, txCount, 54, 4, 3, 256);

            // From 28 expected back down to 3 in 3 "slow" ledgers.

            BEAST_EXPECT(!txCount);
        }
    }

    void run() override
    {
        testQueue();
        testLocalTxRetry();
        testLastLedgerSeq();
        testZeroFeeTxn();
        testPreclaimFailures();
        testQueuedFailure();
        testMultiTxnPerAccount();
        testTieBreaking();
        testAcctTxnID();
        testMaximum();
        testUnexpectedBalanceChange();
        testBlockers();
        testInFlightBalance();
        testConsequences();
        testRPC();
        testExpirationReplacement();
        testSignAndSubmitSequence();
        testAccountInfo();
        testServerInfo();
        testServerSubscribe();
        testClearQueuedAccountTxs();
        testScaling();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(TxQ,app,ripple,1);

}
}
