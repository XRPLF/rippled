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
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/st.h>
#include <test/jtx.h>
#include <test/jtx/TestSuite.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/ticket.h>

namespace ripple {

namespace test {

class TxQ1_test : public beast::unit_test::suite
{
    void
    checkMetrics(
        int line,
        jtx::Env& env,
        std::size_t expectedCount,
        std::optional<std::size_t> expectedMaxCount,
        std::size_t expectedInLedger,
        std::size_t expectedPerLedger,
        std::uint64_t expectedMinFeeLevel,
        std::uint64_t expectedMedFeeLevel = 256 * 500)
    {
        FeeLevel64 const expectedMin{expectedMinFeeLevel};
        FeeLevel64 const expectedMed{expectedMedFeeLevel};
        auto const metrics = env.app().getTxQ().getMetrics(*env.current());
        using namespace std::string_literals;

        metrics.referenceFeeLevel == FeeLevel64{256}
            ? pass()
            : fail(
                  "reference: "s +
                      std::to_string(metrics.referenceFeeLevel.value()) +
                      "/256",
                  __FILE__,
                  line);

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

        metrics.txPerLedger == expectedPerLedger
            ? pass()
            : fail(
                  "txPerLedger: "s + std::to_string(metrics.txPerLedger) + "/" +
                      std::to_string(expectedPerLedger),
                  __FILE__,
                  line);

        metrics.minProcessingFeeLevel == expectedMin
            ? pass()
            : fail(
                  "minProcessingFeeLevel: "s +
                      std::to_string(metrics.minProcessingFeeLevel.value()) +
                      "/" + std::to_string(expectedMin.value()),
                  __FILE__,
                  line);

        metrics.medFeeLevel == expectedMed
            ? pass()
            : fail(
                  "medFeeLevel: "s +
                      std::to_string(metrics.medFeeLevel.value()) + "/" +
                      std::to_string(expectedMed.value()),
                  __FILE__,
                  line);

        auto const expectedCurFeeLevel = expectedInLedger > expectedPerLedger
            ? expectedMed * expectedInLedger * expectedInLedger /
                (expectedPerLedger * expectedPerLedger)
            : metrics.referenceFeeLevel;

        metrics.openLedgerFeeLevel == expectedCurFeeLevel
            ? pass()
            : fail(
                  "openLedgerFeeLevel: "s +
                      std::to_string(metrics.openLedgerFeeLevel.value()) + "/" +
                      std::to_string(expectedCurFeeLevel.value()),
                  __FILE__,
                  line);
    }

    void
    fillQueue(jtx::Env& env, jtx::Account const& account)
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
        auto const base = [&view]() {
            auto base = view.fees().base;
            if (!base)
                base += 1;
            return base;
        }();

        // Don't care about the overflow flag
        return fee(toDrops(metrics.openLedgerFeeLevel, base) + 1);
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

        // Some tests specify different fee settings that are enabled by
        // a FeeVote
        if (!extraVoting.empty())
        {
            auto& votingSection = p->section("voting");
            for (auto const& [k, v] : extraVoting)
            {
                votingSection.set(k, v);
            }

            // In order for the vote to occur, we must run as a validator
            p->section("validation_seed")
                .legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
        }
        return p;
    }

    std::size_t
    initFee(
        jtx::Env& env,
        std::size_t expectedPerLedger,
        std::size_t ledgersInQueue,
        std::uint32_t base,
        std::uint32_t reserve,
        std::uint32_t increment)
    {
        // Run past the flag ledger so that a Fee change vote occurs and
        // lowers the reserve fee. (It also activates all supported
        // amendments.) This will allow creating accounts with lower
        // reserves and balances.
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();
        // The ledger after the flag ledger creates all the
        // fee (1) and amendment (numUpVotedAmendments())
        // pseudotransactions. The queue treats the fees on these
        // transactions as though they are ordinary transactions.
        auto const flagPerLedger = 1 + ripple::detail::numUpVotedAmendments();
        auto const flagMaxQueue = ledgersInQueue * flagPerLedger;
        checkMetrics(__LINE__, env, 0, flagMaxQueue, 0, flagPerLedger, 256);

        // Pad a couple of txs with normal fees so the median comes
        // back down to normal
        env(noop(env.master));
        env(noop(env.master));

        // Close the ledger with a delay, which causes all the TxQ
        // metrics to reset to defaults, EXCEPT the maxQueue size.
        using namespace std::chrono_literals;
        env.close(env.now() + 5s, 10000ms);
        checkMetrics(__LINE__, env, 0, flagMaxQueue, 0, expectedPerLedger, 256);
        auto const fees = env.current()->fees();
        BEAST_EXPECT(fees.base == XRPAmount{base});
        BEAST_EXPECT(fees.reserve == XRPAmount{reserve});
        BEAST_EXPECT(fees.increment == XRPAmount{increment});

        return flagMaxQueue;
    }

public:
    void
    testQueueSeq()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("queue sequence");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto elmo = Account("elmo");
        auto fred = Account("fred");
        auto gwen = Account("gwen");
        auto hank = Account("hank");
        auto iris = Account("iris");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        // Alice - price starts exploding: held
        env(noop(alice), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 3, 256);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, std::nullopt, 5, 3, 256);

        // Daria with low fee: hold
        env(noop(daria), fee(1000), queued);
        checkMetrics(__LINE__, env, 2, std::nullopt, 5, 3, 256);

        env.close();
        // Verify that the held transactions got applied
        checkMetrics(__LINE__, env, 0, 10, 2, 5, 256);

        //////////////////////////////////////////////////////////////

        // Make some more accounts. We'll need them later to abuse the queue.
        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(__LINE__, env, 0, 10, 6, 5, 256);

        // Now get a bunch of transactions held.
        env(noop(alice), fee(12), queued);
        checkMetrics(__LINE__, env, 1, 10, 6, 5, 256);

        env(noop(bob), fee(10), queued);  // won't clear the queue
        env(noop(charlie), fee(20), queued);
        env(noop(daria), fee(15), queued);
        env(noop(elmo), fee(11), queued);
        env(noop(fred), fee(19), queued);
        env(noop(gwen), fee(16), queued);
        env(noop(hank), fee(18), queued);
        checkMetrics(__LINE__, env, 8, 10, 6, 5, 256);

        env.close();
        // Verify that the held transactions got applied
        checkMetrics(__LINE__, env, 1, 12, 7, 6, 256);

        // Bob's transaction is still stuck in the queue.

        //////////////////////////////////////////////////////////////

        // Hank sends another txn
        env(noop(hank), fee(10), queued);
        // But he's not going to leave it in the queue
        checkMetrics(__LINE__, env, 2, 12, 7, 6, 256);

        // Hank sees his txn  got held and bumps the fee,
        // but doesn't even bump it enough to requeue
        env(noop(hank), fee(11), ter(telCAN_NOT_QUEUE_FEE));
        checkMetrics(__LINE__, env, 2, 12, 7, 6, 256);

        // Hank sees his txn got held and bumps the fee,
        // enough to requeue, but doesn't bump it enough to
        // apply to the ledger
        env(noop(hank), fee(6000), queued);
        // But he's not going to leave it in the queue
        checkMetrics(__LINE__, env, 2, 12, 7, 6, 256);

        // Hank sees his txn got held and bumps the fee,
        // high enough to get into the open ledger, because
        // he doesn't want to wait.
        env(noop(hank), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, 12, 8, 6, 256);

        // Hank then sends another, less important txn
        // (In addition to the metrics, this will verify that
        //  the original txn got removed.)
        env(noop(hank), fee(6000), queued);
        checkMetrics(__LINE__, env, 2, 12, 8, 6, 256);

        env.close();

        // Verify that bob and hank's txns were applied
        checkMetrics(__LINE__, env, 0, 16, 2, 8, 256);

        // Close again with a simulated time leap to
        // reset the escalation limit down to minimum
        env.close(env.now() + 5s, 10000ms);
        checkMetrics(__LINE__, env, 0, 16, 0, 3, 256);
        // Then close once more without the time leap
        // to reset the queue maxsize down to minimum
        env.close();
        checkMetrics(__LINE__, env, 0, 6, 0, 3, 256);

        //////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(hank), fee(7000));
        env(noop(gwen), fee(7000));
        env(noop(fred), fee(7000));
        env(noop(elmo), fee(7000));
        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

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
        checkMetrics(__LINE__, env, 6, 6, 4, 3, 385);

        // Try to add another transaction with the default (low) fee,
        // it should fail because the queue is full.
        env(noop(charlie), ter(telCAN_NOT_QUEUE_FULL));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), queued);

        // Queue is still full, of course, but the min fee has gone up
        checkMetrics(__LINE__, env, 6, 6, 4, 3, 410);

        // Close out the ledger, the transactions are accepted, the
        // queue is cleared, then the localTxs are retried. At this
        // point, daria's transaction that was dropped from the queue
        // is put back in. Neat.
        env.close();
        checkMetrics(__LINE__, env, 2, 8, 5, 4, 256, 256 * 700);

        env.close();
        checkMetrics(__LINE__, env, 0, 10, 2, 5, 256);

        //////////////////////////////////////////////////////////////

        // Attempt to put a transaction in the queue for an account
        // that is not yet funded.
        env.memoize(iris);

        env(noop(alice));
        env(noop(bob));
        env(noop(charlie));
        env(noop(daria));
        env(pay(alice, iris, XRP(1000)), queued);
        env(noop(iris), seq(1), fee(20), ter(terNO_ACCOUNT));
        checkMetrics(__LINE__, env, 1, 10, 6, 5, 256);

        env.close();
        checkMetrics(__LINE__, env, 0, 12, 1, 6, 256);

        env.require(balance(iris, XRP(1000)));
        BEAST_EXPECT(env.seq(iris) == 11);

        //////////////////////////////////////////////////////////////
        // Cleanup:

        // Create a few more transactions, so that
        // we can be sure that there's one in the queue when the
        // test ends and the TxQ is destructed.

        auto metrics = env.app().getTxQ().getMetrics(*env.current());
        BEAST_EXPECT(metrics.txCount == 0);

        // Stuff the ledger.
        for (int i = metrics.txInLedger; i <= metrics.txPerLedger; ++i)
        {
            env(noop(env.master));
        }

        // Queue one straightforward transaction
        env(noop(env.master), fee(20), queued);
        ++metrics.txCount;

        checkMetrics(
            __LINE__,
            env,
            metrics.txCount,
            metrics.txQMaxSize,
            metrics.txPerLedger + 1,
            metrics.txPerLedger,
            256);
    }

    void
    testQueueTicket()
    {
        using namespace jtx;
        testcase("queue ticket");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        auto alice = Account("alice");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // Fund alice and then fill the ledger.
        env.fund(XRP(50000), noripple(alice));
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        //////////////////////////////////////////////////////////////////

        // Alice requests tickets, but that transaction is queued.  So
        // Alice can't queue ticketed transactions yet.
        std::uint32_t const tkt1{env.seq(alice) + 1};
        env(ticket::create(alice, 250), seq(tkt1 - 1), queued);

        env(noop(alice), ticket::use(tkt1 - 2), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 - 1), ter(terPRE_TICKET));
        env.require(owners(alice, 0), tickets(alice, 0));
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 3, 256);

        env.close();
        env.require(owners(alice, 250), tickets(alice, 250));
        checkMetrics(__LINE__, env, 0, 8, 1, 4, 256);
        BEAST_EXPECT(env.seq(alice) == tkt1 + 250);

        //////////////////////////////////////////////////////////////////

        // Unlike queued sequence-based transactions, ticket-based
        // transactions _do_ move out of the queue largest fee first,
        // even within one account, since they can be applied in any order.
        // Demonstrate that.

        // Fill the ledger so we can start queuing things.
        env(noop(alice), ticket::use(tkt1 + 1), fee(11));
        env(noop(alice), ticket::use(tkt1 + 2), fee(12));
        env(noop(alice), ticket::use(tkt1 + 3), fee(13));
        env(noop(alice), ticket::use(tkt1 + 4), fee(14));
        env(noop(alice), ticket::use(tkt1 + 5), fee(15), queued);
        env(noop(alice), ticket::use(tkt1 + 6), fee(16), queued);
        env(noop(alice), ticket::use(tkt1 + 7), fee(17), queued);
        env(noop(alice), ticket::use(tkt1 + 8), fee(18), queued);
        env(noop(alice), ticket::use(tkt1 + 9), fee(19), queued);
        env(noop(alice), ticket::use(tkt1 + 10), fee(20), queued);
        env(noop(alice), ticket::use(tkt1 + 11), fee(21), queued);
        env(noop(alice), ticket::use(tkt1 + 12), fee(22), queued);
        env(noop(alice),
            ticket::use(tkt1 + 13),
            fee(23),
            ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 385);

        // Check which of the queued transactions got into the ledger by
        // attempting to replace them.
        //  o Get tefNO_TICKET if the ticket has already been used.
        //  o Get telCAN_NOT_QUEUE_FEE if the transaction is still in the queue.
        env.close();
        env.require(owners(alice, 240), tickets(alice, 240));

        // These 4 went straight to the ledger:
        env(noop(alice), ticket::use(tkt1 + 1), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 2), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 3), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 4), ter(tefNO_TICKET));

        // These two are still in the TxQ:
        env(noop(alice), ticket::use(tkt1 + 5), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), ticket::use(tkt1 + 6), ter(telCAN_NOT_QUEUE_FEE));

        // These six were moved from the queue into the open ledger
        // since those with the highest fees go first.
        env(noop(alice), ticket::use(tkt1 + 7), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 8), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 9), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 10), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 11), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 12), ter(tefNO_TICKET));

        // This last one was moved from the local transactions into
        // the queue.
        env(noop(alice), ticket::use(tkt1 + 13), ter(telCAN_NOT_QUEUE_FEE));

        checkMetrics(__LINE__, env, 3, 10, 6, 5, 256);

        //////////////////////////////////////////////////////////////////

        // Do some experiments with putting sequence-based transactions
        // into the queue while there are ticket-based transactions
        // already in the queue.

        // Alice still has three ticket-based transactions in the queue.
        // The fee is escalated so unless we pay a sufficient fee
        // transactions will go straight to the queue.
        std::uint32_t const nextSeq{env.seq(alice)};
        env(noop(alice), seq(nextSeq + 1), ter(terPRE_SEQ));
        env(noop(alice), seq(nextSeq - 1), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 0), queued);

        // Now that nextSeq is in the queue, we should be able to queue
        // nextSeq + 1.
        env(noop(alice), seq(nextSeq + 1), queued);

        // Fill the queue with sequence-based transactions.  When the
        // ledger closes we should find the three ticket-based
        // transactions gone from the queue (because they had the
        // highest fee).  Then the earliest of the sequence-based
        // transactions should also be gone from the queue.
        env(noop(alice), seq(nextSeq + 2), queued);
        env(noop(alice), seq(nextSeq + 3), queued);
        env(noop(alice), seq(nextSeq + 4), queued);
        env(noop(alice), seq(nextSeq + 5), queued);
        env(noop(alice), seq(nextSeq + 6), queued);
        env(noop(alice), seq(nextSeq + 7), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 10, 10, 6, 5, 257);

        // Check which of the queued transactions got into the ledger by
        // attempting to replace them.
        //  o Get tefNo_TICKET if the ticket has already been used.
        //  o Get tefPAST_SEQ if the sequence moved out of the queue.
        //  o Get telCAN_NOT_QUEUE_FEE if the transaction is still in
        //    the queue.
        env.close();
        env.require(owners(alice, 237), tickets(alice, 237));

        // The four ticket-based transactions went out first, since
        // they paid the highest fee.
        env(noop(alice), ticket::use(tkt1 + 4), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 5), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 12), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 13), ter(tefNO_TICKET));

        // Three of the sequence-based transactions also moved out of
        // the queue.
        env(noop(alice), seq(nextSeq + 1), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 2), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 3), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 4), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), seq(nextSeq + 5), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), seq(nextSeq + 6), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), seq(nextSeq + 7), ter(telCAN_NOT_QUEUE_FEE));

        checkMetrics(__LINE__, env, 4, 12, 7, 6, 256);
        BEAST_EXPECT(env.seq(alice) == nextSeq + 4);

        //////////////////////////////////////////////////////////////////

        // We haven't yet shown that ticket-based transactions can be added
        // to the queue in any order.  We should do that...
        std::uint32_t tkt250 = tkt1 + 249;
        env(noop(alice), ticket::use(tkt250 - 0), fee(30), queued);
        env(noop(alice), ticket::use(tkt1 + 14), fee(29), queued);
        env(noop(alice), ticket::use(tkt250 - 1), fee(28), queued);
        env(noop(alice), ticket::use(tkt1 + 15), fee(27), queued);
        env(noop(alice), ticket::use(tkt250 - 2), fee(26), queued);
        env(noop(alice), ticket::use(tkt1 + 16), fee(25), queued);
        env(noop(alice),
            ticket::use(tkt250 - 3),
            fee(24),
            ter(telCAN_NOT_QUEUE_FULL));
        env(noop(alice),
            ticket::use(tkt1 + 17),
            fee(23),
            ter(telCAN_NOT_QUEUE_FULL));
        env(noop(alice),
            ticket::use(tkt250 - 4),
            fee(22),
            ter(telCAN_NOT_QUEUE_FULL));
        env(noop(alice),
            ticket::use(tkt1 + 18),
            fee(21),
            ter(telCAN_NOT_QUEUE_FULL));

        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);

        env.close();
        env.require(owners(alice, 231), tickets(alice, 231));

        // These three ticket-based transactions escaped the queue.
        env(noop(alice), ticket::use(tkt1 + 14), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 15), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt1 + 16), ter(tefNO_TICKET));

        // But these four ticket-based transactions are in the queue
        // now; they moved into the TxQ from local transactions.
        env(noop(alice), ticket::use(tkt250 - 3), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), ticket::use(tkt1 + 17), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), ticket::use(tkt250 - 4), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), ticket::use(tkt1 + 18), ter(telCAN_NOT_QUEUE_FEE));

        // These three ticket-based transactions also escaped the queue.
        env(noop(alice), ticket::use(tkt250 - 2), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt250 - 1), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt250 - 0), ter(tefNO_TICKET));

        // These sequence-based transactions escaped the queue.
        env(noop(alice), seq(nextSeq + 4), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 5), ter(tefPAST_SEQ));

        // But these sequence-based transactions are still stuck in the queue.
        env(noop(alice), seq(nextSeq + 6), ter(telCAN_NOT_QUEUE_FEE));
        env(noop(alice), seq(nextSeq + 7), ter(telCAN_NOT_QUEUE_FEE));

        BEAST_EXPECT(env.seq(alice) == nextSeq + 6);
        checkMetrics(__LINE__, env, 6, 14, 8, 7, 256);

        //////////////////////////////////////////////////////////////////

        // Since we still have two ticket-based transactions in the queue
        // let's try replacing them.

        // 26 drops is less than 21 * 1.25
        env(noop(alice),
            ticket::use(tkt1 + 18),
            fee(26),
            ter(telCAN_NOT_QUEUE_FEE));

        // 27 drops is more than 21 * 1.25
        env(noop(alice), ticket::use(tkt1 + 18), fee(27), queued);

        // 27 drops is less than 22 * 1.25
        env(noop(alice),
            ticket::use(tkt250 - 4),
            fee(27),
            ter(telCAN_NOT_QUEUE_FEE));

        // 28 drops is more than 22 * 1.25
        env(noop(alice), ticket::use(tkt250 - 4), fee(28), queued);

        env.close();
        env.require(owners(alice, 227), tickets(alice, 227));

        // Verify that all remaining transactions made it out of the TxQ.
        env(noop(alice), ticket::use(tkt1 + 18), ter(tefNO_TICKET));
        env(noop(alice), ticket::use(tkt250 - 4), ter(tefNO_TICKET));
        env(noop(alice), seq(nextSeq + 4), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 5), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 6), ter(tefPAST_SEQ));
        env(noop(alice), seq(nextSeq + 7), ter(tefPAST_SEQ));

        BEAST_EXPECT(env.seq(alice) == nextSeq + 8);
        checkMetrics(__LINE__, env, 0, 16, 6, 8, 256);
    }

    void
    testTecResult()
    {
        using namespace jtx;
        testcase("queue tec");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}));

        auto alice = Account("alice");
        auto gw = Account("gw");
        auto USD = gw["USD"];

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

        // Create accounts
        env.fund(XRP(50000), noripple(alice, gw));
        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 2, 256);
        env.close();
        checkMetrics(__LINE__, env, 0, 4, 0, 2, 256);

        // Alice creates an unfunded offer while the ledger is not full
        env(offer(alice, XRP(1000), USD(1000)), ter(tecUNFUNDED_OFFER));
        checkMetrics(__LINE__, env, 0, 4, 1, 2, 256);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 4, 3, 2, 256);

        // Alice creates an unfunded offer that goes in the queue
        env(offer(alice, XRP(1000), USD(1000)), ter(terQUEUED));
        checkMetrics(__LINE__, env, 1, 4, 3, 2, 256);

        // The offer comes out of the queue
        env.close();
        checkMetrics(__LINE__, env, 0, 6, 1, 3, 256);
    }

    void
    testLocalTxRetry()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("local tx retry");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 2, 256);

        // Future transaction for Alice - fails
        env(noop(alice),
            openLedgerFee(env),
            seq(env.seq(alice) + 1),
            ter(terPRE_SEQ));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 2, 256);

        // Current transaction for Alice: held
        env(noop(alice), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 3, 2, 256);

        // Alice - sequence is too far ahead, so won't queue.
        env(noop(alice), seq(env.seq(alice) + 2), ter(telCAN_NOT_QUEUE));
        checkMetrics(__LINE__, env, 1, std::nullopt, 3, 2, 256);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 2, 256);

        // Daria with low fee: hold
        env(noop(charlie), fee(1000), queued);
        checkMetrics(__LINE__, env, 2, std::nullopt, 4, 2, 256);

        // Alice with normal fee: hold
        env(noop(alice), seq(env.seq(alice) + 1), queued);
        checkMetrics(__LINE__, env, 3, std::nullopt, 4, 2, 256);

        env.close();
        // Verify that the held transactions got applied
        // Alice's bad transaction applied from the
        // Local Txs.
        checkMetrics(__LINE__, env, 0, 8, 4, 4, 256);
    }

    void
    testLastLedgerSeq()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("last ledger sequence");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");
        auto edgar = Account("edgar");
        auto felicia = Account("felicia");

        auto queued = ter(terQUEUED);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(charlie, daria));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(edgar, felicia));
        env.close(env.now() + 5s, 10000ms);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);
        env(noop(bob));
        env(noop(charlie));
        env(noop(daria));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 2, 256);

        BEAST_EXPECT(env.current()->info().seq == 6);
        // Fail to queue an item with a low LastLedgerSeq
        env(noop(alice),
            json(R"({"LastLedgerSequence":7})"),
            ter(telCAN_NOT_QUEUE));
        // Queue an item with a sufficient LastLedgerSeq.
        env(noop(alice), json(R"({"LastLedgerSequence":8})"), queued);
        // Queue items with higher fees to force the previous
        // txn to wait.
        env(noop(bob), fee(7000), queued);
        env(noop(charlie), fee(7000), queued);
        env(noop(daria), fee(7000), queued);
        env(noop(edgar), fee(7000), queued);
        checkMetrics(__LINE__, env, 5, std::nullopt, 3, 2, 256);
        {
            auto& txQ = env.app().getTxQ();
            auto aliceStat = txQ.getAccountTxs(alice.id());
            BEAST_EXPECT(aliceStat.size() == 1);
            BEAST_EXPECT(aliceStat.begin()->feeLevel == FeeLevel64{256});
            BEAST_EXPECT(
                aliceStat.begin()->lastValid &&
                *aliceStat.begin()->lastValid == 8);
            BEAST_EXPECT(!aliceStat.begin()->consequences.isBlocker());

            auto bobStat = txQ.getAccountTxs(bob.id());
            BEAST_EXPECT(bobStat.size() == 1);
            BEAST_EXPECT(
                bobStat.begin()->feeLevel == FeeLevel64{7000 * 256 / 10});
            BEAST_EXPECT(!bobStat.begin()->lastValid);
            BEAST_EXPECT(!bobStat.begin()->consequences.isBlocker());

            auto noStat = txQ.getAccountTxs(Account::master.id());
            BEAST_EXPECT(noStat.empty());
        }

        env.close();
        checkMetrics(__LINE__, env, 1, 6, 4, 3, 256);

        // Keep alice's transaction waiting.
        env(noop(bob), fee(7000), queued);
        env(noop(charlie), fee(7000), queued);
        env(noop(daria), fee(7000), queued);
        env(noop(edgar), fee(7000), queued);
        env(noop(felicia), fee(6999), queued);
        checkMetrics(__LINE__, env, 6, 6, 4, 3, 257);

        env.close();
        // alice's transaction is still hanging around
        checkMetrics(__LINE__, env, 1, 8, 5, 4, 256, 700 * 256);
        BEAST_EXPECT(env.seq(alice) == 3);

        // Keep alice's transaction waiting.
        env(noop(bob), fee(8000), queued);
        env(noop(charlie), fee(8000), queued);
        env(noop(daria), fee(8000), queued);
        env(noop(daria), fee(8000), seq(env.seq(daria) + 1), queued);
        env(noop(edgar), fee(8000), queued);
        env(noop(felicia), fee(7999), queued);
        env(noop(felicia), fee(7999), seq(env.seq(felicia) + 1), queued);
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 257, 700 * 256);

        env.close();
        // alice's transaction expired without getting
        // into the ledger, so her transaction is gone,
        // though one of felicia's is still in the queue.
        checkMetrics(__LINE__, env, 1, 10, 6, 5, 256, 700 * 256);
        BEAST_EXPECT(env.seq(alice) == 3);
        BEAST_EXPECT(env.seq(felicia) == 7);

        env.close();
        // And now the queue is empty
        checkMetrics(__LINE__, env, 0, 12, 1, 6, 256, 800 * 256);
        BEAST_EXPECT(env.seq(alice) == 3);
        BEAST_EXPECT(env.seq(felicia) == 8);
    }

    void
    testZeroFeeTxn()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("zero transaction fee");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto carol = Account("carol");

        auto queued = ter(terQUEUED);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

        // Fund across several ledgers so the TxQ metrics stay restricted.
        env.fund(XRP(1000), noripple(alice, bob));
        env.close(env.now() + 5s, 10000ms);
        env.fund(XRP(1000), noripple(carol));
        env.close(env.now() + 5s, 10000ms);

        // Fill the ledger
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 2, 256);

        env(noop(bob), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 3, 2, 256);

        // Since Alice's queue is empty this blocker can go into her queue.
        env(regkey(alice, bob), fee(0), queued);
        checkMetrics(__LINE__, env, 2, std::nullopt, 3, 2, 256);

        // Close out this ledger so we can get a maxsize
        env.close();
        checkMetrics(__LINE__, env, 0, 6, 2, 3, 256);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

        auto feeAlice = 30;
        auto seqAlice = env.seq(alice);
        for (int i = 0; i < 4; ++i)
        {
            env(noop(alice), fee(feeAlice), seq(seqAlice), queued);
            feeAlice = (feeAlice + 1) * 125 / 100;
            ++seqAlice;
        }
        checkMetrics(__LINE__, env, 4, 6, 4, 3, 256);

        // Bob adds a zero fee blocker to his queue.
        auto const seqBob = env.seq(bob);
        env(regkey(bob, alice), fee(0), queued);
        checkMetrics(__LINE__, env, 5, 6, 4, 3, 256);

        // Carol fills the queue.
        auto feeCarol = feeAlice;
        auto seqCarol = env.seq(carol);
        for (int i = 0; i < 4; ++i)
        {
            env(noop(carol), fee(feeCarol), seq(seqCarol), queued);
            feeCarol = (feeCarol + 1) * 125 / 100;
            ++seqCarol;
        }
        checkMetrics(__LINE__, env, 6, 6, 4, 3, 3 * 256 + 1);

        // Carol submits high enough to beat Bob's average fee which kicks
        // out Bob's queued transaction.  However Bob's transaction stays
        // in the localTx queue, so it will return to the TxQ next time
        // around.
        env(noop(carol), fee(feeCarol), seq(seqCarol), ter(terQUEUED));

        env.close();
        // Some of Alice's transactions stay in the queue.  Bob's
        // transaction returns to the TxQ.
        checkMetrics(__LINE__, env, 5, 8, 5, 4, 256);
        BEAST_EXPECT(env.seq(alice) == seqAlice - 4);
        BEAST_EXPECT(env.seq(bob) == seqBob);
        BEAST_EXPECT(env.seq(carol) == seqCarol + 1);

        env.close();
        // The remaining queued transactions flush through to the ledger.
        checkMetrics(__LINE__, env, 0, 10, 5, 5, 256);
        BEAST_EXPECT(env.seq(alice) == seqAlice);
        BEAST_EXPECT(env.seq(bob) == seqBob + 1);
        BEAST_EXPECT(env.seq(carol) == seqCarol + 1);

        env.close();
        checkMetrics(__LINE__, env, 0, 10, 0, 5, 256);
        BEAST_EXPECT(env.seq(alice) == seqAlice);
        BEAST_EXPECT(env.seq(bob) == seqBob + 1);
        BEAST_EXPECT(env.seq(carol) == seqCarol + 1);
    }

    void
    testFailInPreclaim()
    {
        using namespace jtx;

        Env env(*this, makeConfig());
        testcase("fail in preclaim");

        auto alice = Account("alice");
        auto bob = Account("bob");

        env.fund(XRP(1000), noripple(alice));

        // These types of checks are tested elsewhere, but
        // this verifies that TxQ handles the failures as
        // expected.

        // Fail in preflight
        env(pay(alice, bob, XRP(-1000)), ter(temBAD_AMOUNT));

        // Fail in preclaim
        env(noop(alice), fee(XRP(100000)), ter(terINSUF_FEE_B));
    }

    void
    testQueuedTxFails()
    {
        using namespace jtx;
        testcase("queued tx fails");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "2"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

        env.fund(XRP(1000), noripple(alice, bob));

        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 2, 256);

        // Fill the ledger
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 2, 256);

        // Put a transaction in the queue
        env(noop(alice), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 3, 2, 256);

        // Now cheat, and bypass the queue.
        {
            auto const& jt = env.jt(noop(alice));
            BEAST_EXPECT(jt.stx);

            bool didApply;
            TER ter;

            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    std::tie(ter, didApply) = ripple::apply(
                        env.app(), view, *jt.stx, tapNONE, env.journal);
                    return didApply;
                });
            env.postconditions(jt, ter, didApply);
        }
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 2, 256);

        env.close();
        // Alice's queued transaction failed in TxQ::accept
        // with tefPAST_SEQ
        checkMetrics(__LINE__, env, 0, 8, 0, 4, 256);
    }

    void
    testMultiTxnPerAccount()
    {
        using namespace jtx;
        testcase("multi tx per account");

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
                {{"account_reserve", "200"}, {"owner_reserve", "50"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 10, 200, 50);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(drops(2000), noripple(alice));
        env.fund(XRP(500000), noripple(bob, charlie, daria));
        checkMetrics(__LINE__, env, 0, initQueueMax, 4, 3, 256);

        // Alice - price starts exploding: held
        env(noop(alice), fee(11), queued);
        checkMetrics(__LINE__, env, 1, initQueueMax, 4, 3, 256);

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto charlieSeq = env.seq(charlie);

        // Alice - try to queue a second transaction, but leave a gap
        env(noop(alice), seq(aliceSeq + 2), fee(100), ter(telCAN_NOT_QUEUE));
        checkMetrics(__LINE__, env, 1, initQueueMax, 4, 3, 256);

        // Alice - queue a second transaction. Yay!
        env(noop(alice), seq(aliceSeq + 1), fee(13), queued);
        checkMetrics(__LINE__, env, 2, initQueueMax, 4, 3, 256);

        // Alice - queue a third transaction. Yay.
        env(noop(alice), seq(aliceSeq + 2), fee(17), queued);
        checkMetrics(__LINE__, env, 3, initQueueMax, 4, 3, 256);

        // Bob - queue a transaction
        env(noop(bob), queued);
        checkMetrics(__LINE__, env, 4, initQueueMax, 4, 3, 256);

        // Bob - queue a second transaction
        env(noop(bob), seq(bobSeq + 1), fee(50), queued);
        checkMetrics(__LINE__, env, 5, initQueueMax, 4, 3, 256);

        // Charlie - queue a transaction, with a higher fee
        // than default
        env(noop(charlie), fee(15), queued);
        checkMetrics(__LINE__, env, 6, initQueueMax, 4, 3, 256);

        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        BEAST_EXPECT(env.seq(charlie) == charlieSeq);

        env.close();
        // Verify that all of but one of the queued transactions
        // got applied.
        checkMetrics(__LINE__, env, 1, 8, 5, 4, 256);

        // Verify that the stuck transaction is Bob's second.
        // Even though it had a higher fee than Alice's and
        // Charlie's, it didn't get attempted until the fee escalated.
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 3);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        BEAST_EXPECT(env.seq(charlie) == charlieSeq + 1);

        // Alice - fill up the queue
        std::int64_t aliceFee = 27;
        aliceSeq = env.seq(alice);
        auto lastLedgerSeq = env.current()->info().seq + 2;
        for (auto i = 0; i < 7; i++)
        {
            env(noop(alice),
                seq(aliceSeq),
                json(jss::LastLedgerSequence, lastLedgerSeq + i),
                fee(--aliceFee),
                queued);
            ++aliceSeq;
        }
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 513);
        {
            auto& txQ = env.app().getTxQ();
            auto aliceStat = txQ.getAccountTxs(alice.id());
            aliceFee = 27;
            auto const& baseFee = env.current()->fees().base;
            auto seq = env.seq(alice);
            BEAST_EXPECT(aliceStat.size() == 7);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.seqProxy.isSeq() && tx.seqProxy.value() == seq);
                BEAST_EXPECT(
                    tx.feeLevel == toFeeLevel(XRPAmount(--aliceFee), baseFee));
                BEAST_EXPECT(tx.lastValid);
                BEAST_EXPECT(
                    (tx.consequences.fee() == drops(aliceFee) &&
                     tx.consequences.potentialSpend() == drops(0) &&
                     !tx.consequences.isBlocker()) ||
                    tx.seqProxy.value() == env.seq(alice) + 6);
                ++seq;
            }
        }

        // Alice attempts to add another item to the queue,
        // but you can't force your own earlier txn off the
        // queue.
        env(noop(alice),
            seq(aliceSeq),
            json(jss::LastLedgerSequence, lastLedgerSeq + 7),
            fee(aliceFee),
            ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 513);

        // Charlie - try to add another item to the queue,
        // which fails because fee is lower than Alice's
        // queued average.
        env(noop(charlie), fee(19), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 513);

        // Charlie - add another item to the queue, which
        // causes Alice's last txn to drop
        env(noop(charlie), fee(30), queued);
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 538);

        // Alice - now attempt to add one more to the queue,
        // which fails because the last tx was dropped, so
        // there is no complete chain.
        env(noop(alice), seq(aliceSeq), fee(aliceFee), ter(telCAN_NOT_QUEUE));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 538);

        // Alice wants this tx more than the dropped tx,
        // so resubmits with higher fee, but the queue
        // is full, and her account is the cheapest.
        env(noop(alice),
            seq(aliceSeq - 1),
            fee(aliceFee),
            ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 538);

        // Try to replace a middle item in the queue
        // without enough fee.
        aliceSeq = env.seq(alice) + 2;
        aliceFee = 29;
        env(noop(alice),
            seq(aliceSeq),
            fee(aliceFee),
            ter(telCAN_NOT_QUEUE_FEE));
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 538);

        // Replace a middle item from the queue successfully
        ++aliceFee;
        env(noop(alice), seq(aliceSeq), fee(aliceFee), queued);
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 538);

        env.close();
        // Alice's transactions processed, along with
        // Charlie's, and the lost one is replayed and
        // added back to the queue.
        checkMetrics(__LINE__, env, 4, 10, 6, 5, 256);

        aliceSeq = env.seq(alice) + 1;

        // Try to replace that item with a transaction that will
        // bankrupt Alice. Fails, because an account can't have
        // more than the minimum reserve in flight before the
        // last queued transaction
        aliceFee =
            env.le(alice)->getFieldAmount(sfBalance).xrp().drops() - (62);
        env(noop(alice),
            seq(aliceSeq),
            fee(aliceFee),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 4, 10, 6, 5, 256);

        // Try to spend more than Alice can afford with all the other txs.
        aliceSeq += 2;
        env(noop(alice), seq(aliceSeq), fee(aliceFee), ter(terINSUF_FEE_B));
        checkMetrics(__LINE__, env, 4, 10, 6, 5, 256);

        // Replace the last queued item with a transaction that will
        // bankrupt Alice
        --aliceFee;
        env(noop(alice), seq(aliceSeq), fee(aliceFee), queued);
        checkMetrics(__LINE__, env, 4, 10, 6, 5, 256);

        // Alice - Attempt to queue a last transaction, but it
        // fails because the fee in flight is too high, before
        // the fee is checked against the balance
        aliceFee /= 5;
        ++aliceSeq;
        env(noop(alice),
            seq(aliceSeq),
            fee(aliceFee),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 4, 10, 6, 5, 256);

        env.close();
        // All of Alice's transactions applied.
        checkMetrics(__LINE__, env, 0, 12, 4, 6, 256);

        env.close();
        checkMetrics(__LINE__, env, 0, 12, 0, 6, 256);

        // Alice is broke
        env.require(balance(alice, XRP(0)));
        env(noop(alice), ter(terINSUF_FEE_B));

        // Bob tries to queue up more than the single
        // account limit (10) txs.
        fillQueue(env, bob);
        bobSeq = env.seq(bob);
        checkMetrics(__LINE__, env, 0, 12, 7, 6, 256);
        for (int i = 0; i < 10; ++i)
            env(noop(bob), seq(bobSeq + i), queued);
        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);
        // Bob hit the single account limit
        env(noop(bob), seq(bobSeq + 10), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);
        // Bob can replace one of the earlier txs regardless
        // of the limit
        env(noop(bob), seq(bobSeq + 5), fee(20), queued);
        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);

        // Try to replace a middle item in the queue
        // with enough fee to bankrupt bob and make the
        // later transactions unable to pay their fees
        std::int64_t bobFee =
            env.le(bob)->getFieldAmount(sfBalance).xrp().drops() - (9 * 10 - 1);
        env(noop(bob),
            seq(bobSeq + 5),
            fee(bobFee),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);

        // Attempt to replace a middle item in the queue with enough fee
        // to bankrupt bob, and also to use fee averaging to clear out the
        // first six transactions.
        //
        // The attempt fails because the sum of bob's fees now exceeds the
        // (artificially lowered to 200 drops) account reserve.
        bobFee =
            env.le(bob)->getFieldAmount(sfBalance).xrp().drops() - (9 * 10);
        env(noop(bob),
            seq(bobSeq + 5),
            fee(bobFee),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 10, 12, 7, 6, 256);

        // Close the ledger and verify that the queued transactions succeed
        // and bob has the right ending balance.
        env.close();
        checkMetrics(__LINE__, env, 3, 14, 8, 7, 256);
        env.close();
        checkMetrics(__LINE__, env, 0, 16, 3, 8, 256);
        env.require(balance(bob, drops(499'999'999'750)));
    }

    void
    testTieBreaking()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("tie breaking");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "4"}}));

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

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 4, 256);

        // Create several accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie, daria));
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 4, 256);

        env.close();
        checkMetrics(__LINE__, env, 0, 8, 0, 4, 256);

        env.fund(XRP(50000), noripple(elmo, fred, gwen, hank));
        checkMetrics(__LINE__, env, 0, 8, 4, 4, 256);

        env.close();
        checkMetrics(__LINE__, env, 0, 8, 0, 4, 256);

        //////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(gwen));
        env(noop(hank));
        env(noop(gwen));
        env(noop(fred));
        env(noop(elmo));
        checkMetrics(__LINE__, env, 0, 8, 5, 4, 256);

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto charlieSeq = env.seq(charlie);
        auto dariaSeq = env.seq(daria);
        auto elmoSeq = env.seq(elmo);
        auto fredSeq = env.seq(fred);
        auto gwenSeq = env.seq(gwen);
        auto hankSeq = env.seq(hank);

        // This time, use identical fees.

        // All of these get into the queue, but one gets dropped when the
        // higher fee one is added later. Which one depends on ordering.
        env(noop(alice), fee(15), queued);
        env(noop(bob), fee(15), queued);
        env(noop(charlie), fee(15), queued);
        env(noop(daria), fee(15), queued);
        env(noop(elmo), fee(15), queued);
        env(noop(fred), fee(15), queued);
        env(noop(gwen), fee(15), queued);
        env(noop(hank), fee(15), queued);

        // Queue is full now. Minimum fee now reflects the
        // lowest fee in the queue.
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 385);

        // Try to add another transaction with the default (low) fee,
        // it should fail because it can't replace the one already
        // there.
        env(noop(charlie), ter(telCAN_NOT_QUEUE_FEE));

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(charlie), fee(100), seq(charlieSeq + 1), queued);

        // Queue is still full.
        checkMetrics(__LINE__, env, 8, 8, 5, 4, 385);

        // Six txs are processed out of the queue into the ledger,
        // leaving two txs. The dropped tx is retried from localTxs, and
        // put back into the queue.
        env.close();
        checkMetrics(__LINE__, env, 3, 10, 6, 5, 256);

        // This next test should remain unchanged regardless of
        // transaction ordering
        BEAST_EXPECT(
            aliceSeq + bobSeq + charlieSeq + dariaSeq + elmoSeq + fredSeq +
                gwenSeq + hankSeq + 6 ==
            env.seq(alice) + env.seq(bob) + env.seq(charlie) + env.seq(daria) +
                env.seq(elmo) + env.seq(fred) + env.seq(gwen) + env.seq(hank));
        // These tests may change if TxQ ordering is changed
        using namespace std::string_literals;
        BEAST_EXPECTS(
            aliceSeq == env.seq(alice),
            "alice: "s + std::to_string(aliceSeq) + ", " +
                std::to_string(env.seq(alice)));
        BEAST_EXPECTS(
            bobSeq + 1 == env.seq(bob),
            "bob: "s + std::to_string(bobSeq) + ", " +
                std::to_string(env.seq(bob)));
        BEAST_EXPECTS(
            charlieSeq + 2 == env.seq(charlie),
            "charlie: "s + std::to_string(charlieSeq) + ", " +
                std::to_string(env.seq(charlie)));
        BEAST_EXPECTS(
            dariaSeq + 1 == env.seq(daria),
            "daria: "s + std::to_string(dariaSeq) + ", " +
                std::to_string(env.seq(daria)));
        BEAST_EXPECTS(
            elmoSeq + 1 == env.seq(elmo),
            "elmo: "s + std::to_string(elmoSeq) + ", " +
                std::to_string(env.seq(elmo)));
        BEAST_EXPECTS(
            fredSeq == env.seq(fred),
            "fred: "s + std::to_string(fredSeq) + ", " +
                std::to_string(env.seq(fred)));
        BEAST_EXPECTS(
            gwenSeq == env.seq(gwen),
            "gwen: "s + std::to_string(gwenSeq) + ", " +
                std::to_string(env.seq(gwen)));
        BEAST_EXPECTS(
            hankSeq + 1 == env.seq(hank),
            "hank: "s + std::to_string(hankSeq) + ", " +
                std::to_string(env.seq(hank)));

        // Which sequences get incremented may change if TxQ ordering is
        // changed
        //++aliceSeq;
        ++bobSeq;
        ++(++charlieSeq);
        ++dariaSeq;
        ++elmoSeq;
        // ++fredSeq;
        //++gwenSeq;
        ++hankSeq;

        auto getTxsQueued = [&]() {
            auto const txs = env.app().getTxQ().getTxs();
            std::map<AccountID, std::size_t> result;
            for (auto const& tx : txs)
            {
                ++result[tx.txn->at(sfAccount)];
            }
            return result;
        };
        auto qTxCount1 = getTxsQueued();
        BEAST_EXPECT(qTxCount1.size() <= 3);

        // Fill up the queue again
        env(noop(alice),
            seq(aliceSeq + qTxCount1[alice.id()]++),
            fee(15),
            queued);
        env(noop(bob), seq(bobSeq + qTxCount1[bob.id()]++), fee(15), queued);
        env(noop(charlie),
            seq(charlieSeq + qTxCount1[charlie.id()]++),
            fee(15),
            queued);
        env(noop(daria),
            seq(dariaSeq + qTxCount1[daria.id()]++),
            fee(15),
            queued);
        env(noop(elmo), seq(elmoSeq + qTxCount1[elmo.id()]++), fee(15), queued);
        env(noop(fred), seq(fredSeq + qTxCount1[fred.id()]++), fee(15), queued);
        env(noop(gwen), seq(gwenSeq + qTxCount1[gwen.id()]++), fee(15), queued);
        checkMetrics(__LINE__, env, 10, 10, 6, 5, 385);

        // Add another transaction, with a higher fee,
        // Not high enough to get into the ledger, but high
        // enough to get into the queue (and kick somebody out)
        env(noop(alice),
            fee(100),
            seq(aliceSeq + qTxCount1[alice.id()]++),
            queued);

        checkMetrics(__LINE__, env, 10, 10, 6, 5, 385);

        // Seven txs are processed out of the queue, leaving 3. One
        // dropped tx is retried from localTxs, and put back into the
        // queue.
        env.close();
        checkMetrics(__LINE__, env, 4, 12, 7, 6, 256);

        // Refresh the queue counts
        auto qTxCount2 = getTxsQueued();
        BEAST_EXPECT(qTxCount2.size() <= 4);

        // This next test should remain unchanged regardless of
        // transaction ordering
        BEAST_EXPECT(
            aliceSeq + bobSeq + charlieSeq + dariaSeq + elmoSeq + fredSeq +
                gwenSeq + hankSeq + 7 ==
            env.seq(alice) + env.seq(bob) + env.seq(charlie) + env.seq(daria) +
                env.seq(elmo) + env.seq(fred) + env.seq(gwen) + env.seq(hank));
        // These tests may change if TxQ ordering is changed
        BEAST_EXPECTS(
            aliceSeq + qTxCount1[alice.id()] - qTxCount2[alice.id()] ==
                env.seq(alice),
            "alice: "s + std::to_string(aliceSeq) + ", " +
                std::to_string(env.seq(alice)));
        BEAST_EXPECTS(
            bobSeq + qTxCount1[bob.id()] - qTxCount2[bob.id()] == env.seq(bob),
            "bob: "s + std::to_string(bobSeq) + ", " +
                std::to_string(env.seq(bob)));
        BEAST_EXPECTS(
            charlieSeq + qTxCount1[charlie.id()] - qTxCount2[charlie.id()] ==
                env.seq(charlie),
            "charlie: "s + std::to_string(charlieSeq) + ", " +
                std::to_string(env.seq(charlie)));
        BEAST_EXPECTS(
            dariaSeq + qTxCount1[daria.id()] - qTxCount2[daria.id()] ==
                env.seq(daria),
            "daria: "s + std::to_string(dariaSeq) + ", " +
                std::to_string(env.seq(daria)));
        BEAST_EXPECTS(
            elmoSeq + qTxCount1[elmo.id()] - qTxCount2[elmo.id()] ==
                env.seq(elmo),
            "elmo: "s + std::to_string(elmoSeq) + ", " +
                std::to_string(env.seq(elmo)));
        BEAST_EXPECTS(
            fredSeq + qTxCount1[fred.id()] - qTxCount2[fred.id()] ==
                env.seq(fred),
            "fred: "s + std::to_string(fredSeq) + ", " +
                std::to_string(env.seq(fred)));
        BEAST_EXPECTS(
            gwenSeq + qTxCount1[gwen.id()] - qTxCount2[gwen.id()] ==
                env.seq(gwen),
            "gwen: "s + std::to_string(gwenSeq) + ", " +
                std::to_string(env.seq(gwen)));
        BEAST_EXPECTS(
            hankSeq + qTxCount1[hank.id()] - qTxCount2[hank.id()] ==
                env.seq(hank),
            "hank: "s + std::to_string(hankSeq) + ", " +
                std::to_string(env.seq(hank)));
    }

    void
    testAcctTxnID()
    {
        using namespace jtx;
        testcase("acct tx id");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "1"}}));

        auto alice = Account("alice");

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 1, 256);

        env.fund(XRP(50000), noripple(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 1, 1, 256);

        env(fset(alice, asfAccountTxnID));
        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 1, 256);

        // Immediately after the fset, the sfAccountTxnID field
        // is still uninitialized, so preflight succeeds here,
        // and this txn fails because it can't be stored in the queue.
        env(noop(alice),
            json(R"({"AccountTxnID": "0"})"),
            ter(telCAN_NOT_QUEUE));

        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 1, 256);
        env.close();
        // The failed transaction is retried from LocalTx
        // and succeeds.
        checkMetrics(__LINE__, env, 0, 4, 1, 2, 256);

        env(noop(alice));
        checkMetrics(__LINE__, env, 0, 4, 2, 2, 256);

        env(noop(alice), json(R"({"AccountTxnID": "0"})"), ter(tefWRONG_PRIOR));
    }

    void
    testMaximum()
    {
        using namespace jtx;
        using namespace std::string_literals;
        testcase("maximum tx");

        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger_standalone", "2"},
                     {"minimum_txn_in_ledger", "5"},
                     {"target_txn_in_ledger", "4"},
                     {"maximum_txn_in_ledger", "5"}}));

            auto alice = Account("alice");

            checkMetrics(__LINE__, env, 0, std::nullopt, 0, 2, 256);

            env.fund(XRP(50000), noripple(alice));
            checkMetrics(__LINE__, env, 0, std::nullopt, 1, 2, 256);

            for (int i = 0; i < 10; ++i)
                env(noop(alice), openLedgerFee(env));

            checkMetrics(__LINE__, env, 0, std::nullopt, 11, 2, 256);

            env.close();
            // If not for the maximum, the per ledger would be 11.
            checkMetrics(__LINE__, env, 0, 10, 0, 5, 256, 800025);
        }

        try
        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger", "200"},
                     {"minimum_txn_in_ledger_standalone", "200"},
                     {"target_txn_in_ledger", "4"},
                     {"maximum_txn_in_ledger", "5"}}));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s);
        }
        try
        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger", "200"},
                     {"minimum_txn_in_ledger_standalone", "2"},
                     {"target_txn_in_ledger", "4"},
                     {"maximum_txn_in_ledger", "5"}}));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s);
        }
        try
        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger", "2"},
                     {"minimum_txn_in_ledger_standalone", "200"},
                     {"target_txn_in_ledger", "4"},
                     {"maximum_txn_in_ledger", "5"}}));
            // should throw
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                e.what() ==
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger_standalone) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger)."s);
        }
    }

    void
    testUnexpectedBalanceChange()
    {
        using namespace jtx;
        testcase("unexpected balance change");

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
                {{"account_reserve", "200"}, {"owner_reserve", "50"}}));

        auto alice = Account("alice");
        auto bob = Account("bob");

        auto queued = ter(terQUEUED);

        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 10, 200, 50);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, initQueueMax, 0, 3, 256);

        env.fund(drops(5000), noripple(alice));
        env.fund(XRP(50000), noripple(bob));
        checkMetrics(__LINE__, env, 0, initQueueMax, 2, 3, 256);
        auto USD = bob["USD"];

        env(offer(alice, USD(5000), drops(5000)), require(owners(alice, 1)));
        checkMetrics(__LINE__, env, 0, initQueueMax, 3, 3, 256);

        env.close();
        checkMetrics(__LINE__, env, 0, 6, 0, 3, 256);

        // Fill up the ledger
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

        // Queue up a couple of transactions, plus one
        // more expensive one.
        auto aliceSeq = env.seq(alice);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), seq(aliceSeq++), queued);
        env(noop(alice), fee(drops(1000)), seq(aliceSeq), queued);
        checkMetrics(__LINE__, env, 4, 6, 4, 3, 256);

        // This offer should take Alice's offer
        // up to Alice's reserve.
        env(offer(bob, drops(5000), USD(5000)),
            openLedgerFee(env),
            require(
                balance(alice, drops(250)), owners(alice, 1), lines(alice, 1)));
        checkMetrics(__LINE__, env, 4, 6, 5, 3, 256);

        // Try adding a new transaction.
        // Too many fees in flight.
        env(noop(alice),
            fee(drops(200)),
            seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 4, 6, 5, 3, 256);

        // Close the ledger. All of Alice's transactions
        // take a fee, except the last one.
        env.close();
        checkMetrics(__LINE__, env, 1, 10, 3, 5, 256);
        env.require(balance(alice, drops(250 - 30)));

        // Still can't add a new transaction for Alice,
        // no matter the fee.
        env(noop(alice),
            fee(drops(200)),
            seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 1, 10, 3, 5, 256);

        /* At this point, Alice's transaction is indefinitely
            stuck in the queue. Eventually it will either
            expire, get forced off the end by more valuable
            transactions, get replaced by Alice, or Alice
            will get more XRP, and it'll process.
        */

        for (int i = 0; i < 9; ++i)
        {
            env.close();
            checkMetrics(__LINE__, env, 1, 10, 0, 5, 256);
        }

        // And Alice's transaction expires (via the retry limit,
        // not LastLedgerSequence).
        env.close();
        checkMetrics(__LINE__, env, 0, 10, 0, 5, 256);
    }

    void
    testBlockersSeq()
    {
        using namespace jtx;
        testcase("blockers sequence");

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        env.fund(XRP(50000), noripple(alice, bob));
        env.memoize(charlie);
        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 3, 256);
        {
            // Cannot put a blocker in an account's queue if that queue
            // already holds two or more (non-blocker) entries.

            // Fill up the open ledger
            env(noop(alice));
            // Set a regular key just to clear the password spent flag
            env(regkey(alice, charlie));
            checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

            // Put two "normal" txs in the queue
            auto const aliceSeq = env.seq(alice);
            env(noop(alice), seq(aliceSeq + 0), queued);
            env(noop(alice), seq(aliceSeq + 1), queued);

            // Can't replace either queued transaction with a blocker
            env(fset(alice, asfAccountTxnID),
                seq(aliceSeq + 0),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            env(regkey(alice, bob),
                seq(aliceSeq + 1),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            // Can't append a blocker to the queue.
            env(signers(alice, 2, {{bob}, {charlie}, {daria}}),
                seq(aliceSeq + 2),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            // Other accounts are not affected
            env(noop(bob), queued);
            checkMetrics(__LINE__, env, 3, std::nullopt, 4, 3, 256);

            // Drain the queue.
            env.close();
            checkMetrics(__LINE__, env, 0, 8, 4, 4, 256);
        }
        {
            // Replace a lone non-blocking tx with a blocker.

            // Fill up the open ledger and put just one entry in the TxQ.
            env(noop(alice));

            auto const aliceSeq = env.seq(alice);
            env(noop(alice), seq(aliceSeq + 0), queued);

            // Since there's only one entry in the queue we can replace
            // that entry with a blocker.
            env(regkey(alice, bob), seq(aliceSeq + 0), fee(20), queued);

            // Now that there's a blocker in the queue we can't append to
            // the queue.
            env(noop(alice), seq(aliceSeq + 1), ter(telCAN_NOT_QUEUE_BLOCKED));

            // Other accounts are unaffected.
            env(noop(bob), queued);

            // We can replace the blocker with a different blocker.
            env(signers(alice, 2, {{bob}, {charlie}, {daria}}),
                seq(aliceSeq + 0),
                fee(26),
                queued);

            // Prove that the queue is still blocked.
            env(noop(alice), seq(aliceSeq + 1), ter(telCAN_NOT_QUEUE_BLOCKED));

            // We can replace the blocker with a non-blocker.  Then we can
            // successfully append to the queue.
            env(noop(alice), seq(aliceSeq + 0), fee(33), queued);
            env(noop(alice), seq(aliceSeq + 1), queued);

            // Drain the queue.
            env.close();
            checkMetrics(__LINE__, env, 0, 10, 3, 5, 256);
        }
        {
            // Put a blocker in an empty queue.

            // Fill up the open ledger and put a blocker as Alice's first
            // entry in the (empty) TxQ.
            env(noop(alice));
            env(noop(alice));
            env(noop(alice));

            auto const aliceSeq = env.seq(alice);
            env(fset(alice, asfAccountTxnID), seq(aliceSeq + 0), queued);

            // Since there's a blocker in the queue we can't append to
            // the queue.
            env(noop(alice), seq(aliceSeq + 1), ter(telCAN_NOT_QUEUE_BLOCKED));

            // Other accounts are unaffected.
            env(noop(bob), queued);

            // We can replace the blocker with a non-blocker.  Then we can
            // successfully append to the queue.
            env(noop(alice), seq(aliceSeq + 0), fee(20), queued);
            env(noop(alice), seq(aliceSeq + 1), queued);

            // Drain the queue.
            env.close();
            checkMetrics(__LINE__, env, 0, 12, 3, 6, 256);
        }
    }

    void
    testBlockersTicket()
    {
        using namespace jtx;
        testcase("blockers ticket");

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");
        auto daria = Account("daria");

        auto queued = ter(terQUEUED);

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        env.fund(XRP(50000), noripple(alice, bob));
        env.memoize(charlie);

        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 3, 256);

        std::uint32_t tkt{env.seq(alice) + 1};
        {
            // Cannot put a blocker in an account's queue if that queue
            // already holds two or more (non-blocker) entries.

            // Fill up the open ledger
            env(ticket::create(alice, 250), seq(tkt - 1));
            // Set a regular key just to clear the password spent flag
            env(regkey(alice, charlie));
            checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

            // Put two "normal" txs in the queue
            auto const aliceSeq = env.seq(alice);
            env(noop(alice), ticket::use(tkt + 2), queued);
            env(noop(alice), ticket::use(tkt + 1), queued);

            // Can't replace either queued transaction with a blocker
            env(fset(alice, asfAccountTxnID),
                ticket::use(tkt + 1),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            env(regkey(alice, bob),
                ticket::use(tkt + 2),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            // Can't append a blocker to the queue.
            env(signers(alice, 2, {{bob}, {charlie}, {daria}}),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            env(signers(alice, 2, {{bob}, {charlie}, {daria}}),
                ticket::use(tkt + 0),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            // Other accounts are not affected
            env(noop(bob), queued);
            checkMetrics(__LINE__, env, 3, std::nullopt, 4, 3, 256);

            // Drain the queue and local transactions.
            env.close();
            checkMetrics(__LINE__, env, 0, 8, 5, 4, 256);

            // Show that the local transactions have flushed through as well.
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env(noop(alice), ticket::use(tkt + 0), ter(tefNO_TICKET));
            env(noop(alice), ticket::use(tkt + 1), ter(tefNO_TICKET));
            env(noop(alice), ticket::use(tkt + 2), ter(tefNO_TICKET));
            tkt += 3;
        }
        {
            // Replace a lone non-blocking tx with a blocker.

            // Put just one entry in the TxQ.
            auto const aliceSeq = env.seq(alice);
            env(noop(alice), ticket::use(tkt + 0), queued);

            // Since there's an entry in the queue we cannot append a
            // blocker to the account's queue.
            env(regkey(alice, bob), fee(20), ter(telCAN_NOT_QUEUE_BLOCKS));
            env(regkey(alice, bob),
                ticket::use(tkt + 1),
                fee(20),
                ter(telCAN_NOT_QUEUE_BLOCKS));

            // However we can _replace_ that lone entry with a blocker.
            env(regkey(alice, bob), ticket::use(tkt + 0), fee(20), queued);

            // Now that there's a blocker in the queue we can't append to
            // the queue.
            env(noop(alice), ter(telCAN_NOT_QUEUE_BLOCKED));
            env(noop(alice),
                ticket::use(tkt + 1),
                ter(telCAN_NOT_QUEUE_BLOCKED));

            // Other accounts are unaffected.
            env(noop(bob), queued);

            // We can replace the blocker with a different blocker.
            env(signers(alice, 2, {{bob}, {charlie}, {daria}}),
                ticket::use(tkt + 0),
                fee(26),
                queued);

            // Prove that the queue is still blocked.
            env(noop(alice), ter(telCAN_NOT_QUEUE_BLOCKED));
            env(noop(alice),
                ticket::use(tkt + 1),
                ter(telCAN_NOT_QUEUE_BLOCKED));

            // We can replace the blocker with a non-blocker.  Then we can
            // successfully append to the queue.
            env(noop(alice), ticket::use(tkt + 0), fee(33), queued);
            env(noop(alice), ticket::use(tkt + 1), queued);
            env(noop(alice), seq(aliceSeq), queued);

            // Drain the queue.
            env.close();
            checkMetrics(__LINE__, env, 0, 10, 4, 5, 256);

            // Show that the local transactions have flushed through as well.
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env(noop(alice), ticket::use(tkt + 0), ter(tefNO_TICKET));
            env(noop(alice), ticket::use(tkt + 1), ter(tefNO_TICKET));
            tkt += 2;
        }
        {
            // Put a blocker in an empty queue.

            // Fill up the open ledger and put a blocker as Alice's first
            // entry in the (empty) TxQ.
            env(noop(alice));
            env(noop(alice));

            env(fset(alice, asfAccountTxnID), ticket::use(tkt + 2), queued);

            // Since there's a blocker in the queue we can't append to
            // the queue.
            env(noop(alice),
                ticket::use(tkt + 1),
                ter(telCAN_NOT_QUEUE_BLOCKED));

            // Other accounts are unaffected.
            env(noop(bob), queued);

            // We can replace the blocker with a non-blocker.  Then we can
            // successfully append to the queue.
            env(noop(alice), ticket::use(tkt + 2), fee(20), queued);
            env(noop(alice), ticket::use(tkt + 1), queued);

            // Drain the queue.
            env.close();
            checkMetrics(__LINE__, env, 0, 12, 3, 6, 256);
        }
    }

    void
    testInFlightBalance()
    {
        using namespace jtx;
        testcase("In-flight balance checks");

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
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
        auto const initQueueMax = initFee(env, 3, 2, 10, 200, 50);

        auto limit = 3;

        checkMetrics(__LINE__, env, 0, initQueueMax, 0, limit, 256);

        env.fund(XRP(50000), noripple(alice, charlie), gw);
        checkMetrics(__LINE__, env, 0, initQueueMax, limit + 1, limit, 256);

        auto USD = gw["USD"];
        auto BUX = gw["BUX"];

        //////////////////////////////////////////
        // Offer with high XRP out and low fee doesn't block
        auto aliceSeq = env.seq(alice);
        auto aliceBal = env.balance(alice);

        env.require(balance(alice, XRP(50000)), owners(alice, 0));

        // If this offer crosses, all of alice's
        // XRP will be taken (except the reserve).
        env(offer(alice, BUX(5000), XRP(50000)), queued);
        checkMetrics(__LINE__, env, 1, initQueueMax, limit + 1, limit, 256);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, initQueueMax, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(20)), owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with high XRP out and high total fee blocks later txs
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        env.require(owners(alice, 0));

        // Alice creates an offer with a fee of half the reserve
        env(offer(alice, BUX(5000), XRP(50000)), fee(drops(100)), queued);
        checkMetrics(__LINE__, env, 1, limit * 2, limit + 1, limit, 256);

        // Alice creates another offer with a fee
        // that brings the total to just shy of the reserve
        env(noop(alice), fee(drops(99)), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice),
            fee(drops(51)),
            seq(aliceSeq + 2),
            ter(terINSUF_FEE_B));
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 3, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(250)), owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with high XRP out and super high fee blocks later txs
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        env.require(owners(alice, 0));

        // Alice creates an offer with a fee larger than the reserve
        // This one can queue because it's the first in the queue for alice
        env(offer(alice, BUX(5000), XRP(50000)), fee(drops(300)), queued);
        checkMetrics(__LINE__, env, 1, limit * 2, limit + 1, limit, 256);

        // So even a noop will look like alice
        // doesn't have the balance to pay the fee
        env(noop(alice),
            fee(drops(51)),
            seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BALANCE));
        checkMetrics(__LINE__, env, 1, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(351)), owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Offer with low XRP out allows later txs
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);
        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this offer crosses, just a bit
        // of alice's XRP will be taken.
        env(offer(alice, BUX(50), XRP(500)), queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // has plenty of XRP, because the offer didn't
        // cross (of course).
        env.require(balance(alice, aliceBal - drops(20)), owners(alice, 1));
        // cancel the offer
        env(offer_cancel(alice, aliceSeq));

        //////////////////////////////////////////
        // Large XRP payment doesn't block later txs
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this payment succeeds, alice will
        // send her entire balance to charlie
        // (minus the reserve).
        env(pay(alice, charlie, XRP(50000)), queued);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // But once we close the ledger, we find alice
        // still has most of her balance, because the
        // payment was unfunded!
        env.require(balance(alice, aliceBal - drops(20)), owners(alice, 0));

        //////////////////////////////////////////
        // Small XRP payment allows later txs
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);

        // If this payment succeeds, alice will
        // send just a bit of balance to charlie
        env(pay(alice, charlie, XRP(500)), queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // The payment succeeds
        env.require(
            balance(alice, aliceBal - XRP(500) - drops(20)), owners(alice, 0));

        //////////////////////////////////////////
        // Large IOU payment allows later txs
        auto const amount = USD(500000);
        env(trust(alice, USD(50000000)));
        env(trust(charlie, USD(50000000)));
        checkMetrics(__LINE__, env, 0, limit * 2, 4, limit, 256);
        // Close so we don't have to deal
        // with tx ordering in consensus.
        env.close();

        env(pay(gw, alice, amount));
        checkMetrics(__LINE__, env, 0, limit * 2, 1, limit, 256);
        // Close so we don't have to deal
        // with tx ordering in consensus.
        env.close();

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        auto aliceUSD = env.balance(alice, USD);

        // If this payment succeeds, alice will
        // send her entire USD balance to charlie.
        env(pay(alice, charlie, amount), queued);

        // But that's fine, because it doesn't affect
        // alice's XRP balance (other than the fee, of course).
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice has her
        // XRP balance, but her USD balance went to charlie.
        env.require(
            balance(alice, aliceBal - drops(20)),
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
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        auto charlieUSD = env.balance(charlie, USD);

        // If this payment succeeds, and uses the
        // entire sendMax, alice will send her
        // entire XRP balance to charlie in the
        // form of USD.
        BEAST_EXPECT(XRP(60000) > aliceBal);
        env(pay(alice, charlie, USD(1000)), sendmax(XRP(60000)), queued);

        // But because the reserve is protected, another
        // transaction will be allowed to queue
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice sent a payment
        // to charlie using only a portion of her XRP balance
        env.require(
            balance(alice, aliceBal - XRP(10000) - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, charlieUSD + USD(1000)),
            owners(alice, 1),
            owners(charlie, 1));

        //////////////////////////////////////////
        // Small XRP to IOU payment allows later txs.

        fillQueue(env, charlie);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        charlieUSD = env.balance(charlie, USD);

        // If this payment succeeds, and uses the
        // entire sendMax, alice will only send
        // a portion of her XRP balance to charlie
        // in the form of USD.
        BEAST_EXPECT(aliceBal > XRP(6001));
        env(pay(alice, charlie, USD(500)), sendmax(XRP(6000)), queued);

        // And later transactions are just fine
        env(noop(alice), seq(aliceSeq + 1), queued);
        checkMetrics(__LINE__, env, 2, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 2, limit, 256);

        // So once we close the ledger, alice sent a payment
        // to charlie using only a portion of her XRP balance
        env.require(
            balance(alice, aliceBal - XRP(5000) - drops(20)),
            balance(alice, USD(0)),
            balance(charlie, charlieUSD + USD(500)),
            owners(alice, 1),
            owners(charlie, 1));

        //////////////////////////////////////////
        // Edge case: what happens if the balance is below the reserve?
        env(noop(alice), fee(env.balance(alice) - drops(30)));
        env.close();

        fillQueue(env, charlie);
        checkMetrics(__LINE__, env, 0, limit * 2, limit + 1, limit, 256);

        aliceSeq = env.seq(alice);
        aliceBal = env.balance(alice);
        BEAST_EXPECT(aliceBal == drops(30));

        env(noop(alice), fee(drops(25)), queued);
        env(noop(alice), seq(aliceSeq + 1), ter(terINSUF_FEE_B));
        BEAST_EXPECT(env.balance(alice) == drops(30));

        checkMetrics(__LINE__, env, 1, limit * 2, limit + 1, limit, 256);

        env.close();
        ++limit;
        checkMetrics(__LINE__, env, 0, limit * 2, 1, limit, 256);
        BEAST_EXPECT(env.balance(alice) == drops(5));
    }

    void
    testConsequences()
    {
        using namespace jtx;
        using namespace std::chrono;
        testcase("consequences");

        Env env(*this);
        auto const alice = Account("alice");
        env.memoize(alice);
        env.memoize("bob");
        env.memoize("carol");
        {
            auto const jtx = env.jt(offer_cancel(alice, 3), seq(5), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto USD = alice["USD"];

            auto const jtx =
                env.jt(trust("carol", USD(50000000)), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx = env.jt(ticket::create(alice, 1), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }
    }

    void
    testAcctInQueueButEmpty()
    {
        // It is possible for an account to be present in the queue but have
        // no queued transactions.  This has been the source of at least one
        // bug where an insufficiently informed developer assumed that if an
        // account was present in the queue then it also had at least one
        // queued transaction.
        //
        // This test does touch testing to verify that, at least, that bug
        // is addressed.
        using namespace jtx;
        testcase("acct in queue but empty");

        auto alice = Account("alice");
        auto bob = Account("bob");
        auto charlie = Account("charlie");

        auto queued = ter(terQUEUED);

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // Fund accounts while the fee is cheap so they all apply.
        env.fund(XRP(50000), noripple(alice, bob, charlie));
        checkMetrics(__LINE__, env, 0, std::nullopt, 3, 3, 256);

        // Alice - no fee change yet
        env(noop(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        // Bob with really high fee - applies
        env(noop(bob), openLedgerFee(env));
        checkMetrics(__LINE__, env, 0, std::nullopt, 5, 3, 256);

        // Charlie with low fee: queued
        env(noop(charlie), fee(1000), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 5, 3, 256);

        env.close();
        // Verify that the queued transaction was applied
        checkMetrics(__LINE__, env, 0, 10, 1, 5, 256);

        /////////////////////////////////////////////////////////////////

        // Stuff the ledger and queue so we can verify that
        // stuff gets kicked out.
        env(noop(bob), fee(1000));
        env(noop(bob), fee(1000));
        env(noop(bob), fee(1000));
        env(noop(bob), fee(1000));
        env(noop(bob), fee(1000));
        checkMetrics(__LINE__, env, 0, 10, 6, 5, 256);

        // Use explicit fees so we can control which txn
        // will get dropped
        // This one gets into the queue, but gets dropped when the
        // higher fee one is added later.
        std::uint32_t const charlieSeq{env.seq(charlie)};
        env(noop(charlie), fee(15), seq(charlieSeq), queued);

        // These stay in the queue.
        std::uint32_t aliceSeq{env.seq(alice)};
        std::uint32_t bobSeq{env.seq(bob)};

        env(noop(alice), fee(16), seq(aliceSeq++), queued);
        env(noop(bob), fee(16), seq(bobSeq++), queued);
        env(noop(alice), fee(17), seq(aliceSeq++), queued);
        env(noop(bob), fee(17), seq(bobSeq++), queued);
        env(noop(alice), fee(18), seq(aliceSeq++), queued);
        env(noop(bob), fee(19), seq(bobSeq++), queued);
        env(noop(alice), fee(20), seq(aliceSeq++), queued);
        env(noop(bob), fee(20), seq(bobSeq++), queued);
        env(noop(alice), fee(21), seq(aliceSeq++), queued);

        // Queue is full now.
        checkMetrics(__LINE__, env, 10, 10, 6, 5, 385);

        // Try to add another transaction with the default (low) fee,
        // it should fail because the queue is full.
        env(noop(alice), seq(aliceSeq++), ter(telCAN_NOT_QUEUE_FULL));

        // Add another transaction, with a higher fee,
        // not high enough to get into the ledger, but high
        // enough to get into the queue (and kick Charlie's out)
        env(noop(bob), fee(22), seq(bobSeq++), queued);

        /////////////////////////////////////////////////////////

        // That was the setup for the actual test :-).  Now make
        // sure we get the right results if we try to add a
        // transaction for Charlie (who's in the queue, but has no queued
        // transactions) with the wrong sequence numbers.
        //
        // Charlie is paying a high enough fee to go straight into the
        // ledger in order to get into the vicinity of an assert which
        // should no longer fire :-).
        env(noop(charlie), fee(8000), seq(charlieSeq - 1), ter(tefPAST_SEQ));
        env(noop(charlie), fee(8000), seq(charlieSeq + 1), ter(terPRE_SEQ));
        env(noop(charlie), fee(8000), seq(charlieSeq), ter(tesSUCCESS));
    }

    void
    testRPC()
    {
        using namespace jtx;
        testcase("rpc");

        Env env(*this);

        auto fee = env.rpc("fee");

        if (BEAST_EXPECT(fee.isMember(jss::result)) &&
            BEAST_EXPECT(!RPC::contains_error(fee[jss::result])))
        {
            auto const& result = fee[jss::result];
            BEAST_EXPECT(
                result.isMember(jss::ledger_current_index) &&
                result[jss::ledger_current_index] == 3);
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
            BEAST_EXPECT(
                result.isMember(jss::ledger_current_index) &&
                result[jss::ledger_current_index] == 4);
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

    void
    testExpirationReplacement()
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
        testcase("expiration replacement");

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "1"},
                 {"ledgers_in_queue", "10"},
                 {"maximum_txn_per_account", "20"}}));

        // Alice will recreate the scenario. Bob will block.
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(500000), noripple(alice, bob));
        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 1, 256);

        auto const aliceSeq = env.seq(alice);
        BEAST_EXPECT(env.current()->info().seq == 3);
        env(noop(alice),
            seq(aliceSeq),
            json(R"({"LastLedgerSequence":5})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 1),
            json(R"({"LastLedgerSequence":5})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 2),
            json(R"({"LastLedgerSequence":10})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 3),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        checkMetrics(__LINE__, env, 4, std::nullopt, 2, 1, 256);
        auto const bobSeq = env.seq(bob);
        // Ledger 4 gets 3,
        // Ledger 5 gets 4,
        // Ledger 6 gets 5.
        for (int i = 0; i < 3 + 4 + 5; ++i)
        {
            env(noop(bob), seq(bobSeq + i), fee(200), ter(terQUEUED));
        }
        checkMetrics(__LINE__, env, 4 + 3 + 4 + 5, std::nullopt, 2, 1, 256);
        // Close ledger 3
        env.close();
        checkMetrics(__LINE__, env, 4 + 4 + 5, 20, 3, 2, 256);
        // Close ledger 4
        env.close();
        checkMetrics(__LINE__, env, 4 + 5, 30, 4, 3, 256);
        // Close ledger 5
        env.close();
        // Alice's first two txs expired.
        checkMetrics(__LINE__, env, 2, 40, 5, 4, 256);

        // Because aliceSeq is missing, aliceSeq + 1 fails
        env(noop(alice), seq(aliceSeq + 1), ter(terPRE_SEQ));

        // Cannot fill the gap with a blocker since Alice's queue is not empty.
        env(fset(alice, asfAccountTxnID),
            seq(aliceSeq),
            ter(telCAN_NOT_QUEUE_BLOCKS));
        checkMetrics(__LINE__, env, 2, 40, 5, 4, 256);

        // However we can fill the gap with a non-blocker.
        env(noop(alice), seq(aliceSeq), fee(20), ter(terQUEUED));
        checkMetrics(__LINE__, env, 3, 40, 5, 4, 256);

        // Attempt to queue up a new aliceSeq + 1 tx that's a blocker.
        env(fset(alice, asfAccountTxnID),
            seq(aliceSeq + 1),
            ter(telCAN_NOT_QUEUE_BLOCKS));
        checkMetrics(__LINE__, env, 3, 40, 5, 4, 256);

        // Queue up a non-blocker replacement for aliceSeq + 1.
        env(noop(alice), seq(aliceSeq + 1), fee(20), ter(terQUEUED));
        checkMetrics(__LINE__, env, 4, 40, 5, 4, 256);

        // Close ledger 6
        env.close();
        // We expect that all of alice's queued tx's got into
        // the open ledger.
        checkMetrics(__LINE__, env, 0, 50, 4, 5, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 4);
    }

    void
    testFullQueueGapFill()
    {
        // This test focuses on which gaps in queued transactions are
        // allowed to be filled even when the account's queue is full.
        using namespace jtx;
        testcase("full queue gap handling");

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "1"},
                 {"ledgers_in_queue", "10"},
                 {"maximum_txn_per_account", "11"}}));

        // Alice will have the gaps.  Bob will keep the queue busy with
        // high fee transactions so alice's transactions can expire to leave
        // gaps.
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(500000), noripple(alice, bob));
        checkMetrics(__LINE__, env, 0, std::nullopt, 2, 1, 256);

        auto const aliceSeq = env.seq(alice);
        BEAST_EXPECT(env.current()->info().seq == 3);

        // Start by procuring tickets for alice to use to keep her queue full
        // without affecting the sequence gap that will appear later.
        env(ticket::create(alice, 11),
            seq(aliceSeq + 0),
            fee(201),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 11),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 12),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 13),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 14),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 15),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 16),
            json(R"({"LastLedgerSequence": 5})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 17),
            json(R"({"LastLedgerSequence": 5})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 18),
            json(R"({"LastLedgerSequence": 5})"),
            ter(terQUEUED));
        env(noop(alice),
            seq(aliceSeq + 19),
            json(R"({"LastLedgerSequence":11})"),
            ter(terQUEUED));
        checkMetrics(__LINE__, env, 10, std::nullopt, 2, 1, 256);

        auto const bobSeq = env.seq(bob);
        // Ledger 4 gets 2 from bob and 1 from alice,
        // Ledger 5 gets 4 from bob,
        // Ledger 6 gets 5 from bob.
        for (int i = 0; i < 2 + 4 + 5; ++i)
        {
            env(noop(bob), seq(bobSeq + i), fee(200), ter(terQUEUED));
        }
        checkMetrics(__LINE__, env, 10 + 2 + 4 + 5, std::nullopt, 2, 1, 256);
        // Close ledger 3
        env.close();
        checkMetrics(__LINE__, env, 9 + 4 + 5, 20, 3, 2, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 12);

        // Close ledger 4
        env.close();
        checkMetrics(__LINE__, env, 9 + 5, 30, 4, 3, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 12);

        // Close ledger 5
        env.close();
        // Three of Alice's txs expired.
        checkMetrics(__LINE__, env, 6, 40, 5, 4, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 12);

        // Top off Alice's queue again using Tickets so the sequence gap is
        // unaffected.
        env(noop(alice), ticket::use(aliceSeq + 1), ter(terQUEUED));
        env(noop(alice), ticket::use(aliceSeq + 2), ter(terQUEUED));
        env(noop(alice), ticket::use(aliceSeq + 3), ter(terQUEUED));
        env(noop(alice), ticket::use(aliceSeq + 4), ter(terQUEUED));
        env(noop(alice), ticket::use(aliceSeq + 5), ter(terQUEUED));
        env(noop(alice), ticket::use(aliceSeq + 6), ter(telCAN_NOT_QUEUE_FULL));
        checkMetrics(__LINE__, env, 11, 40, 5, 4, 256);

        // Even though alice's queue is full we can still slide in a couple
        // more transactions because she has a sequence gap.  But we
        // can only install a transaction that fills the bottom of the gap.
        // Explore that...

        // Verify that we can't queue a sequence-based transaction that
        // follows a gap.
        env(noop(alice), seq(aliceSeq + 20), ter(telCAN_NOT_QUEUE_FULL));

        // Verify that the transaction in front of the gap is still present
        // by attempting to replace it without a sufficient fee.
        env(noop(alice), seq(aliceSeq + 15), ter(telCAN_NOT_QUEUE_FEE));

        // We can't queue a transaction into the middle of the gap.  It must
        // go at the front.
        env(noop(alice), seq(aliceSeq + 18), ter(telCAN_NOT_QUEUE_FULL));
        env(noop(alice), seq(aliceSeq + 17), ter(telCAN_NOT_QUEUE_FULL));

        // Successfully put this transaction into the front of the gap.
        env(noop(alice), seq(aliceSeq + 16), ter(terQUEUED));

        // Still can't put a sequence-based transaction at the end of the gap.
        env(noop(alice), seq(aliceSeq + 18), ter(telCAN_NOT_QUEUE_FULL));

        // But we can still fill the gap from the front.
        env(noop(alice), seq(aliceSeq + 17), ter(terQUEUED));

        // Finally we can fill in the entire gap.
        env(noop(alice), seq(aliceSeq + 18), ter(terQUEUED));
        checkMetrics(__LINE__, env, 14, 40, 5, 4, 256);

        // Verify that nothing can be added now that the gap is filled.
        env(noop(alice), seq(aliceSeq + 20), ter(telCAN_NOT_QUEUE_FULL));

        // Close ledger 6.  That removes some of alice's transactions,
        // but alice adds some more transaction(s) so expectedCount
        // may not reduce to 8.
        env.close();
        checkMetrics(__LINE__, env, 9, 50, 6, 5, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 13);

        // Close ledger 7.  That should remove 7 more of alice's transactions.
        env.close();
        checkMetrics(__LINE__, env, 2, 60, 7, 6, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 19);

        // Close one last ledger to see all of alice's transactions moved
        // into the ledger, including the tickets
        env.close();
        checkMetrics(__LINE__, env, 0, 70, 2, 7, 256);
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 21);
    }

    void
    testSignAndSubmitSequence()
    {
        testcase("Autofilled sequence should account for TxQ");
        using namespace jtx;
        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "6"}}));
        Env_ss envs(env);
        auto const& txQ = env.app().getTxQ();

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(100000), alice, bob);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, std::nullopt, 7, 6, 256);

        // Queue up several transactions for alice sign-and-submit
        auto const aliceSeq = env.seq(alice);
        auto const lastLedgerSeq = env.current()->info().seq + 2;

        auto submitParams = Json::Value(Json::objectValue);
        for (int i = 0; i < 5; ++i)
        {
            if (i == 2)
                envs(
                    noop(alice),
                    fee(1000),
                    seq(none),
                    json(jss::LastLedgerSequence, lastLedgerSeq),
                    ter(terQUEUED))(submitParams);
            else
                envs(noop(alice), fee(1000), seq(none), ter(terQUEUED))(
                    submitParams);
        }
        checkMetrics(__LINE__, env, 5, std::nullopt, 7, 6, 256);
        {
            auto aliceStat = txQ.getAccountTxs(alice.id());
            SeqProxy seq = SeqProxy::sequence(aliceSeq);
            BEAST_EXPECT(aliceStat.size() == 5);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.seqProxy == seq);
                BEAST_EXPECT(tx.feeLevel == FeeLevel64{25600});
                if (seq.value() == aliceSeq + 2)
                {
                    BEAST_EXPECT(
                        tx.lastValid && *tx.lastValid == lastLedgerSeq);
                }
                else
                {
                    BEAST_EXPECT(!tx.lastValid);
                }
                seq.advanceBy(1);
            }
        }
        // Put some txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        for (int i = 0; i < 8; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(__LINE__, env, 13, std::nullopt, 7, 6, 256);

        env.close();
        checkMetrics(__LINE__, env, 5, 14, 8, 7, 256);
        // Put some more txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        fillQueue(env, bob);
        for (int i = 0; i < 9; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(__LINE__, env, 14, 14, 8, 7, 25601);
        env.close();
        // Put some more txs in the queue for bob.
        // Give them a higher fee so they'll beat alice's.
        fillQueue(env, bob);
        for (int i = 0; i < 10; ++i)
            envs(noop(bob), fee(2000), seq(none), ter(terQUEUED))();
        checkMetrics(__LINE__, env, 15, 16, 9, 8, 256);
        env.close();
        checkMetrics(__LINE__, env, 4, 18, 10, 9, 256);
        {
            // Bob has nothing left in the queue.
            auto bobStat = txQ.getAccountTxs(bob.id());
            BEAST_EXPECT(bobStat.empty());
        }
        // Verify alice's tx got dropped as we BEAST_EXPECT, and that there's
        // a gap in her queued txs.
        {
            auto aliceStat = txQ.getAccountTxs(alice.id());
            auto seq = aliceSeq;
            BEAST_EXPECT(aliceStat.size() == 4);
            for (auto const& tx : aliceStat)
            {
                // Skip over the missing one.
                if (seq == aliceSeq + 2)
                    ++seq;

                BEAST_EXPECT(tx.seqProxy.isSeq() && tx.seqProxy.value() == seq);
                BEAST_EXPECT(tx.feeLevel == FeeLevel64{25600});
                BEAST_EXPECT(!tx.lastValid);
                ++seq;
            }
        }
        // Now, fill the gap.
        envs(noop(alice), fee(1000), seq(none), ter(terQUEUED))(submitParams);
        checkMetrics(__LINE__, env, 5, 18, 10, 9, 256);
        {
            auto aliceStat = txQ.getAccountTxs(alice.id());
            auto seq = aliceSeq;
            BEAST_EXPECT(aliceStat.size() == 5);
            for (auto const& tx : aliceStat)
            {
                BEAST_EXPECT(tx.seqProxy.isSeq() && tx.seqProxy.value() == seq);
                BEAST_EXPECT(tx.feeLevel == FeeLevel64{25600});
                BEAST_EXPECT(!tx.lastValid);
                ++seq;
            }
        }

        env.close();
        checkMetrics(__LINE__, env, 0, 20, 5, 10, 256);
        {
            // Bob's data has been cleaned up.
            auto bobStat = txQ.getAccountTxs(bob.id());
            BEAST_EXPECT(bobStat.empty());
        }
        {
            auto aliceStat = txQ.getAccountTxs(alice.id());
            BEAST_EXPECT(aliceStat.empty());
        }
    }

    void
    testAccountInfo()
    {
        using namespace jtx;
        testcase("account info");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));
        Env_ss envs(env);

        Account const alice{"alice"};
        env.fund(XRP(1000000), alice);
        env.close();

        auto const withQueue =
            R"({ "account": ")" + alice.human() + R"(", "queue": true })";
        auto const withoutQueue = R"({ "account": ")" + alice.human() + R"("})";
        auto const prevLedgerWithQueue = R"({ "account": ")" + alice.human() +
            R"(", "queue": true, "ledger_index": 3 })";
        BEAST_EXPECT(env.current()->info().seq > 3);

        {
            // account_info without the "queue" argument.
            auto const info = env.rpc("json", "account_info", withoutQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result].isMember(jss::queue_data));
        }
        {
            // account_info with the "queue" argument.
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
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
        checkMetrics(__LINE__, env, 0, 6, 0, 3, 256);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
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
        checkMetrics(__LINE__, env, 4, 6, 4, 3, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 4);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(
                queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(
                queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(queue_data[jss::auth_change_queued] == false);
            BEAST_EXPECT(queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data[jss::max_spend_drops_total] == "400");
            BEAST_EXPECT(queue_data.isMember(jss::transactions));
            auto const& queued = queue_data[jss::transactions];
            BEAST_EXPECT(queued.size() == queue_data[jss::txn_count]);
            for (unsigned i = 0; i < queued.size(); ++i)
            {
                auto const& item = queued[i];
                BEAST_EXPECT(item[jss::seq] == data[jss::Sequence].asInt() + i);
                BEAST_EXPECT(item[jss::fee_level] == "2560");
                BEAST_EXPECT(!item.isMember(jss::LastLedgerSequence));

                BEAST_EXPECT(item.isMember(jss::fee));
                BEAST_EXPECT(item[jss::fee] == "100");
                BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                BEAST_EXPECT(item.isMember(jss::auth_change));
                BEAST_EXPECT(item[jss::auth_change].asBool() == false);
            }
        }

        // Drain the queue so we can queue up a blocker.
        env.close();
        checkMetrics(__LINE__, env, 0, 8, 4, 4, 256);

        // Fill the ledger and then queue up a blocker.
        envs(noop(alice), seq(none))(submitParams);

        envs(
            fset(alice, asfAccountTxnID),
            fee(100),
            seq(none),
            json(jss::LastLedgerSequence, 10),
            ter(terQUEUED))(submitParams);
        checkMetrics(__LINE__, env, 1, 8, 5, 4, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 1);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(
                queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(
                queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(queue_data[jss::auth_change_queued] == true);
            BEAST_EXPECT(queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data[jss::max_spend_drops_total] == "100");
            BEAST_EXPECT(queue_data.isMember(jss::transactions));
            auto const& queued = queue_data[jss::transactions];
            BEAST_EXPECT(queued.size() == queue_data[jss::txn_count]);
            for (unsigned i = 0; i < queued.size(); ++i)
            {
                auto const& item = queued[i];
                BEAST_EXPECT(item[jss::seq] == data[jss::Sequence].asInt() + i);
                BEAST_EXPECT(item[jss::fee_level] == "2560");
                BEAST_EXPECT(item.isMember(jss::fee));
                BEAST_EXPECT(item[jss::fee] == "100");
                BEAST_EXPECT(item.isMember(jss::max_spend_drops));
                BEAST_EXPECT(item[jss::max_spend_drops] == "100");
                BEAST_EXPECT(item.isMember(jss::auth_change));

                if (i == queued.size() - 1)
                {
                    BEAST_EXPECT(item[jss::auth_change].asBool() == true);
                    BEAST_EXPECT(item.isMember(jss::LastLedgerSequence));
                    BEAST_EXPECT(item[jss::LastLedgerSequence] == 10);
                }
                else
                {
                    BEAST_EXPECT(item[jss::auth_change].asBool() == false);
                    BEAST_EXPECT(!item.isMember(jss::LastLedgerSequence));
                }
            }
        }

        envs(noop(alice), fee(100), seq(none), ter(telCAN_NOT_QUEUE_BLOCKED))(
            submitParams);
        checkMetrics(__LINE__, env, 1, 8, 5, 4, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& result = info[jss::result];
            auto const& data = result[jss::account_data];
            BEAST_EXPECT(result.isMember(jss::queue_data));
            auto const& queue_data = result[jss::queue_data];
            BEAST_EXPECT(queue_data.isObject());
            BEAST_EXPECT(queue_data.isMember(jss::txn_count));
            BEAST_EXPECT(queue_data[jss::txn_count] == 1);
            BEAST_EXPECT(queue_data.isMember(jss::lowest_sequence));
            BEAST_EXPECT(
                queue_data[jss::lowest_sequence] == data[jss::Sequence]);
            BEAST_EXPECT(queue_data.isMember(jss::highest_sequence));
            BEAST_EXPECT(
                queue_data[jss::highest_sequence] ==
                data[jss::Sequence].asUInt() +
                    queue_data[jss::txn_count].asUInt() - 1);
            BEAST_EXPECT(queue_data.isMember(jss::auth_change_queued));
            BEAST_EXPECT(queue_data[jss::auth_change_queued].asBool());
            BEAST_EXPECT(queue_data.isMember(jss::max_spend_drops_total));
            BEAST_EXPECT(queue_data[jss::max_spend_drops_total] == "100");
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
            auto const info =
                env.rpc("json", "account_info", prevLedgerWithQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                RPC::contains_error(info[jss::result]));
        }

        env.close();
        checkMetrics(__LINE__, env, 0, 10, 2, 5, 256);
        env.close();
        checkMetrics(__LINE__, env, 0, 10, 0, 5, 256);

        {
            auto const info = env.rpc("json", "account_info", withQueue);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
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

    void
    testServerInfo()
    {
        using namespace jtx;
        testcase("server info");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));
        Env_ss envs(env);

        Account const alice{"alice"};
        env.fund(XRP(1000000), alice);
        env.close();

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(
                server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            BEAST_EXPECT(
                info.isMember(jss::load_factor) && info[jss::load_factor] == 1);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(!info.isMember(jss::load_factor_fee_escalation));
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(
                state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_base) && state[jss::load_base] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        checkMetrics(__LINE__, env, 0, 6, 0, 3, 256);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

        auto aliceSeq = env.seq(alice);
        auto submitParams = Json::Value(Json::objectValue);
        for (auto i = 0; i < 4; ++i)
            envs(noop(alice), fee(100), seq(aliceSeq + i), ter(terQUEUED))(
                submitParams);
        checkMetrics(__LINE__, env, 4, 6, 4, 3, 256);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(
                server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(
                info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 888.88 &&
                info[jss::load_factor] < 888.89);
            BEAST_EXPECT(
                info.isMember(jss::load_factor_server) &&
                info[jss::load_factor_server] == 1);
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(
                info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(
                state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 227555);
            BEAST_EXPECT(
                state.isMember(jss::load_base) && state[jss::load_base] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.app().getFeeTrack().setRemoteFee(256000);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(
                server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(
                info.isMember(jss::load_factor) &&
                info[jss::load_factor] == 1000);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(!info.isMember(jss::load_factor_local));
            BEAST_EXPECT(
                info.isMember(jss::load_factor_net) &&
                info[jss::load_factor_net] == 1000);
            BEAST_EXPECT(
                info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(
                state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 256000);
            BEAST_EXPECT(
                state.isMember(jss::load_base) && state[jss::load_base] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] == 256000);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.app().getFeeTrack().setRemoteFee(256);

        // Increase the server load
        for (int i = 0; i < 5; ++i)
            env.app().getFeeTrack().raiseLocalFee();
        BEAST_EXPECT(env.app().getFeeTrack().getLoadFactor() == 625);

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(
                server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.
            BEAST_EXPECT(
                info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 888.88 &&
                info[jss::load_factor] < 888.89);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 1.
            BEAST_EXPECT(
                info.isMember(jss::load_factor_server) &&
                info[jss::load_factor_server] > 1.245 &&
                info[jss::load_factor_server] < 2.4415);
            BEAST_EXPECT(
                info.isMember(jss::load_factor_local) &&
                info[jss::load_factor_local] > 1.245 &&
                info[jss::load_factor_local] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(
                info.isMember(jss::load_factor_fee_escalation) &&
                info[jss::load_factor_fee_escalation] > 888.88 &&
                info[jss::load_factor_fee_escalation] < 888.89);
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(
                state.isMember(jss::load_factor) &&
                state[jss::load_factor] == 227555);
            BEAST_EXPECT(
                state.isMember(jss::load_base) && state[jss::load_base] == 256);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 256.
            BEAST_EXPECT(
                state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] >= 320 &&
                state[jss::load_factor_server] <= 625);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 227555);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }

        env.close();

        {
            auto const server_info = env.rpc("server_info");
            BEAST_EXPECT(
                server_info.isMember(jss::result) &&
                server_info[jss::result].isMember(jss::info));
            auto const& info = server_info[jss::result][jss::info];
            // Avoid double rounding issues by comparing to a range.

            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 1.
            BEAST_EXPECT(
                info.isMember(jss::load_factor) &&
                info[jss::load_factor] > 1.245 &&
                info[jss::load_factor] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_server));
            BEAST_EXPECT(
                info.isMember(jss::load_factor_local) &&
                info[jss::load_factor_local] > 1.245 &&
                info[jss::load_factor_local] < 2.4415);
            BEAST_EXPECT(!info.isMember(jss::load_factor_net));
            BEAST_EXPECT(!info.isMember(jss::load_factor_fee_escalation));
        }
        {
            auto const server_state = env.rpc("server_state");
            auto const& state = server_state[jss::result][jss::state];
            BEAST_EXPECT(
                state.isMember(jss::load_factor) &&
                state[jss::load_factor] >= 320 &&
                state[jss::load_factor] <= 625);
            BEAST_EXPECT(
                state.isMember(jss::load_base) && state[jss::load_base] == 256);
            // There can be a race between LoadManager lowering the fee,
            // and the call to server_info, so check a wide range.
            // The important thing is that it's not 256.
            BEAST_EXPECT(
                state.isMember(jss::load_factor_server) &&
                state[jss::load_factor_server] >= 320 &&
                state[jss::load_factor_server] <= 625);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_escalation) &&
                state[jss::load_factor_fee_escalation] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_queue) &&
                state[jss::load_factor_fee_queue] == 256);
            BEAST_EXPECT(
                state.isMember(jss::load_factor_fee_reference) &&
                state[jss::load_factor_fee_reference] == 256);
        }
    }

    void
    testServerSubscribe()
    {
        using namespace jtx;
        testcase("server subscribe");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        Json::Value stream;
        stream[jss::streams] = Json::arrayValue;
        stream[jss::streams].append("server");
        auto wsc = makeWSClient(env.app().config());
        {
            auto jv = wsc->invoke("subscribe", stream);
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        Account a{"a"}, b{"b"}, c{"c"}, d{"d"}, e{"e"}, f{"f"}, g{"g"}, h{"h"},
            i{"i"};

        // Fund the first few accounts at non escalated fee
        env.fund(XRP(50000), noripple(a, b, c, d));
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        // First transaction establishes the messaging
        using namespace std::chrono_literals;
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) && jv[jss::load_factor] == 256 &&
                jv.isMember(jss::load_base) && jv[jss::load_base] == 256 &&
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
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 227555 && jv.isMember(jss::load_base) &&
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
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) && jv[jss::load_factor] == 256 &&
                jv.isMember(jss::load_base) && jv[jss::load_base] == 256 &&
                jv.isMember(jss::load_factor_server) &&
                jv[jss::load_factor_server] == 256 &&
                jv.isMember(jss::load_factor_fee_escalation) &&
                jv[jss::load_factor_fee_escalation] == 256 &&
                jv.isMember(jss::load_factor_fee_queue) &&
                jv[jss::load_factor_fee_queue] == 256 &&
                jv.isMember(jss::load_factor_fee_reference) &&
                jv[jss::load_factor_fee_reference] == 256;
        }));

        checkMetrics(__LINE__, env, 0, 8, 0, 4, 256);

        // Fund then next few accounts at non escalated fee
        env.fund(XRP(50000), noripple(e, f, g, h, i));

        // Extra transactions with low fee are queued
        auto queued = ter(terQUEUED);
        env(noop(a), fee(10), queued);
        env(noop(b), fee(10), queued);
        env(noop(c), fee(10), queued);
        env(noop(d), fee(10), queued);
        env(noop(e), fee(10), queued);
        env(noop(f), fee(10), queued);
        env(noop(g), fee(10), queued);
        checkMetrics(__LINE__, env, 7, 8, 5, 4, 256);

        // Last transaction escalates the fee
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 200000 && jv.isMember(jss::load_base) &&
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
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) &&
                jv[jss::load_factor] == 184320 && jv.isMember(jss::load_base) &&
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
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus" &&
                jv.isMember(jss::load_factor) && jv[jss::load_factor] == 256 &&
                jv.isMember(jss::load_base) && jv[jss::load_base] == 256 &&
                jv.isMember(jss::load_factor_server) &&
                jv[jss::load_factor_server] == 256 &&
                jv.isMember(jss::load_factor_fee_escalation) &&
                jv[jss::load_factor_fee_escalation] == 256 &&
                jv.isMember(jss::load_factor_fee_queue) &&
                jv[jss::load_factor_fee_queue] == 256 &&
                jv.isMember(jss::load_factor_fee_reference) &&
                jv[jss::load_factor_fee_reference] == 256;
        }));

        BEAST_EXPECT(!wsc->findMsg(1s, [&](auto const& jv) {
            return jv[jss::type] == "serverStatus";
        }));

        auto jv = wsc->invoke("unsubscribe", stream);
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testClearQueuedAccountTxs()
    {
        using namespace jtx;
        testcase("clear queued acct txs");

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));
        auto alice = Account("alice");
        auto bob = Account("bob");

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);
        env.fund(XRP(50000000), alice, bob);

        fillQueue(env, alice);

        auto calcTotalFee = [&](std::int64_t alreadyPaid,
                                std::optional<std::size_t> numToClear =
                                    std::nullopt) -> std::uint64_t {
            auto totalFactor = 0;
            auto const metrics = env.app().getTxQ().getMetrics(*env.current());
            if (!numToClear)
                numToClear.emplace(metrics.txCount + 1);
            for (int i = 0; i < *numToClear; ++i)
            {
                auto inLedger = metrics.txInLedger + i;
                totalFactor += inLedger * inLedger;
            }
            auto result = toDrops(
                              metrics.medFeeLevel * totalFactor /
                                  (metrics.txPerLedger * metrics.txPerLedger),
                              env.current()->fees().base)
                              .drops();
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
            env(noop(alice),
                openLedgerFee(env),
                seq(aliceSeq++),
                ter(terQUEUED));

            checkMetrics(__LINE__, env, 3, std::nullopt, 4, 3, 256);

            // Figure out how much it would cost to cover all the
            // queued txs + itself
            std::uint64_t totalFee1 = calcTotalFee(100 * 2 + 8889);
            --totalFee1;

            BEAST_EXPECT(totalFee1 == 60911);
            // Submit a transaction with that fee. It will get queued
            // because the fee level calculation rounds down. This is
            // the edge case test.
            env(noop(alice), fee(totalFee1), seq(aliceSeq++), ter(terQUEUED));

            checkMetrics(__LINE__, env, 4, std::nullopt, 4, 3, 256);

            // Now repeat the process including the new tx
            // and avoiding the rounding error
            std::uint64_t const totalFee2 =
                calcTotalFee(100 * 2 + 8889 + 60911);
            BEAST_EXPECT(totalFee2 == 35556);
            // Submit a transaction with that fee. It will succeed.
            env(noop(alice), fee(totalFee2), seq(aliceSeq++));

            checkMetrics(__LINE__, env, 0, std::nullopt, 9, 3, 256);
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
            env(noop(alice),
                openLedgerFee(env),
                seq(aliceSeq++),
                ter(terQUEUED));

            checkMetrics(__LINE__, env, 3, std::nullopt, 9, 3, 256);

            // Figure out how much it would cost to cover all the
            // queued txs + itself
            auto const metrics = env.app().getTxQ().getMetrics(*env.current());
            std::uint64_t const totalFee =
                calcTotalFee(100 * 2, metrics.txCount);
            BEAST_EXPECT(totalFee == 167578);
            // Replacing the last tx with the large fee succeeds.
            --aliceSeq;
            env(noop(alice), fee(totalFee), seq(aliceSeq++));

            // The queue is clear
            checkMetrics(__LINE__, env, 0, std::nullopt, 12, 3, 256);

            env.close();
            checkMetrics(__LINE__, env, 0, 24, 0, 12, 256);
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

            checkMetrics(__LINE__, env, 5, 24, 13, 12, 256);

            // Figure out how much it would cost to cover 3 txns
            std::uint64_t const totalFee = calcTotalFee(100 * 2, 3);
            BEAST_EXPECT(totalFee == 20287);
            // Replacing the last tx with the large fee succeeds.
            aliceSeq -= 3;
            env(noop(alice), fee(totalFee), seq(aliceSeq++));

            checkMetrics(__LINE__, env, 2, 24, 16, 12, 256);
            auto const aliceQueue =
                env.app().getTxQ().getAccountTxs(alice.id());
            BEAST_EXPECT(aliceQueue.size() == 2);
            SeqProxy seq = SeqProxy::sequence(aliceSeq);
            for (auto const& tx : aliceQueue)
            {
                BEAST_EXPECT(tx.seqProxy == seq);
                BEAST_EXPECT(tx.feeLevel == FeeLevel64{2560});
                seq.advanceBy(1);
            }

            // Close the ledger to clear the queue
            env.close();
            checkMetrics(__LINE__, env, 0, 32, 2, 16, 256);
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

            checkMetrics(__LINE__, env, 4, 32, 17, 16, 256);

            // Figure out how much it would cost to cover all the txns
            //  + 1
            std::uint64_t const totalFee = calcTotalFee(200 * 2 + 22 * 2);
            BEAST_EXPECT(totalFee == 35006);
            // This fee should be enough, but oh no! Server load went up!
            auto& feeTrack = env.app().getFeeTrack();
            auto const origFee = feeTrack.getRemoteFee();
            feeTrack.setRemoteFee(origFee * 5);
            // Instead the tx gets queued, and all of the queued
            // txs stay in the queue.
            env(noop(alice), fee(totalFee), seq(aliceSeq++), ter(terQUEUED));

            // The original last transaction is still in the queue
            checkMetrics(__LINE__, env, 5, 32, 17, 16, 256);

            // With high load, some of the txs stay in the queue
            env.close();
            checkMetrics(__LINE__, env, 3, 34, 2, 17, 256);

            // Load drops back down
            feeTrack.setRemoteFee(origFee);

            // Because of the earlier failure, alice can not clear the queue,
            // no matter how high the fee
            fillQueue(env, bob);
            checkMetrics(__LINE__, env, 3, 34, 18, 17, 256);

            env(noop(alice), fee(XRP(1)), seq(aliceSeq++), ter(terQUEUED));
            checkMetrics(__LINE__, env, 4, 34, 18, 17, 256);

            // With normal load, those txs get into the ledger
            env.close();
            checkMetrics(__LINE__, env, 0, 36, 4, 18, 256);
        }
    }

    void
    testScaling()
    {
        using namespace jtx;
        using namespace std::chrono_literals;
        testcase("scaling");

        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger_standalone", "3"},
                     {"normal_consensus_increase_percent", "25"},
                     {"slow_consensus_decrease_percent", "50"},
                     {"target_txn_in_ledger", "10"},
                     {"maximum_txn_per_account", "200"}}));
            auto alice = Account("alice");

            checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);
            env.fund(XRP(50000000), alice);

            fillQueue(env, alice);
            checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);
            auto seqAlice = env.seq(alice);
            auto txCount = 140;
            for (int i = 0; i < txCount; ++i)
                env(noop(alice), seq(seqAlice++), ter(terQUEUED));
            checkMetrics(__LINE__, env, txCount, std::nullopt, 4, 3, 256);

            // Close a few ledgers successfully, so the limit grows

            env.close();
            // 4 + 25% = 5
            txCount -= 6;
            checkMetrics(__LINE__, env, txCount, 10, 6, 5, 257);

            env.close();
            // 6 + 25% = 7
            txCount -= 8;
            checkMetrics(__LINE__, env, txCount, 14, 8, 7, 257);

            env.close();
            // 8 + 25% = 10
            txCount -= 11;
            checkMetrics(__LINE__, env, txCount, 20, 11, 10, 257);

            env.close();
            // 11 + 25% = 13
            txCount -= 14;
            checkMetrics(__LINE__, env, txCount, 26, 14, 13, 257);

            env.close();
            // 14 + 25% = 17
            txCount -= 18;
            checkMetrics(__LINE__, env, txCount, 34, 18, 17, 257);

            env.close();
            // 18 + 25% = 22
            txCount -= 23;
            checkMetrics(__LINE__, env, txCount, 44, 23, 22, 257);

            env.close();
            // 23 + 25% = 28
            txCount -= 29;
            checkMetrics(__LINE__, env, txCount, 56, 29, 28, 256);

            // From 3 expected to 28 in 7 "fast" ledgers.

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 15;
            checkMetrics(__LINE__, env, txCount, 56, 15, 14, 256);

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 8;
            checkMetrics(__LINE__, env, txCount, 56, 8, 7, 256);

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(__LINE__, env, txCount, 56, 4, 3, 256);

            // From 28 expected back down to 3 in 3 "slow" ledgers.

            // Confirm the minimum sticks
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(__LINE__, env, txCount, 56, 4, 3, 256);

            BEAST_EXPECT(!txCount);
        }

        {
            Env env(
                *this,
                makeConfig(
                    {{"minimum_txn_in_ledger_standalone", "3"},
                     {"normal_consensus_increase_percent", "150"},
                     {"slow_consensus_decrease_percent", "150"},
                     {"target_txn_in_ledger", "10"},
                     {"maximum_txn_per_account", "200"}}));
            auto alice = Account("alice");

            checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);
            env.fund(XRP(50000000), alice);

            fillQueue(env, alice);
            checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);
            auto seqAlice = env.seq(alice);
            auto txCount = 43;
            for (int i = 0; i < txCount; ++i)
                env(noop(alice), seq(seqAlice++), ter(terQUEUED));
            checkMetrics(__LINE__, env, txCount, std::nullopt, 4, 3, 256);

            // Close a few ledgers successfully, so the limit grows

            env.close();
            // 4 + 150% = 10
            txCount -= 11;
            checkMetrics(__LINE__, env, txCount, 20, 11, 10, 257);

            env.close();
            // 11 + 150% = 27
            txCount -= 28;
            checkMetrics(__LINE__, env, txCount, 54, 28, 27, 256);

            // From 3 expected to 28 in 7 "fast" ledgers.

            // Close the ledger with a delay.
            env.close(env.now() + 5s, 10000ms);
            txCount -= 4;
            checkMetrics(__LINE__, env, txCount, 54, 4, 3, 256);

            // From 28 expected back down to 3 in 3 "slow" ledgers.

            BEAST_EXPECT(!txCount);
        }
    }

    void
    testInLedgerSeq()
    {
        // Test the situation where a transaction with an account and
        // sequence that's in the queue also appears in the ledger.
        //
        // Normally this situation can only happen on a network
        // when a transaction gets validated by most of the network,
        // but one or more nodes have that transaction (or a different
        // transaction with the same sequence) queued.  And, yes, this
        // situation has been observed (rarely) in the wild.
        testcase("Sequence in queue and open ledger");
        using namespace jtx;

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        auto const alice = Account("alice");

        auto const queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // Create account
        env.fund(XRP(50000), noripple(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 1, 3, 256);

        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        // Queue a transaction
        auto const aliceSeq = env.seq(alice);
        env(noop(alice), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 3, 256);

        // Now, apply a (different) transaction directly
        // to the open ledger, bypassing the queue
        // (This requires calling directly into the open ledger,
        //  which won't work if unit tests are separated to only
        //  be callable via RPC.)
        env.app().openLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const tx =
                env.jt(noop(alice), seq(aliceSeq), openLedgerFee(env));
            auto const result =
                ripple::apply(env.app(), view, *tx.stx, tapUNLIMITED, j);
            BEAST_EXPECT(result.first == tesSUCCESS && result.second);
            return result.second;
        });
        // the queued transaction is still there
        checkMetrics(__LINE__, env, 1, std::nullopt, 5, 3, 256);

        // The next transaction should be able to go into the open
        // ledger, even though aliceSeq is queued.  In earlier incarnations
        // of the TxQ this would cause an assert.
        env(noop(alice), seq(aliceSeq + 1), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, std::nullopt, 6, 3, 256);
        // Now queue a couple more transactions to make sure
        // they succeed despite aliceSeq being queued
        env(noop(alice), seq(aliceSeq + 2), queued);
        env(noop(alice), seq(aliceSeq + 3), queued);
        checkMetrics(__LINE__, env, 3, std::nullopt, 6, 3, 256);

        // Now close the ledger. One of the queued transactions
        // (aliceSeq) should be dropped.
        env.close();
        checkMetrics(__LINE__, env, 0, 12, 2, 6, 256);
    }

    void
    testInLedgerTicket()
    {
        // Test the situation where a transaction with an account and
        // ticket that's in the queue also appears in the ledger.
        //
        // Although this situation has not (yet) been observed in the wild,
        // it is a direct analogy to the previous sequence based test.  So
        // there is no reason to not expect to see it in the wild.
        testcase("Ticket in queue and open ledger");
        using namespace jtx;

        Env env(*this, makeConfig({{"minimum_txn_in_ledger_standalone", "3"}}));

        auto alice = Account("alice");

        auto queued = ter(terQUEUED);

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // Create account
        env.fund(XRP(50000), noripple(alice));
        checkMetrics(__LINE__, env, 0, std::nullopt, 1, 3, 256);

        // Create tickets
        std::uint32_t const tktSeq0{env.seq(alice) + 1};
        env(ticket::create(alice, 4));

        // Fill the queue so the next transaction will be queued.
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, std::nullopt, 4, 3, 256);

        // Queue a transaction with a ticket.  Leave an unused ticket
        // on either side.
        env(noop(alice), ticket::use(tktSeq0 + 1), queued);
        checkMetrics(__LINE__, env, 1, std::nullopt, 4, 3, 256);

        // Now, apply a (different) transaction directly
        // to the open ledger, bypassing the queue
        // (This requires calling directly into the open ledger,
        //  which won't work if unit tests are separated to only
        //  be callable via RPC.)
        env.app().openLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const tx = env.jt(
                noop(alice), ticket::use(tktSeq0 + 1), openLedgerFee(env));
            auto const result =
                ripple::apply(env.app(), view, *tx.stx, tapUNLIMITED, j);
            BEAST_EXPECT(result.first == tesSUCCESS && result.second);
            return result.second;
        });
        // the queued transaction is still there
        checkMetrics(__LINE__, env, 1, std::nullopt, 5, 3, 256);

        // The next (sequence-based) transaction should be able to go into
        // the open ledger, even though tktSeq0 is queued.  Note that this
        // sequence-based transaction goes in front of the queued
        // transaction, so the queued transaction is left in the queue.
        env(noop(alice), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, std::nullopt, 6, 3, 256);

        // We should be able to do the same thing with a ticket that goes
        // if front of the queued transaction.  This one too will leave
        // the queued transaction in place.
        env(noop(alice), ticket::use(tktSeq0 + 0), openLedgerFee(env));
        checkMetrics(__LINE__, env, 1, std::nullopt, 7, 3, 256);

        // We have one ticketed transaction in the queue.  We should able
        // to add another to the queue.
        env(noop(alice), ticket::use(tktSeq0 + 2), queued);
        checkMetrics(__LINE__, env, 2, std::nullopt, 7, 3, 256);

        // Here we try to force the queued transactions into the ledger by
        // adding one more queued (ticketed) transaction that pays enough
        // so fee averaging kicks in.  It doesn't work.  It only succeeds in
        // forcing just the one ticketed transaction into the ledger.
        //
        // The fee averaging functionality makes sense for sequence-based
        // transactions because if there are several sequence-based
        // transactions queued, the transactions in front must go into the
        // ledger before the later ones can go in.
        //
        // Fee averaging does not make sense with tickets.  Every ticketed
        // transaction is equally capable of going into the ledger independent
        // of all other ticket- or sequence-based transactions.
        env(noop(alice), ticket::use(tktSeq0 + 3), fee(XRP(1)));
        checkMetrics(__LINE__, env, 2, std::nullopt, 8, 3, 256);

        // Now close the ledger. One of the queued transactions
        // (the one with tktSeq0 + 1) should be dropped.
        env.close();
        checkMetrics(__LINE__, env, 0, 16, 1, 8, 256);
    }

    void
    testReexecutePreflight()
    {
        // The TxQ caches preflight results.  But there are situations where
        // that cache must become invalidated, like if amendments change.
        //
        // This test puts transactions into the TxQ and then enables an
        // amendment.  We won't really see much interesting here in the unit
        // test, but the code that checks for cache invalidation should be
        // exercised.  You can see that in improved code coverage,
        testcase("Re-execute preflight");
        using namespace jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const daria("daria");
        Account const ellie("ellie");
        Account const fiona("fiona");

        auto cfg = makeConfig(
            {{"minimum_txn_in_ledger_standalone", "1"},
             {"ledgers_in_queue", "5"},
             {"maximum_txn_per_account", "10"}},
            {{"account_reserve", "1000"}, {"owner_reserve", "50"}});

        Env env(*this, std::move(cfg));

        env.fund(XRP(10000), alice);
        env.close();
        env.fund(XRP(10000), bob);
        env.close();
        env.fund(XRP(10000), carol);
        env.close();
        env.fund(XRP(10000), daria);
        env.close();
        env.fund(XRP(10000), ellie);
        env.close();
        env.fund(XRP(10000), fiona);
        env.close();
        checkMetrics(__LINE__, env, 0, 10, 0, 2, 256);

        // Close ledgers until the amendments show up.
        int i = 0;
        for (i = 0; i <= 257; ++i)
        {
            env.close();
            if (!getMajorityAmendments(*env.closed()).empty())
                break;
        }
        auto expectedPerLedger = ripple::detail::numUpVotedAmendments() + 1;
        checkMetrics(
            __LINE__, env, 0, 5 * expectedPerLedger, 0, expectedPerLedger, 256);

        // Now wait 2 weeks modulo 256 ledgers for the amendments to be
        // enabled.  Speed the process by closing ledgers every 80 minutes,
        // which should get us to just past 2 weeks after 256 ledgers.
        using namespace std::chrono_literals;
        auto closeDuration = 80min;
        for (i = 0; i <= 255; ++i)
            env.close(closeDuration);

        // We're very close to the flag ledger.  Fill the ledger.
        fillQueue(env, alice);
        checkMetrics(
            __LINE__,
            env,
            0,
            5 * expectedPerLedger,
            expectedPerLedger + 1,
            expectedPerLedger,
            256);

        // Fill everyone's queues.
        auto seqAlice = env.seq(alice);
        auto seqBob = env.seq(bob);
        auto seqCarol = env.seq(carol);
        auto seqDaria = env.seq(daria);
        auto seqEllie = env.seq(ellie);
        auto seqFiona = env.seq(fiona);
        // Use fees to guarantee order
        int txFee{90};
        for (int i = 0; i < 10; ++i)
        {
            env(noop(alice), seq(seqAlice++), fee(--txFee), ter(terQUEUED));
            env(noop(bob), seq(seqBob++), fee(--txFee), ter(terQUEUED));
            env(noop(carol), seq(seqCarol++), fee(--txFee), ter(terQUEUED));
            env(noop(daria), seq(seqDaria++), fee(--txFee), ter(terQUEUED));
            env(noop(ellie), seq(seqEllie++), fee(--txFee), ter(terQUEUED));
            env(noop(fiona), seq(seqFiona++), fee(--txFee), ter(terQUEUED));
        }
        std::size_t expectedInQueue = 60;
        checkMetrics(
            __LINE__,
            env,
            expectedInQueue,
            5 * expectedPerLedger,
            expectedPerLedger + 1,
            expectedPerLedger,
            256);

        // The next close should cause the in-ledger amendments to change.
        // Alice's queued transactions have a cached PreflightResult
        // that resulted from running against the Rules in the previous
        // ledger.  Since the amendments change in this newest ledger
        // The TxQ must re-run preflight using the new rules.
        //
        // These particular amendments don't impact any of the queued
        // transactions, so we won't see any change in the transaction
        // outcomes.  But code coverage is affected.
        do
        {
            env.close(closeDuration);
            auto expectedInLedger = expectedInQueue;
            expectedInQueue =
                (expectedInQueue > expectedPerLedger + 2
                     ? expectedInQueue - (expectedPerLedger + 2)
                     : 0);
            expectedInLedger -= expectedInQueue;
            ++expectedPerLedger;
            checkMetrics(
                __LINE__,
                env,
                expectedInQueue,
                5 * expectedPerLedger,
                expectedInLedger,
                expectedPerLedger,
                256);
            {
                auto const expectedPerAccount = expectedInQueue / 6;
                auto const expectedRemainder = expectedInQueue % 6;
                BEAST_EXPECT(env.seq(alice) == seqAlice - expectedPerAccount);
                BEAST_EXPECT(
                    env.seq(bob) ==
                    seqBob - expectedPerAccount -
                        (expectedRemainder > 4 ? 1 : 0));
                BEAST_EXPECT(
                    env.seq(carol) ==
                    seqCarol - expectedPerAccount -
                        (expectedRemainder > 3 ? 1 : 0));
                BEAST_EXPECT(
                    env.seq(daria) ==
                    seqDaria - expectedPerAccount -
                        (expectedRemainder > 2 ? 1 : 0));
                BEAST_EXPECT(
                    env.seq(ellie) ==
                    seqEllie - expectedPerAccount -
                        (expectedRemainder > 1 ? 1 : 0));
                BEAST_EXPECT(
                    env.seq(fiona) ==
                    seqFiona - expectedPerAccount -
                        (expectedRemainder > 0 ? 1 : 0));
            }
        } while (expectedInQueue > 0);
    }

    void
    testQueueFullDropPenalty()
    {
        // If...
        //   o The queue is close to full,
        //   o An account has multiple txs queued, and
        //   o That same account has a transaction fail
        // Then drop the last transaction for the account if possible.
        //
        // Verify that happens.
        testcase("Queue full drop penalty");
        using namespace jtx;

        // Because we're looking at a phenomenon that occurs when the TxQ
        // is at 95% capacity or greater, we need to have lots of entries
        // in the queue.  You can't even see 95% capacity unless there are
        // 20 entries in the queue.
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const daria("daria");
        Account const ellie("ellie");
        Account const fiona("fiona");

        // We'll be using fees to control which entries leave the queue in
        // which order.  There's no "lowFee" -- that's the default fee from
        // the unit test.
        int const medFee = 100;
        int const hiFee = 1000;

        auto cfg = makeConfig(
            {{"minimum_txn_in_ledger_standalone", "5"},
             {"ledgers_in_queue", "5"},
             {"maximum_txn_per_account", "30"},
             {"minimum_queue_size", "50"}});

        Env env(*this, std::move(cfg));

        // The noripple is to reduce the number of transactions required to
        // fund the accounts.  There is no rippling in this test.
        env.fund(XRP(10000), noripple(alice, bob, carol, daria, ellie, fiona));
        env.close();

        // Get bob some tickets.
        std::uint32_t const bobTicketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 10));
        env.close();

        // Get the dropPenalty flag set on alice and bob by having one
        // of their transactions expire out of the queue.  To start out
        // alice fills the ledger.
        fillQueue(env, alice);
        checkMetrics(__LINE__, env, 0, 50, 7, 6, 256);

        // Now put a few transactions into alice's queue, including one that
        // will expire out soon.
        auto seqAlice = env.seq(alice);
        auto const seqSaveAlice = seqAlice;
        int feeDrops = 40;
        env(noop(alice),
            seq(seqAlice++),
            fee(--feeDrops),
            json(R"({"LastLedgerSequence": 7})"),
            ter(terQUEUED));
        env(noop(alice), seq(seqAlice++), fee(--feeDrops), ter(terQUEUED));
        env(noop(alice), seq(seqAlice++), fee(--feeDrops), ter(terQUEUED));
        BEAST_EXPECT(env.seq(alice) == seqSaveAlice);

        // Similarly for bob, but bob uses tickets in his transactions.
        // The drop penalty works a little differently with tickets.
        env(noop(bob),
            ticket::use(bobTicketSeq + 0),
            json(R"({"LastLedgerSequence": 7})"),
            ter(terQUEUED));
        env(noop(bob),
            ticket::use(bobTicketSeq + 1),
            fee(--feeDrops),
            ter(terQUEUED));
        env(noop(bob),
            ticket::use(bobTicketSeq + 2),
            fee(--feeDrops),
            ter(terQUEUED));

        // Fill the queue with higher fee transactions so alice's and
        // bob's transactions are stuck in the queue.
        auto seqCarol = env.seq(carol);
        auto seqDaria = env.seq(daria);
        auto seqEllie = env.seq(ellie);
        auto seqFiona = env.seq(fiona);
        feeDrops = medFee;
        for (int i = 0; i < 7; ++i)
        {
            env(noop(carol), seq(seqCarol++), fee(--feeDrops), ter(terQUEUED));
            env(noop(daria), seq(seqDaria++), fee(--feeDrops), ter(terQUEUED));
            env(noop(ellie), seq(seqEllie++), fee(--feeDrops), ter(terQUEUED));
            env(noop(fiona), seq(seqFiona++), fee(--feeDrops), ter(terQUEUED));
        }

        checkMetrics(__LINE__, env, 34, 50, 7, 6, 256);
        env.close();
        checkMetrics(__LINE__, env, 26, 50, 8, 7, 256);

        // Re-fill the queue so alice and bob stay stuck.
        feeDrops = medFee;
        for (int i = 0; i < 3; ++i)
        {
            env(noop(carol), seq(seqCarol++), fee(--feeDrops), ter(terQUEUED));
            env(noop(daria), seq(seqDaria++), fee(--feeDrops), ter(terQUEUED));
            env(noop(ellie), seq(seqEllie++), fee(--feeDrops), ter(terQUEUED));
            env(noop(fiona), seq(seqFiona++), fee(--feeDrops), ter(terQUEUED));
        }
        checkMetrics(__LINE__, env, 38, 50, 8, 7, 256);
        env.close();
        checkMetrics(__LINE__, env, 29, 50, 9, 8, 256);

        // One more time...
        feeDrops = medFee;
        for (int i = 0; i < 3; ++i)
        {
            env(noop(carol), seq(seqCarol++), fee(--feeDrops), ter(terQUEUED));
            env(noop(daria), seq(seqDaria++), fee(--feeDrops), ter(terQUEUED));
            env(noop(ellie), seq(seqEllie++), fee(--feeDrops), ter(terQUEUED));
            env(noop(fiona), seq(seqFiona++), fee(--feeDrops), ter(terQUEUED));
        }
        checkMetrics(__LINE__, env, 41, 50, 9, 8, 256);
        env.close();
        checkMetrics(__LINE__, env, 29, 50, 10, 9, 256);

        // Finally the stage is set.  alice's and bob's transactions expired
        // out of the queue which caused the dropPenalty flag to be set on
        // their accounts.
        //
        // This also means that alice has a sequence gap in her transactions,
        // and thus can't queue any more.
        env(noop(alice), seq(seqAlice), fee(hiFee), ter(telCAN_NOT_QUEUE));

        // Once again, fill the queue almost to the brim.
        feeDrops = medFee;
        for (int i = 0; i < 4; ++i)
        {
            env(noop(carol), seq(seqCarol++), fee(--feeDrops), ter(terQUEUED));
            env(noop(daria), seq(seqDaria++), fee(--feeDrops), ter(terQUEUED));
            env(noop(ellie), seq(seqEllie++), fee(--feeDrops), ter(terQUEUED));
            env(noop(fiona), seq(seqFiona++), fee(--feeDrops), ter(terQUEUED));
        }
        env(noop(carol), seq(seqCarol++), fee(--feeDrops), ter(terQUEUED));
        env(noop(daria), seq(seqDaria++), fee(--feeDrops), ter(terQUEUED));
        env(noop(ellie), seq(seqEllie++), fee(--feeDrops), ter(terQUEUED));
        checkMetrics(__LINE__, env, 48, 50, 10, 9, 256);

        // Now induce a fee jump which should cause all the transactions
        // in the queue to fail with telINSUF_FEE_P.
        //
        // *NOTE* raiseLocalFee() is tricky to use since the local fee is
        // asynchronously lowered by LoadManager.  Here we're just
        // pushing the local fee up really high and then hoping that we
        // outrace LoadManager undoing our work.
        for (int i = 0; i < 30; ++i)
            env.app().getFeeTrack().raiseLocalFee();

        // Now close the ledger, which will attempt to process alice's
        // and bob's queued transactions.
        //  o The _last_ transaction should be dropped from alice's queue.
        //  o The first failing transaction should be dropped from bob's queue.
        env.close();
        checkMetrics(__LINE__, env, 46, 50, 0, 10, 256);

        // Run the local fee back down.
        while (env.app().getFeeTrack().lowerLocalFee())
            ;

        // bob fills the ledger so it's easier to probe the TxQ.
        fillQueue(env, bob);
        checkMetrics(__LINE__, env, 46, 50, 11, 10, 256);

        // Before the close() alice had two transactions in her queue.
        // We now expect her to have one.  Here's the state of alice's queue.
        //
        //  0. The transaction that used to be first in her queue expired
        //     out two env.close() calls back.  That left a gap in alice's
        //     queue which has not been filled yet.
        //
        //  1. The first transaction in the queue failed to apply because
        //     of the sequence gap.  But it is retained in the queue.
        //
        //  2. The last (second) transaction in alice's queue was removed
        //     as "punishment"...
        //    a) For already having a transaction expire out of her queue, and
        //    b) For just now having a queued transaction fail on apply()
        //       because of the sequence gap.
        //
        // Verify that none of alice's queued transactions actually applied to
        // her account.
        BEAST_EXPECT(env.seq(alice) == seqSaveAlice);
        seqAlice = seqSaveAlice;

        // Verify that there's a gap at the front of alice's queue by
        // queuing another low fee transaction into that spot.
        env(noop(alice), seq(seqAlice++), fee(11), ter(terQUEUED));

        // Verify that the first entry in alice's queue is still there
        // by trying to replace it and having that fail.
        env(noop(alice), seq(seqAlice++), ter(telCAN_NOT_QUEUE_FEE));

        // Verify that the last transaction in alice's queue was removed by
        // appending to her queue with a very low fee.
        env(noop(alice), seq(seqAlice++), ter(terQUEUED));

        // Before the close() bob had two transactions in his queue.
        // We now expect him to have one.  Here's the state of bob's queue.
        //
        //  0. The transaction that used to be first in his queue expired out
        //     two env.close() calls back.  That is how the dropPenalty flag
        //     got set on bob's queue.
        //
        //  1. Since bob's remaining transactions all have the same fee, the
        //     TxQ attempted to apply bob's second transaction to the ledger,
        //     but the fee was too low.  So the TxQ threw that transaction
        //     (not bob's last transaction) out of the queue.
        //
        //  2. The last of bob's transactions remains in the TxQ.

        // Verify that bob's first transaction was removed from the queue
        // by queueing another low fee transaction into that spot.
        env(noop(bob), ticket::use(bobTicketSeq + 0), fee(12), ter(terQUEUED));

        // Verify that bob's second transaction was removed from the queue
        // by queueing another low fee transaction into that spot.
        env(noop(bob), ticket::use(bobTicketSeq + 1), fee(11), ter(terQUEUED));

        // Verify that the last entry in bob's queue is still there
        // by trying to replace it and having that fail.
        env(noop(bob),
            ticket::use(bobTicketSeq + 2),
            ter(telCAN_NOT_QUEUE_FEE));
    }

    void
    testCancelQueuedOffers()
    {
        testcase("Cancel queued offers");
        using namespace jtx;

        Account const alice("alice");
        auto gw = Account("gw");
        auto USD = gw["USD"];

        auto cfg = makeConfig(
            {{"minimum_txn_in_ledger_standalone", "5"},
             {"ledgers_in_queue", "5"},
             {"maximum_txn_per_account", "30"},
             {"minimum_queue_size", "50"}});

        Env env(*this, std::move(cfg));

        // The noripple is to reduce the number of transactions required to
        // fund the accounts.  There is no rippling in this test.
        env.fund(XRP(100000), noripple(alice));
        env.close();

        {
            // ------- Sequence-based transactions -------
            fillQueue(env, alice);

            // Alice creates a couple offers
            auto const aliceSeq = env.seq(alice);
            env(offer(alice, USD(1000), XRP(1000)), ter(terQUEUED));

            env(offer(alice, USD(1000), XRP(1001)),
                seq(aliceSeq + 1),
                ter(terQUEUED));

            // Alice creates transactions that cancel the first set of
            // offers, one through another offer, and one cancel
            env(offer(alice, USD(1000), XRP(1002)),
                seq(aliceSeq + 2),
                json(jss::OfferSequence, aliceSeq),
                ter(terQUEUED));

            env(offer_cancel(alice, aliceSeq + 1),
                seq(aliceSeq + 3),
                ter(terQUEUED));

            env.close();

            checkMetrics(__LINE__, env, 0, 50, 4, 6, 256);
        }

        {
            // ------- Ticket-based transactions -------

            // Alice creates some tickets
            auto const aliceTkt = env.seq(alice);
            env(ticket::create(alice, 6));
            env.close();

            fillQueue(env, alice);

            // Alice creates a couple offers using tickets, consuming the
            // tickets in reverse order
            auto const aliceSeq = env.seq(alice);
            env(offer(alice, USD(1000), XRP(1000)),
                ticket::use(aliceTkt + 4),
                ter(terQUEUED));

            env(offer(alice, USD(1000), XRP(1001)),
                ticket::use(aliceTkt + 3),
                ter(terQUEUED));

            // Alice creates a couple more transactions that cancel the first
            // set of offers, also in reverse order. This allows Alice to submit
            // a tx with a lower ticket value than the offer it's cancelling.
            // These transactions succeed because Ticket ordering is arbitrary
            // and it's up to the user to ensure they don't step on their own
            // feet.
            env(offer(alice, USD(1000), XRP(1002)),
                ticket::use(aliceTkt + 2),
                json(jss::OfferSequence, aliceTkt + 4),
                ter(terQUEUED));

            env(offer_cancel(alice, aliceTkt + 3),
                ticket::use(aliceTkt + 1),
                ter(terQUEUED));

            // Create a couple more offers using sequences
            env(offer(alice, USD(1000), XRP(1000)), ter(terQUEUED));

            env(offer(alice, USD(1000), XRP(1001)),
                seq(aliceSeq + 1),
                ter(terQUEUED));

            // And try to cancel those using tickets
            env(offer(alice, USD(1000), XRP(1002)),
                ticket::use(aliceTkt + 5),
                json(jss::OfferSequence, aliceSeq),
                ter(terQUEUED));

            env(offer_cancel(alice, aliceSeq + 1),
                ticket::use(aliceTkt + 6),
                ter(terQUEUED));

            env.close();

            // The ticket transactions that didn't succeed or get queued succeed
            // this time because the tickets got consumed when the offers came
            // out of the queue
            checkMetrics(__LINE__, env, 0, 50, 8, 7, 256);
        }
    }

    void
    testZeroReferenceFee()
    {
        testcase("Zero reference fee");
        using namespace jtx;

        Account const alice("alice");
        auto const queued = ter(terQUEUED);

        Env env(
            *this,
            makeConfig(
                {{"minimum_txn_in_ledger_standalone", "3"}},
                {{"reference_fee", "0"},
                 {"account_reserve", "0"},
                 {"owner_reserve", "0"}}));

        BEAST_EXPECT(env.current()->fees().base == 10);

        checkMetrics(__LINE__, env, 0, std::nullopt, 0, 3, 256);

        // ledgers in queue is 2 because of makeConfig
        auto const initQueueMax = initFee(env, 3, 2, 0, 0, 0);

        BEAST_EXPECT(env.current()->fees().base == 0);

        {
            auto const fee = env.rpc("fee");

            if (BEAST_EXPECT(fee.isMember(jss::result)) &&
                BEAST_EXPECT(!RPC::contains_error(fee[jss::result])))
            {
                auto const& result = fee[jss::result];

                BEAST_EXPECT(result.isMember(jss::levels));
                auto const& levels = result[jss::levels];
                BEAST_EXPECT(
                    levels.isMember(jss::median_level) &&
                    levels[jss::median_level] == "128000");
                BEAST_EXPECT(
                    levels.isMember(jss::minimum_level) &&
                    levels[jss::minimum_level] == "256");
                BEAST_EXPECT(
                    levels.isMember(jss::open_ledger_level) &&
                    levels[jss::open_ledger_level] == "256");
                BEAST_EXPECT(
                    levels.isMember(jss::reference_level) &&
                    levels[jss::reference_level] == "256");

                auto const& drops = result[jss::drops];
                BEAST_EXPECT(
                    drops.isMember(jss::base_fee) &&
                    drops[jss::base_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::median_fee) &&
                    drops[jss::median_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::minimum_fee) &&
                    drops[jss::minimum_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::open_ledger_fee) &&
                    drops[jss::open_ledger_fee] == "0");
            }
        }

        checkMetrics(__LINE__, env, 0, initQueueMax, 0, 3, 256);

        // The noripple is to reduce the number of transactions required to
        // fund the accounts.  There is no rippling in this test.
        env.fund(XRP(100000), noripple(alice));

        checkMetrics(__LINE__, env, 0, initQueueMax, 1, 3, 256);

        env.close();

        checkMetrics(__LINE__, env, 0, 6, 0, 3, 256);

        fillQueue(env, alice);

        checkMetrics(__LINE__, env, 0, 6, 4, 3, 256);

        env(noop(alice), openLedgerFee(env));

        checkMetrics(__LINE__, env, 0, 6, 5, 3, 256);

        auto aliceSeq = env.seq(alice);
        env(noop(alice), queued);

        checkMetrics(__LINE__, env, 1, 6, 5, 3, 256);

        env(noop(alice), seq(aliceSeq + 1), fee(10), queued);

        checkMetrics(__LINE__, env, 2, 6, 5, 3, 256);

        {
            auto const fee = env.rpc("fee");

            if (BEAST_EXPECT(fee.isMember(jss::result)) &&
                BEAST_EXPECT(!RPC::contains_error(fee[jss::result])))
            {
                auto const& result = fee[jss::result];

                BEAST_EXPECT(result.isMember(jss::levels));
                auto const& levels = result[jss::levels];
                BEAST_EXPECT(
                    levels.isMember(jss::median_level) &&
                    levels[jss::median_level] == "128000");
                BEAST_EXPECT(
                    levels.isMember(jss::minimum_level) &&
                    levels[jss::minimum_level] == "256");
                BEAST_EXPECT(
                    levels.isMember(jss::open_ledger_level) &&
                    levels[jss::open_ledger_level] == "355555");
                BEAST_EXPECT(
                    levels.isMember(jss::reference_level) &&
                    levels[jss::reference_level] == "256");

                auto const& drops = result[jss::drops];
                BEAST_EXPECT(
                    drops.isMember(jss::base_fee) &&
                    drops[jss::base_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::median_fee) &&
                    drops[jss::median_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::minimum_fee) &&
                    drops[jss::minimum_fee] == "0");
                BEAST_EXPECT(
                    drops.isMember(jss::open_ledger_fee) &&
                    drops[jss::open_ledger_fee] == "1389");
            }
        }

        env.close();

        checkMetrics(__LINE__, env, 0, 10, 2, 5, 256);
    }

    void
    run() override
    {
        testQueueSeq();
        testQueueTicket();
        testTecResult();
        testLocalTxRetry();
        testLastLedgerSeq();
        testZeroFeeTxn();
        testFailInPreclaim();
        testQueuedTxFails();
        testMultiTxnPerAccount();
        testTieBreaking();
        testAcctTxnID();
        testMaximum();
        testUnexpectedBalanceChange();
        testBlockersSeq();
        testBlockersTicket();
        testInFlightBalance();
        testConsequences();
    }

    void
    run2()
    {
        testAcctInQueueButEmpty();
        testRPC();
        testExpirationReplacement();
        testFullQueueGapFill();
        testSignAndSubmitSequence();
        testAccountInfo();
        testServerInfo();
        testServerSubscribe();
        testClearQueuedAccountTxs();
        testScaling();
        testInLedgerSeq();
        testInLedgerTicket();
        testReexecutePreflight();
        testQueueFullDropPenalty();
        testCancelQueuedOffers();
        testZeroReferenceFee();
    }
};

class TxQ2_test : public TxQ1_test
{
    void
    run() override
    {
        run2();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(TxQ1, app, ripple, 1);
BEAST_DEFINE_TESTSUITE_PRIO(TxQ2, app, ripple, 1);

}  // namespace test
}  // namespace ripple
