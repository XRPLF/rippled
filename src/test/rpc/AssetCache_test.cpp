//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2026 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without  fee  is hereby granted, provided that the above
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

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace xrpl {

class AssetCache_test : public beast::unit_test::Suite
{
    /**
     * Give `holder` N distinct outgoing trust lines (holder → peer IOUs).
     * `tag` prefixes peer account names so multiple holders do not collide.
     */
    void
    fundManyLines(
        test::jtx::Env& env,
        test::jtx::Account const& holder,
        int n,
        std::string const& tag = "p")
    {
        using namespace test::jtx;
        env.fund(XRP(10000), holder);
        env.close();
        for (int i = 0; i < n; ++i)
        {
            Account peer{tag + std::to_string(i)};
            env.fund(XRP(1000), peer);
            // Distinct holder-peer IOU lines (holder issues USD to peer).
            env.trust(holder["USD"](1000), peer);
            env(pay(holder, peer, holder["USD"](1)));
            env.close();
        }
    }

    void
    testBudgetZeroEmptyStubAndExpand()
    {
        testcase("budget zero: empty incomplete stub then expand with budget");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(gw["USD"](1000), alice);
        env(pay(gw, alice, gw["USD"](10)));
        env.close();

        // maxTotalLines=0: first load cannot admit lines; empty incomplete stub.
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/0,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/64);

        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(!lines || lines->empty());
            BEAST_EXPECT(cache->hasIncompleteLinesForSession(1));
            BEAST_EXPECT(cache->totalLineCount() == 0);
            BEAST_EXPECT(cache->overBudget());
        }

        // New cache with budget: expand/load can admit lines.
        cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/10000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/64);
        {
            AssetCache::SessionPin pin{2};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && !lines->empty());
            BEAST_EXPECT(cache->totalLineCount() >= 1);
            BEAST_EXPECT(!cache->overBudget());
        }
        cache->releaseSession(2);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testPendingExpandWhileShared()
    {
        testcase("expand while shared: published vector stable");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        fundManyLines(env, alice, 5, "pe");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/1);

        AssetCache::SessionPin pin{7};
        auto held = cache->getRippleLines(alice.id());
        BEAST_EXPECT(held);
        auto const firstSize = held->size();
        BEAST_EXPECT(firstSize >= 1);

        // External hold keeps use_count > 1; expand must not mutate `held`.
        auto const before = firstSize;
        bool grew = cache->expandIncompleteLines();
        BEAST_EXPECT(held->size() == before);
        // New publish may include more lines after coalesce on next get.
        auto again = cache->getRippleLines(alice.id());
        BEAST_EXPECT(again);
        if (grew)
            BEAST_EXPECT(again->size() >= before);
    }

    void
    testSessionPinsSharedHub()
    {
        testcase("session pins: shared hub freed only when last session ends");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(gw["USD"](1000), alice);
        env(pay(gw, alice, gw["USD"](5)));
        env.close();

        auto cache =
            std::make_shared<AssetCache>(env.current(), env.app().getJournal("AssetCache"));

        {
            AssetCache::SessionPin pinA{10};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        {
            AssetCache::SessionPin pinB{11};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->totalLineCount() >= 1);

        // First session ends — hub remains while second holds it.
        auto const freed10 = cache->releaseSession(10);
        BEAST_EXPECT(freed10 == 0);
        BEAST_EXPECT(cache->totalLineCount() >= 1);

        auto const freed11 = cache->releaseSession(11);
        BEAST_EXPECT(freed11 >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testAdvanceLedgerSoftRetainAndForceClear()
    {
        testcase("advanceLedger: soft retain vs forceClear");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(gw["USD"](1000), alice);
        env(pay(gw, alice, gw["USD"](10)));
        env.close();

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/10000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/64);

        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        auto const linesBefore = cache->totalLineCount();
        BEAST_EXPECT(linesBefore >= 1);
        auto const hitsBefore = cache->cacheHits();
        auto const advancesBefore = cache->ledgerAdvances();

        // Soft advance to next closed ledger: retain vectors.
        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->ledgerAdvances() == advancesBefore + 1);
        BEAST_EXPECT(cache->totalLineCount() == linesBefore);
        BEAST_EXPECT(cache->getLedger()->seq() == env.closed()->seq());

        // Same-seq no-op without forceClear.
        auto const advMid = cache->ledgerAdvances();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->ledgerAdvances() == advMid);

        // Hit path: reuse within cacheReuseLedgers.
        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->cacheHits() > hitsBefore);
        BEAST_EXPECT(cache->totalLineCount() == linesBefore);

        // forceClear drops all entries and pins.
        cache->advanceLedger(env.closed(), /*forceClear=*/true);
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(cache->ledgerAdvances() == advMid + 1);

        // After force-clear, next load is a miss that reloads.
        auto const missesBefore = cache->cacheMisses();
        {
            AssetCache::SessionPin pin{2};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->cacheMisses() > missesBefore);
        BEAST_EXPECT(cache->totalLineCount() >= 1);
        cache->releaseSession(2);
    }

    void
    testReuseWindowExpiryReloads()
    {
        testcase("cache reuse window expiry forces reload");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(gw["USD"](1000), alice);
        env(pay(gw, alice, gw["USD"](10)));
        env.close();

        // Tiny reuse window so a few closes force stale reload.
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/10000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/1,
            /*lineChunkSize=*/64);

        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        auto const misses0 = cache->cacheMisses();
        auto const loadedAt = cache->getLedger()->seq();

        // Soft-advance past reuse window (age > cacheReuseLedgers_).
        // Note: first close may only upgrade open→closed at the same seq.
        for (int i = 0; i < 4; ++i)
        {
            env.close();
            cache->advanceLedger(env.closed(), false);
        }
        BEAST_EXPECT(cache->getLedger()->seq() > loadedAt + 1);

        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->cacheMisses() > misses0);
        cache->releaseSession(1);
    }

    void
    testMaxLinesPerAccountAndChunk()
    {
        testcase("maxLinesPerAccount cap and chunked LoadScope");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        fundManyLines(env, alice, 8, "pl");

        // Cap at 3 lines; chunk size 2 so progressive fill stops at cap.
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/3,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/2);

        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            BEAST_EXPECT(lines->size() <= 3);
            // Drain progressive expand until complete or cap.
            for (int i = 0; i < 20; ++i)
            {
                if (!cache->expandIncompleteLines())
                    break;
            }
            lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            BEAST_EXPECT(lines->size() <= 3);
            BEAST_EXPECT(cache->totalLineCount() <= 3);
            // Cap should mark complete so no incomplete warning for session.
            BEAST_EXPECT(!cache->hasIncompleteLinesForSession(1) || lines->size() == 3);
        }
        cache->releaseSession(1);

        // LoadScope one-shot: pull full chunk in one reply (still capped).
        cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/5,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/1);
        {
            AssetCache::LoadScope oneShot{5};
            AssetCache::SessionPin pin{2};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            BEAST_EXPECT(lines->size() <= 5);
            BEAST_EXPECT(lines->size() >= 1);
        }
        cache->releaseSession(2);
    }

    void
    testGlobalBudgetBlocksNewLines()
    {
        testcase("global maxTotalLines blocks further admits");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        fundManyLines(env, alice, 6, "pa");
        fundManyLines(env, bob, 3, "pb");

        // Tiny global budget: alice loads a few; bob may be blocked.
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/2,
            /*maxLinesPerAccount=*/100,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/2);

        {
            AssetCache::SessionPin pin{1};
            auto aLines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(aLines);
            BEAST_EXPECT(cache->totalLineCount() <= 2);
            // May be incomplete if budget blocked mid-fill.
            auto bLines = cache->getRippleLines(bob.id());
            // Bob may get empty incomplete stub when budget exhausted.
            if (cache->overBudget() || cache->totalLineCount() >= 2)
            {
                BEAST_EXPECT(cache->totalLineCount() <= 2);
                if (!bLines || bLines->empty())
                    BEAST_EXPECT(cache->hasIncompleteLinesForSession(1));
            }
        }
        cache->releaseSession(1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testLineEpochBumpsOnLoadAndExpand()
    {
        testcase("lineEpoch bumps on load and expand");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        fundManyLines(env, alice, 4, "le");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/1);

        BEAST_EXPECT(cache->lineEpoch() == 0);
        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        auto const epoch1 = cache->lineEpoch();
        BEAST_EXPECT(epoch1 >= 1);

        if (cache->hasIncompleteLines())
        {
            BEAST_EXPECT(cache->expandIncompleteLines());
            BEAST_EXPECT(cache->lineEpoch() > epoch1);
        }
        cache->releaseSession(1);
    }

    void
    testConcurrentReadersAndAdvance()
    {
        // Multi-threaded stress intended to run under TSan CI builds as well as
        // the default unit-test matrix. Catches data races on shared_mutex
        // cache hits, session pins, and soft ledger advances.
        testcase("concurrent getRippleLines / expand / advanceLedger");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        fundManyLines(env, alice, 6, "ca");
        fundManyLines(env, bob, 4, "cb");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/8,
            /*lineChunkSize=*/2);

        std::atomic<int> errors{0};
        std::atomic<int> ops{0};
        constexpr int kThreads = 8;
        constexpr int kIters = 40;

        auto worker = [&](int sessionId, AccountID const account) {
            for (int i = 0; i < kIters; ++i)
            {
                try
                {
                    AssetCache::SessionPin pin{sessionId};
                    auto lines = cache->getRippleLines(account);
                    if (lines && lines->empty())
                        ++errors;
                    cache->expandIncompleteLines();
                    (void)cache->cacheHits();
                    (void)cache->cacheMisses();
                    (void)cache->totalLineCount();
                    (void)cache->lineEpoch();
                    (void)cache->hasIncompleteLinesForSession(sessionId);
                    ++ops;
                }
                catch (...)
                {
                    ++errors;
                }
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(kThreads + 1);
        for (int t = 0; t < kThreads; ++t)
        {
            auto const& acct = (t % 2 == 0) ? alice.id() : bob.id();
            threads.emplace_back(worker, t + 1, acct);
        }

        // Soft-advance worker: re-point at current/closed views (Env is not
        // thread-safe — no env.close() off the main thread).
        auto const viewA = env.closed();
        env.close();
        auto const viewB = env.closed();
        threads.emplace_back([&, viewA, viewB] {
            for (int i = 0; i < kIters; ++i)
            {
                try
                {
                    cache->advanceLedger(i % 2 == 0 ? viewB : viewA, /*forceClear=*/false);
                    ++ops;
                }
                catch (...)
                {
                    ++errors;
                }
            }
        });

        for (auto& th : threads)
            th.join();

        BEAST_EXPECT(errors == 0);
        BEAST_EXPECT(ops >= kThreads * kIters);
        // At least one real soft advance when seq differs (viewA → viewB).
        BEAST_EXPECT(cache->ledgerAdvances() >= 1);

        // Release all sessions — memory reclaims.
        for (int t = 0; t < kThreads; ++t)
            cache->releaseSession(t + 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

public:
    void
    run() override
    {
        testBudgetZeroEmptyStubAndExpand();
        testPendingExpandWhileShared();
        testSessionPinsSharedHub();
        testAdvanceLedgerSoftRetainAndForceClear();
        testReuseWindowExpiryReloads();
        testMaxLinesPerAccountAndChunk();
        testGlobalBudgetBlocksNewLines();
        testLineEpochBumpsOnLoadAndExpand();
        testConcurrentReadersAndAdvance();
    }
};

BEAST_DEFINE_TESTSUITE(AssetCache, rpc, xrpl);

}  // namespace xrpl
