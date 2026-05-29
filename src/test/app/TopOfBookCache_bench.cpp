#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/TopOfBookCache.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/paths/BookTip.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace xrpl::test {

/** Level 1 micro-benchmark for the top-of-book cache.

    A/Bs the cache via its runtime kill switch (TopOfBookCache::setEnabled) over
    a deep order book, entirely in-process (no network, no synced ledger).

    Two arms:

    - readArm  (isolated): drives BookTip's first-step top-of-book read against
      an OpenView this benchmark constructs and OWNS, so cache counters are
      reliable (the open-ledger apply path copies the OpenView per tx and the
      copy ctor resets counters, so counters read off env.current() are not).
      This isolates the optimized primitive (succ() walk -> hash probe). It is a
      BEST-CASE, hot-entry read number — not an end-to-end throughput figure.

    - e2eArm   (end-to-end): times a batch of real crossing OfferCreates through
      the full Env apply path (real offer consumption => invalidation/repopulate
      churn). Timing only; also captures the cache's own overhead (the OpenView
      copy on every modify()). Answers "does the isolated saving show up at all,
      net of overhead". Realistic hit-rate under MainNet-like mixed load is a
      later, heavier exercise (Level 1.5 / Level 3), not measured here.

    Registered MANUAL — never runs in normal CI. Invoke explicitly:
        rippled --unittest=TopOfBookCacheBench
*/
class TopOfBookCacheBench_test : public beast::unit_test::Suite
{
    using clock = std::chrono::steady_clock;

    // Median of repeated samples — robust to scheduler noise.
    static double
    median(std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

    struct ReadArm
    {
        double nsPerRead{0};
        std::uint64_t hits{0};
        std::uint64_t misses{0};
        std::uint64_t invalidations{0};
        bool foundTop{false};
    };

    // Build a deep order book (XRP <-> USD), close it into the LCL, and return
    // the Book those offers populate. `pages` distinct qualities => `pages`
    // directory pages.
    static Book
    buildDeepBook(jtx::Env& env, jtx::Account const& gw, int pages)
    {
        using namespace jtx;
        auto const USD = gw["USD"];
        Account const maker{"maker"};

        env.fund(XRP(1'000'000), gw, maker);
        env.close();
        env.trust(USD(10'000'000), maker);
        env.close();
        env(pay(gw, maker, USD(1'000'000)));
        env.close();

        // Each offer: maker receives takerPays (XRP), gives takerGets (USD).
        // Distinct takerPays => distinct quality => distinct directory page.
        for (int i = 0; i < pages; ++i)
            env(offer(maker, XRP(500 + i), USD(100)));
        env.close();

        // Book{in = takerPays.asset(), out = takerGets.asset()} — see
        // OfferCreate.cpp:570.
        return Book{xrpIssue(), USD.issue(), std::nullopt};
    }

    // Isolated read-path arm. Owns the OpenView so counters are trustworthy.
    ReadArm
    runReadArm(jtx::Env& env, Book const& book, bool cacheEnabled, std::size_t reads)
    {
        TopOfBookCache::setEnabled(cacheEnabled);

        // Fresh owned view per arm => clean counters (no reset API otherwise).
        OpenView ov(kOpenLedger, env.closed()->rules(), env.closed());

        // One read = fresh BookTip + a single step() = one top-of-book probe.
        // BookTip's first step is read-only (it deletes only from the 2nd step
        // on), so a single ApplyView can be reused across reads.
        ApplyViewImpl av(&ov, TapNone);

        ReadArm r;
        {
            BookTip bt(av, book);
            r.foundTop = bt.step(env.journal) && bt.entry() != nullptr;
        }

        auto const once = [&] {
            for (std::size_t i = 0; i < reads; ++i)
            {
                BookTip bt(av, book);
                bt.step(env.journal);
            }
        };

        once();  // warmup (also populates the cache in the enabled arm)

        std::vector<double> samples;
        for (int rep = 0; rep < 5; ++rep)
        {
            auto const t0 = clock::now();
            once();
            auto const t1 = clock::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                static_cast<double>(reads));
        }

        r.nsPerRead = median(std::move(samples));
        r.hits = ov.topOfBookCache().hits();
        r.misses = ov.topOfBookCache().misses();
        r.invalidations = ov.topOfBookCache().invalidations();
        return r;
    }

    void
    testReadPath()
    {
        testcase("Arm 1: isolated top-of-book read (owned OpenView)");
        using namespace jtx;

        // Isolate the TopOfBookCache read path: the order-book index, when on,
        // supersedes the cache in BookTip (cursor instead of cache+succ), so it
        // must be off for this arm to measure the cache.
        OrderBookIndex::setEnabled(false);

        int const pages = 64;
        std::size_t const reads = 200'000;

        Env env{*this};
        auto const book = buildDeepBook(env, Account{"gw"}, pages);

        auto const off = runReadArm(env, book, /*cacheEnabled=*/false, reads);
        auto const on = runReadArm(env, book, /*cacheEnabled=*/true, reads);
        TopOfBookCache::setEnabled(true);  // restore default

        BEAST_EXPECT(off.foundTop);
        BEAST_EXPECT(on.foundTop);
        // Disabled arm never consults the cache.
        BEAST_EXPECT(off.hits == 0 && off.misses == 0);
        // Enabled arm: 1 cold miss, the rest hits.
        BEAST_EXPECT(on.hits > 0);
        BEAST_EXPECT(on.misses >= 1);
        BEAST_EXPECT(on.invalidations == 0);

        double const speedup = on.nsPerRead > 0 ? off.nsPerRead / on.nsPerRead : 0.0;

#ifndef NDEBUG
        log << "\n*** DEBUG build: BookTip's differential gate shadow-verifies "
               "every cache hit with an extra succ() walk, so the cache-ON path "
               "does MORE work here. Arm 1 timing is only meaningful in a "
               "Release (NDEBUG) build; counters below are valid regardless. ***\n";
#endif

        log << "\n=== Arm 1: isolated read (best-case, hot entry) ===\n"
            << "  book pages           : " << pages << "\n"
            << "  reads / sample       : " << reads << "\n"
            << "  cache OFF  ns/read   : " << off.nsPerRead << "\n"
            << "  cache ON   ns/read   : " << on.nsPerRead << "\n"
            << "  speedup              : " << speedup << "x\n"
            << "  cache ON  hits/miss  : " << on.hits << " / " << on.misses << "\n"
            << std::endl;

        OrderBookIndex::setEnabled(true);  // restore default
    }

    // End-to-end arm: time real crossing offers through the full apply path.
    double
    runE2EArm(bool cacheEnabled, int pages, int crossings)
    {
        using namespace jtx;
        TopOfBookCache::setEnabled(cacheEnabled);

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        buildDeepBook(env, gw, pages);

        // Taker buys USD with XRP, crossing the maker's resting offers.
        Account const taker{"taker"};
        env.fund(XRP(1'000'000), taker);
        env.close();
        env.trust(USD(10'000'000), taker);
        env.close();

        auto const t0 = clock::now();
        for (int i = 0; i < crossings; ++i)
        {
            env(offer(taker, USD(100), XRP(500 + (i % pages))));
            if ((i % 10) == 9)
                env.close();
        }
        env.close();
        auto const t1 = clock::now();

        return static_cast<double>(
                   std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) /
            crossings;
    }

    void
    testEndToEnd()
    {
        testcase("Arm 2: end-to-end crossing throughput (timing only)");

        int const pages = 64;
        int const crossings = 300;

        double const off = runE2EArm(/*cacheEnabled=*/false, pages, crossings);
        double const on = runE2EArm(/*cacheEnabled=*/true, pages, crossings);
        TopOfBookCache::setEnabled(true);  // restore default

        BEAST_EXPECT(off > 0 && on > 0);

        log << "\n=== Arm 2: end-to-end crossing (full apply path, real churn) ===\n"
            << "  book pages           : " << pages << "\n"
            << "  crossing offers      : " << crossings << "\n"
            << "  cache OFF  us/cross  : " << off << "\n"
            << "  cache ON   us/cross  : " << on << "\n"
            << "  delta                : " << (off - on) << " us/cross"
            << " (note: dominated by tx machinery + cache copy overhead)\n"
            << std::endl;
    }

    // Profiling arm: measure PURE crossing-apply cost with NO ledger close.
    // env.close() runs full consensus close (flushDirty hashing + SQLite ledger
    // writes); Arm 2 closed every 10 offers, contaminating its per-crossing
    // number. Here we pre-sign crossing OfferCreates and replay them against a
    // fresh owned OpenView per rep (each rep starts with the full book), timing
    // only xrpl::apply (preflight + preclaim + doApply). Long enough total work
    // to attach `sample`/Instruments to the running process.
    void
    testCrossingApplyProfile()
    {
        testcase("Arm 3: pure crossing-apply cost (no ledger close)");
        using namespace jtx;

        int const pages = 64;
        int const crossPerRep = 50;  // crossings applied per fresh book
        // BENCH_PROFILE=1 cranks reps so the apply loop runs ~30s for `sample`.
        bool const profiling = std::getenv("BENCH_PROFILE") != nullptr;
        int const reps = profiling ? 5000 : 400;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        buildDeepBook(env, gw, pages);

        Account const taker{"taker"};
        env.fund(XRP(10'000'000), taker);
        env.close();
        env.trust(USD(100'000'000), taker);
        env.close();

        // Pre-sign the crossing OfferCreates once, with explicit increasing
        // sequences starting at taker's current seq. Each fresh accum resets
        // taker to that same seq, so the identical signed set replays cleanly.
        std::uint32_t const startSeq = env.seq(taker);
        std::vector<std::shared_ptr<STTx const>> txns;
        txns.reserve(crossPerRep);
        for (int i = 0; i < crossPerRep; ++i)
        {
            auto jtx = env.jt(
                offer(taker, USD(100), XRP(500 + (i % pages))),
                Seq(startSeq + i),
                Fee(100));
            txns.push_back(jtx.stx);
        }

        auto const base = env.current();  // open view over the closed book

        std::size_t applied = 0, crossed = 0;
        std::vector<double> samples;
        for (int rep = 0; rep < reps; ++rep)
        {
            OpenView accum(kOpenLedger, base->rules(), base);
            auto const t0 = clock::now();
            for (auto const& tx : txns)
            {
                auto const r = apply(env.app(), accum, *tx, TapNone, env.journal);
                if (rep == 0)
                {
                    ++applied;
                    if (r.applied && isTesSuccess(r.ter))
                        ++crossed;
                }
            }
            auto const t1 = clock::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                crossPerRep / 1000.0);  // us/crossing
        }

        BEAST_EXPECT(applied == static_cast<std::size_t>(crossPerRep));
        BEAST_EXPECT(crossed > 0);

        log << "\n=== Arm 3: pure crossing-apply (no ledger close) ===\n"
            << "  book pages              : " << pages << "\n"
            << "  crossings / rep         : " << crossPerRep << "\n"
            << "  reps                    : " << reps << "\n"
            << "  tesSUCCESS (rep 0)      : " << crossed << " / " << applied << "\n"
            << "  median us / crossing    : " << median(samples) << "\n"
            << "  (compare to Arm 2's ~780us which INCLUDED ledger close)\n"
            << std::endl;
    }

    // Arm 4 (Plan 9 headline): pure crossing-apply with the order-book index
    // ON vs OFF. OFF = baseline succ()-per-offer walk; ON = BookTip iterates the
    // in-memory cursor (index pre-seeded per rep, untimed, modelling the
    // maintained steady state). Same owned-OpenView / no-ledger-close method as
    // Arm 3, so the delta isolates the succ() cost the cursor removes.
    void
    testCrossingIndexArm()
    {
        testcase("Arm 4: crossing-apply, order-book index ON vs OFF");
        using namespace jtx;

        int const pages = 64;
        int const crossPerRep = 50;
        int const reps = 400;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        auto const book = buildDeepBook(env, gw, pages);

        Account const taker{"taker"};
        env.fund(XRP(10'000'000), taker);
        env.close();
        env.trust(USD(100'000'000), taker);
        env.close();

        std::uint32_t const startSeq = env.seq(taker);
        std::vector<std::shared_ptr<STTx const>> txns;
        txns.reserve(crossPerRep);
        for (int i = 0; i < crossPerRep; ++i)
            txns.push_back(
                env.jt(offer(taker, USD(100), XRP(500 + (i % pages))), Seq(startSeq + i), Fee(100))
                    .stx);

        auto const base = env.current();

        auto runArm = [&](bool indexEnabled) {
            OrderBookIndex::setEnabled(indexEnabled);
            std::vector<double> samples;
            for (int rep = 0; rep < reps; ++rep)
            {
                OpenView accum(kOpenLedger, base->rules(), base);
                // Warm the maintained index outside the timed region (models the
                // steady state where it is kept in sync, not rebuilt per cross).
                if (indexEnabled)
                    accum.orderBookIndex().rebuildBook(accum, book);
                auto const t0 = clock::now();
                for (auto const& tx : txns)
                    apply(env.app(), accum, *tx, TapNone, env.journal);
                auto const t1 = clock::now();
                samples.push_back(
                    static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                    crossPerRep / 1000.0);
            }
            return median(samples);
        };

        double const off = runArm(false);
        double const on = runArm(true);
        OrderBookIndex::setEnabled(true);  // restore default

        double const speedup = on > 0 ? off / on : 0.0;

        log << "\n=== Arm 4: crossing-apply, index ON vs OFF (no ledger close) ===\n"
            << "  book pages              : " << pages << "\n"
            << "  crossings / rep         : " << crossPerRep << "\n"
            << "  index OFF us/crossing   : " << off << "  (baseline succ() walk)\n"
            << "  index ON  us/crossing   : " << on << "  (in-memory cursor)\n"
            << "  speedup                 : " << speedup << "x\n"
            << std::endl;
    }

    // Arm 5 (P9.6 headline): the REALISTIC per-tx path. Each crossing is applied
    // to a fresh COW copy of the prior OpenView — exactly what OpenLedger::modify
    // does per transaction — so the persistent index warms via the clone (no
    // pre-seed) and the clone cost is INCLUDED in the timing. Index ON should now
    // beat OFF on this path (the warm cursor amortizes the one cold rebuild),
    // unlike the non-persistent index which cold-started and rebuilt every tx.
    void
    testCrossingWarmArm()
    {
        testcase("Arm 5: realistic per-tx-copy crossing, index ON vs OFF");
        using namespace jtx;

        int const pages = 400;       // deep enough to stay populated across the batch
        int const crossPerRep = 100;
        int const reps = 200;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        auto const book = buildDeepBook(env, gw, pages);

        Account const taker{"taker"};
        env.fund(XRP(100'000'000), taker);
        env.close();
        env.trust(USD(1'000'000'000), taker);
        env.close();

        std::uint32_t const startSeq = env.seq(taker);
        std::vector<std::shared_ptr<STTx const>> txns;
        txns.reserve(crossPerRep);
        for (int i = 0; i < crossPerRep; ++i)
            txns.push_back(
                env.jt(offer(taker, USD(100), XRP(500 + (i % pages))), Seq(startSeq + i), Fee(100))
                    .stx);

        auto const base = env.current();

        auto runArm = [&](bool indexEnabled) {
            OrderBookIndex::setEnabled(indexEnabled);
            std::vector<double> samples;
            for (int rep = 0; rep < reps; ++rep)
            {
                // Fresh cold OpenView over the closed book (index empty).
                auto current = std::make_shared<OpenView>(kOpenLedger, base->rules(), base);
                auto const t0 = clock::now();
                for (auto const& tx : txns)
                {
                    // The per-tx COW copy (clones the persistent index) — exactly
                    // what OpenLedger::modify does per transaction.
                    auto next = std::make_shared<OpenView>(*current);
                    apply(env.app(), *next, *tx, TapNone, env.journal);
                    current = next;
                }
                auto const t1 = clock::now();
                samples.push_back(
                    static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
                    crossPerRep / 1000.0);
            }
            return median(samples);
        };

        double const off = runArm(false);
        double const on = runArm(true);
        OrderBookIndex::setEnabled(true);

        double const speedup = on > 0 ? off / on : 0.0;

        log << "\n=== Arm 5: realistic per-tx-copy crossing (clones index per tx) ===\n"
            << "  book pages              : " << pages << "\n"
            << "  crossings / rep         : " << crossPerRep << "\n"
            << "  index OFF us/crossing   : " << off << "  (succ() per offer, per tx)\n"
            << "  index ON  us/crossing   : " << on << "  (warm cursor; clone+rebuild amortized)\n"
            << "  speedup                 : " << speedup << "x\n"
            << std::endl;
    }

public:
    void
    run() override
    {
        // BENCH_PROFILE=1 runs only the crossing-apply loop (long) for `sample`.
        if (std::getenv("BENCH_PROFILE") != nullptr)
        {
            testCrossingApplyProfile();
            return;
        }
        testReadPath();
        testEndToEnd();
        testCrossingApplyProfile();
        testCrossingIndexArm();
        testCrossingWarmArm();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(TopOfBookCacheBench, app, xrpl, 20);

}  // namespace xrpl::test
