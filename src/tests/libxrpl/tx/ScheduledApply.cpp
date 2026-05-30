// Differential test for applyScheduled (Plan 1, Phase 3 core): applying a
// transaction set via its conflict-group schedule (each group isolated over the
// closed snapshot, write-sets merged) must yield a byte-identical account-state
// root to a serial canonical apply.
//
// Unlike a test routed through TxTest::close() (which re-canonicalizes by tx
// hash and so cannot observe a reordering), this test builds BOTH ledgers
// itself, so the comparison is real: if the scheduler ever placed two
// conflicting transactions in different groups, the roots would diverge here.

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/OfferCreate.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/tx/Schedule.h>
#include <xrpl/tx/apply.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/IOU.h>
#include <helpers/TxTest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::test {

namespace {

std::shared_ptr<STTx const>
payment(Account const& from, Account const& to, STAmount const& amount, std::uint32_t seq)
{
    return transactions::PaymentBuilder{from, to, amount}
        .setSequence(seq)
        .setFee(XRPAmount{10})
        .build(from.pk(), from.sk())
        .getSTTx();
}

// Build a fresh ledger from `closed`, apply `txns` in the given exact order in a
// single accumulating view (the serial ground truth), and return its
// account-state root.
uint256
serialStateRoot(
    TxTest& env,
    Ledger const& closed,
    std::vector<std::shared_ptr<STTx const>> const& txns)
{
    auto const closeTime = env.getCloseTime() + closed.header().closeTimeResolution;
    auto next = std::make_shared<Ledger>(closed, closeTime);
    {
        OpenView accum(&closed);
        for (auto const& tx : txns)
            apply(env.getServiceRegistry(), accum, *tx, TapNone, env.getServiceRegistry().getJournal("apply"));
        accum.apply(*next);
    }
    next->setAccepted(closeTime, closed.header().closeTimeResolution, true);
    return next->header().accountHash;
}

// Build a fresh ledger from `closed`, apply `txns` via applyScheduled (grouped +
// merged), and return its account-state root, along with the schedule result.
std::pair<uint256, ScheduledApplyResult>
scheduledStateRoot(
    TxTest& env,
    Ledger const& closed,
    std::vector<std::shared_ptr<STTx const>> const& txns,
    unsigned workers = 1)
{
    auto const closeTime = env.getCloseTime() + closed.header().closeTimeResolution;
    auto next = std::make_shared<Ledger>(closed, closeTime);
    auto const res = applyScheduled(
        env.getServiceRegistry(),
        closed,
        *next,
        txns,
        env.getServiceRegistry().getJournal("apply"),
        workers);
    next->setAccepted(closeTime, closed.header().closeTimeResolution, true);
    return {next->header().accountHash, res};
}

}  // namespace

TEST(ScheduledApply, ParallelGroupedMatchesSerial)
{
    TxTest env;
    Account const a("a"), b("b"), c("c"), d("d"), e("e"), f("f"), g("g");
    for (auto const* acct : {&a, &b, &c, &d, &e, &f, &g})
        env.createAccount(*acct, XRP(10000));
    env.close();

    auto const& closed = *env.getClosedLedgerPtr();
    std::uint32_t const seqA = env.getAccountRoot(a.id()).getSequence();

    // Groups: {p1,p4} (source a, ordered), {p2}, {p3} — three independent groups.
    std::vector<std::shared_ptr<STTx const>> txns{
        payment(a, b, XRP(7), seqA),
        payment(c, d, XRP(3), env.getAccountRoot(c.id()).getSequence()),
        payment(e, f, XRP(5), env.getAccountRoot(e.id()).getSequence()),
        payment(a, g, XRP(2), seqA + 1),
    };

    auto const serialRoot = serialStateRoot(env, closed, txns);
    auto const [scheduledRoot, res] = scheduledStateRoot(env, closed, txns);

    EXPECT_FALSE(res.fullySerial);
    EXPECT_EQ(res.groupCount, 3u);
    EXPECT_EQ(res.applied, 4u);

    // The state root must be non-trivial (real accounts changed) AND identical
    // regardless of the parallel grouping — the determinism guarantee.
    EXPECT_NE(serialRoot, uint256{});
    EXPECT_EQ(serialRoot, scheduledRoot);
}

TEST(ScheduledApply, ThreadedMatchesSerialAcrossManyGroups)
{
    TxTest env;
    // 24 accounts -> 12 disjoint payment pairs -> 12 independent groups, applied
    // across a thread pool. Repeated to give nondeterminism/races a chance to
    // surface. (A clean unit pass is necessary, not sufficient, for production —
    // see applyScheduled's note on certification.)
    constexpr int kPairs = 12;
    std::vector<Account> accts;
    accts.reserve(kPairs * 2);
    for (int i = 0; i < kPairs * 2; ++i)
        accts.emplace_back("acct" + std::to_string(i));
    for (auto const& a : accts)
        env.createAccount(a, XRP(10000));
    env.close();

    auto const& closed = *env.getClosedLedgerPtr();
    std::vector<std::shared_ptr<STTx const>> txns;
    for (int p = 0; p < kPairs; ++p)
    {
        auto const& from = accts[2 * p];
        auto const& to = accts[2 * p + 1];
        txns.push_back(
            payment(from, to, XRP(p + 1), env.getAccountRoot(from.id()).getSequence()));
    }

    auto const serialRoot = serialStateRoot(env, closed, txns);
    EXPECT_NE(serialRoot, uint256{});

    for (int iter = 0; iter < 8; ++iter)
    {
        auto const [threadedRoot, res] = scheduledStateRoot(env, closed, txns, /*workers=*/8);
        EXPECT_FALSE(res.fullySerial);
        EXPECT_EQ(res.groupCount, static_cast<std::size_t>(kPairs));
        EXPECT_EQ(res.applied, static_cast<std::size_t>(kPairs));
        EXPECT_EQ(threadedRoot, serialRoot) << "threaded apply diverged on iteration " << iter;
    }
}

TEST(ScheduledApply, FullySerialPathAlsoMatches)
{
    TxTest env;
    Account const a("a"), b("b"), gw("gw");
    IOU const usd("USD", gw);
    for (auto const* acct : {&a, &b, &gw})
        env.createAccount(*acct, XRP(10000));
    env.close();

    auto const& closed = *env.getClosedLedgerPtr();

    // An OfferCreate is touchesGlobal -> scheduleApply falls back to fully
    // serial; applyScheduled then applies the whole set in canonical order.
    auto const offer = transactions::OfferCreateBuilder{a, usd.amount(10), XRP(10)}
                           .setSequence(env.getAccountRoot(a.id()).getSequence())
                           .setFee(XRPAmount{10})
                           .build(a.pk(), a.sk())
                           .getSTTx();
    std::vector<std::shared_ptr<STTx const>> txns{
        payment(b, gw, XRP(1), env.getAccountRoot(b.id()).getSequence()),
        offer,
    };

    auto const serialRoot = serialStateRoot(env, closed, txns);
    auto const [scheduledRoot, res] = scheduledStateRoot(env, closed, txns);

    EXPECT_TRUE(res.fullySerial);
    EXPECT_NE(serialRoot, uint256{});
    EXPECT_EQ(serialRoot, scheduledRoot);
}

// Throughput benchmark: time applyScheduled over many disjoint payments at
// increasing worker counts. Run explicitly:
//   xrpl.test.tx --gtest_filter='ScheduledApply.ThroughputBenchmark'
// Build type matters enormously — Debug numbers (assertions on, unoptimized) are
// directional only; use a Release build for representative figures.
TEST(ScheduledApply, ThroughputBenchmark)
{
    constexpr int kPairs = 400;   // -> kPairs independent groups, 2*kPairs accounts
    constexpr int kReps = 5;

    TxTest env;
    std::vector<Account> accts;
    accts.reserve(kPairs * 2);
    for (int i = 0; i < kPairs * 2; ++i)
        accts.emplace_back("ba" + std::to_string(i));
    // Fund in batches with a single close per batch (createAccount closes each).
    for (auto const& a : accts)
        env.createAccount(a, XRP(10000));
    env.close();

    auto const& closed = *env.getClosedLedgerPtr();
    std::vector<std::shared_ptr<STTx const>> txns;
    txns.reserve(kPairs);
    for (int p = 0; p < kPairs; ++p)
        txns.push_back(payment(
            accts[2 * p], accts[2 * p + 1], XRP(1), env.getAccountRoot(accts[2 * p].id()).getSequence()));

    auto const closeTime = env.getCloseTime() + closed.header().closeTimeResolution;
    auto bestNanos = [&](unsigned workers) {
        long long best = -1;
        for (int r = 0; r < kReps; ++r)
        {
            auto next = std::make_shared<Ledger>(closed, closeTime);
            auto const t0 = std::chrono::steady_clock::now();
            auto const res = applyScheduled(
                env.getServiceRegistry(), closed, *next, txns,
                env.getServiceRegistry().getJournal("bench"), workers);
            auto const t1 = std::chrono::steady_clock::now();
            EXPECT_EQ(res.applied, static_cast<std::size_t>(kPairs));
            auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            if (best < 0 || ns < best)
                best = ns;
        }
        return best;
    };

    std::printf("\n=== applyScheduled throughput: %d disjoint payments (best of %d) ===\n",
                kPairs, kReps);
    long long base = 0;
    for (unsigned w : {1u, 2u, 4u, 8u})
    {
        auto const ns = bestNanos(w);
        if (w == 1)
            base = ns;
        std::printf("  workers=%u: %8.3f ms total, %7.1f us/tx, speedup %.2fx\n",
                    w, ns / 1e6, ns / 1e3 / kPairs, base / double(ns));
    }
    std::printf("(build type dominates these numbers; Debug is directional only)\n");
}

}  // namespace xrpl::test
