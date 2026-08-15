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

    /**
     * Accounts with no trust lines must still publish an empty complete vector
     * so reuse hits. Leaving lines==null re-scanned the owner dir under the
     * exclusive lock on every Pathfinder hop.
     */
    void
    testEmptyAccountCachedNotRescanned()
    {
        testcase("empty account: complete miss is cached, not re-scanned");
        using namespace test::jtx;
        Env env(*this);
        Account const bare{"bare"};  // funded, no trust lines
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10000), bare, alice, gw);
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

        auto const misses0 = cache->cacheMisses();
        auto const hits0 = cache->cacheHits();

        {
            AssetCache::SessionPin pin{1};
            auto first = cache->getRippleLines(bare.id());
            // API still returns nullptr for empty (no lines to walk).
            BEAST_EXPECT(!first);
            BEAST_EXPECT(cache->cacheMisses() == misses0 + 1);
            BEAST_EXPECT(!cache->hasIncompleteLines());
            BEAST_EXPECT(!cache->hasIncompleteLinesForSession(1));

            // Second lookup must reuse the empty complete entry (hit), not miss.
            auto second = cache->getRippleLines(bare.id());
            BEAST_EXPECT(!second);
            BEAST_EXPECT(cache->cacheMisses() == misses0 + 1);
            BEAST_EXPECT(cache->cacheHits() >= hits0 + 1);
        }
        cache->releaseSession(1);

        // Non-empty account still loads normally after empty caching.
        {
            AssetCache::SessionPin pin{2};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && !lines->empty());
        }
        cache->releaseSession(2);
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
    testSoftAdvanceResetsIncompleteCursor()
    {
        testcase("soft advance keeps progress hint; reload on next access (Option A)");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        // Enough lines that a single first-load chunk cannot finish the account.
        fundManyLines(env, alice, 10, "ic");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/2);

        {
            AssetCache::SessionPin pin{1};
            // First load only (2 lines) — do not call expandIncompleteLines()
            // which would multi-chunk up to kPathExpandLinesPerWave and finish.
            auto partialLines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(partialLines && partialLines->size() == 2);
        }
        // pinCount remains until releaseSession (SessionPin only sets TLS).
        BEAST_EXPECT(cache->hasIncompleteLines());
        BEAST_EXPECT(cache->totalLineCount() == 2);

        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        // Option A: advance does not re-walk — drops line memory, keeps pin + hint.
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(cache->hasIncompleteLines());  // stub still incomplete

        {
            AssetCache::SessionPin pin{1};
            // On-demand reload from page 0 with want = prev(2) + chunk(2) = 4.
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == 4);
        }
        BEAST_EXPECT(cache->totalLineCount() == 4);
        BEAST_EXPECT(cache->hasIncompleteLines());
        cache->releaseSession(1);
        // Last pin released → entry erased (releaseSession).
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(!cache->hasIncompleteLines());

        // Unpinned incomplete is erased on soft advance (no progress-hint work).
        {
            auto unpinned = cache->getRippleLines(alice.id());  // no SessionPin
            BEAST_EXPECT(unpinned && unpinned->size() == 2);
        }
        BEAST_EXPECT(cache->hasIncompleteLines());
        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(!cache->hasIncompleteLines());
    }

    /**
     * Closed/create waves expand stubs before any getRippleLines. A single
     * expandAccountUnlocked (ForSession without LoadScope, or the first
     * iteration of a wave) must consume reloadMinLines. Otherwise want is
     * one chunk, lines becomes non-null, and the hint is stranded — hubs
     * restart at lineChunkSize every close and never pass the per-wave cap.
     */
    void
    testSoftAdvanceExpandHonorsReloadHint()
    {
        testcase("soft advance: expand restores reloadMinLines, not one chunk");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        fundManyLines(env, alice, 10, "eh");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/2);

        {
            AssetCache::SessionPin pin{1};
            auto partial = cache->getRippleLines(alice.id());
            BEAST_EXPECT(partial && partial->size() == 2);
        }
        BEAST_EXPECT(cache->totalLineCount() == 2);
        BEAST_EXPECT(cache->hasIncompleteLines());

        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(cache->hasIncompleteLines());

        // One expandAccountUnlocked (no LoadScope). Hint is prev(2)+chunk(2)=4.
        // Without the fix this returns 2 and getRippleLines then hits that 2.
        BEAST_EXPECT(cache->expandIncompleteLinesForSession(1));
        BEAST_EXPECT(cache->totalLineCount() == 4);
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == 4);
        }
        BEAST_EXPECT(cache->hasIncompleteLines());

        // Next close compounds: hint becomes 4+2=6, not a restart at 2.
        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->expandIncompleteLinesForSession(1));
        BEAST_EXPECT(cache->totalLineCount() == 6);
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == 6);
        }

        cache->releaseSession(1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    /**
     * Soft advance of an incomplete pin stashes reloadMinLines, then drops
     * the lines (freeing budget). If another pinned hub consumes that
     * budget before the first account reloads, loadOutgoing clamps `want`
     * and used to publish a tiny snapshot with reloadMinLines = 0. The next
     * advance then remembered only the small stored count, so a busy
     * account grew one chunk per ledger after a brief budget squeeze.
     */
    void
    testBudgetClampKeepsReloadHint()
    {
        testcase("budget-clamped reload keeps reloadMinLines for later expand");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        fundManyLines(env, alice, 8, "ba");
        fundManyLines(env, bob, 6, "bb");

        constexpr std::size_t kChunk = 2;
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/6,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/kChunk);

        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == kChunk);
            BEAST_EXPECT(cache->expandIncompleteLinesForSession(1));
            lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == 4);
        }
        {
            AssetCache::SessionPin pin{2};
            auto lines = cache->getRippleLines(bob.id());
            BEAST_EXPECT(lines && lines->size() == kChunk);
        }
        BEAST_EXPECT(cache->totalLineCount() == 6);

        // Stubs: alice hint = 4+2=6, bob hint = 2+2=4. Line budget freed.
        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->totalLineCount() == 0);

        // Bob consumes most of the budget before alice reloads.
        {
            AssetCache::SessionPin pin{2};
            auto lines = cache->getRippleLines(bob.id());
            BEAST_EXPECT(lines && lines->size() == 4);
        }
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            // Remaining budget is 2; snapshot shrinks. Hint must survive.
            BEAST_EXPECT(lines && lines->size() == kChunk);
        }
        BEAST_EXPECT(cache->totalLineCount() == 6);

        // Another close must not rebase the hint on the shrunken snapshot.
        // Both accounts are still incomplete, so both become 0-line stubs.
        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->totalLineCount() == 0);

        cache->releaseSession(2);

        // expand consumes preserved hint (6), not one chunk from 2.
        BEAST_EXPECT(cache->expandIncompleteLinesForSession(1));
        BEAST_EXPECT(cache->totalLineCount() == 6);
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines && lines->size() == 6);
        }

        cache->releaseSession(1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testLoadScopeDrainsIncompleteSharedHit()
    {
        testcase("LoadScope drains incomplete shared-cache hit (one-shot dest currencies)");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        fundManyLines(env, alice, 8, "ls");

        // Shared progressive partial (WS-style chunk of 2).
        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/2);

        {
            AssetCache::SessionPin pin{1};
            auto partial = cache->getRippleLines(alice.id());
            BEAST_EXPECT(partial && partial->size() == 2);
            BEAST_EXPECT(cache->hasIncompleteLines());
        }

        // One-shot LoadScope must finish the account on the same cache hit.
        {
            AssetCache::LoadScope oneShot{1000};
            AssetCache::SessionPin pin{2};
            auto full = cache->getRippleLines(alice.id());
            BEAST_EXPECT(full);
            BEAST_EXPECT(full->size() == 8);
            BEAST_EXPECT(!cache->hasIncompleteLinesForSession(2));
        }
        cache->releaseSession(1);
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

    /**
     * A complete entry that ages out of cacheReuseLedgers must reload at
     * least the previously stored line count. Reloading only lineChunkSize
     * (64 for WS) would drop a hub from thousands of lines to one chunk for
     * that wave — Pathfinder then publishes empty/partial alternatives.
     */
    void
    testReuseWindowExpiryKeepsCompleteLineCount()
    {
        testcase("reuse expiry: complete entry reloads prior line count, not one chunk");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        constexpr int kLines = 8;
        constexpr std::size_t kChunk = 2;
        fundManyLines(env, alice, kLines, "re");

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/100000,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/1,
            /*lineChunkSize=*/kChunk);

        std::size_t completeCount = 0;
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            BEAST_EXPECT(lines->size() == kChunk);
            for (int i = 0; i < 20; ++i)
            {
                if (!cache->expandIncompleteLines())
                    break;
            }
            lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            completeCount = lines->size();
            BEAST_EXPECT(completeCount == static_cast<std::size_t>(kLines));
            BEAST_EXPECT(!cache->hasIncompleteLinesForSession(1));
        }

        auto const loadedAt = cache->getLedger()->seq();
        for (int i = 0; i < 4; ++i)
        {
            env.close();
            cache->advanceLedger(env.closed(), false);
        }
        BEAST_EXPECT(cache->getLedger()->seq() > loadedAt + 1);

        // Reload happens inside getRippleLines, before any expand. Must not
        // collapse to kChunk.
        {
            AssetCache::SessionPin pin{1};
            auto lines = cache->getRippleLines(alice.id());
            BEAST_EXPECT(lines);
            BEAST_EXPECT(lines->size() == completeCount);
            BEAST_EXPECT(!cache->hasIncompleteLinesForSession(1));
        }
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

    /**
     * Closing a session while its SessionPin is still live (in-flight doUpdate)
     * must not let later getRippleLines re-increment pinCount. That leftover
     * pin would keep a shared hub after every other session has released.
     */
    void
    testReleaseSessionRefusesLaterPins()
    {
        testcase("releaseSession: in-flight SessionPin cannot re-pin after close");
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

        // Keeper session — holds alice after the closing session is released.
        {
            AssetCache::SessionPin pinKeep{2};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->totalLineCount() >= 1);

        {
            AssetCache::SessionPin pinClose{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
            cache->releaseSession(1);
            // In-flight update re-touches the same account after close.
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }

        // Keeper release must free alice. A leaked pinCount from session 1
        // would leave the hub in the cache forever.
        BEAST_EXPECT(cache->releaseSession(2) >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(cache->releaseSession(1) == 0);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    /**
     * doClose can run before the first getRippleLines. releaseSession must
     * still plant a tombstone so the subsequent SessionPin cannot create a
     * live session that nothing will ever release.
     */
    void
    testReleaseSessionBeforeFirstPin()
    {
        testcase("releaseSession: tombstone before first pin refuses later pins");
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

        BEAST_EXPECT(cache->releaseSession(1) == 0);

        {
            AssetCache::SessionPin pinClose{1};
            // Loads lines but must not pin them to the retired session.
            (void)cache->getRippleLines(alice.id());
        }

        {
            AssetCache::SessionPin pinKeep{2};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->releaseSession(2) >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testForgetSessionAllowsReuse()
    {
        testcase("forgetSession: retired id can be reused after PathRequest dies");
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
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->releaseSession(1) >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);

        // Still retired: a new SessionPin with id 1 must not re-pin.
        {
            AssetCache::SessionPin pin{1};
            (void)cache->getRippleLines(alice.id());
        }
        // Unpinned complete leftover may remain; no session owns it.
        cache->forgetSession(1);

        {
            AssetCache::SessionPin pin{1};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }
        BEAST_EXPECT(cache->releaseSession(1) >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    void
    testReleaseSessionRacesInFlightPin()
    {
        testcase("releaseSession races in-flight getRippleLines (shared hub)");
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
            AssetCache::SessionPin pinKeep{2};
            BEAST_EXPECT(cache->getRippleLines(alice.id()));
        }

        std::atomic<bool> started{false};
        std::atomic<bool> released{false};
        std::atomic<int> errors{0};
        std::thread worker([&] {
            try
            {
                AssetCache::SessionPin pin{1};
                if (!cache->getRippleLines(alice.id()))
                    ++errors;
                started.store(true, std::memory_order_release);
                while (!released.load(std::memory_order_acquire))
                    (void)cache->getRippleLines(alice.id());
                // After releaseSession: more hops must not re-pin.
                for (int i = 0; i < 32; ++i)
                    (void)cache->getRippleLines(alice.id());
            }
            catch (...)
            {
                ++errors;
            }
        });

        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();
        cache->releaseSession(1);
        released.store(true, std::memory_order_release);
        worker.join();

        BEAST_EXPECT(errors == 0);
        BEAST_EXPECT(cache->releaseSession(2) >= 1);
        BEAST_EXPECT(cache->totalLineCount() == 0);
    }

    /**
     * A retired in-flight doUpdate reloads accounts with pinCount 0. Those
     * complete unpinned entries must not occupy the line budget until idle:
     * drop them on the next soft advance.
     */
    void
    testUnpinnedCompleteDroppedOnAdvance()
    {
        testcase("unpinned complete entries are dropped on soft advance");
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

        // No SessionPin: same as a retired in-flight getRippleLines.
        BEAST_EXPECT(cache->getRippleLines(alice.id()));
        BEAST_EXPECT(cache->totalLineCount() >= 1);
        BEAST_EXPECT(!cache->hasIncompleteLines());

        env.close();
        cache->advanceLedger(env.closed(), /*forceClear=*/false);
        BEAST_EXPECT(cache->totalLineCount() == 0);
        BEAST_EXPECT(!cache->hasIncompleteLines());
    }

    /**
     * When the global line budget is exhausted, a new load must reclaim
     * unpinned complete leftovers so later accounts are not budget-blocked.
     */
    void
    testUnpinnedCompleteReclaimedOnBudget()
    {
        testcase("unpinned entries reclaimed when budget is exhausted");
        using namespace test::jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(gw["USD"](1000), alice);
        env.trust(gw["USD"](1000), bob);
        env(pay(gw, alice, gw["USD"](5)));
        env(pay(gw, bob, gw["USD"](5)));
        env.close();

        auto cache = std::make_shared<AssetCache>(
            env.current(),
            env.app().getJournal("AssetCache"),
            /*maxTotalLines=*/1,
            /*maxLinesPerAccount=*/1000,
            /*cacheReuseLedgers=*/12,
            /*lineChunkSize=*/64);

        BEAST_EXPECT(cache->getRippleLines(alice.id()));
        BEAST_EXPECT(cache->totalLineCount() >= 1);
        BEAST_EXPECT(cache->overBudget() || cache->totalLineCount() >= 1);

        // Budget is full of unpinned alice. Bob must still load (reclaim).
        auto bobLines = cache->getRippleLines(bob.id());
        BEAST_EXPECT(bobLines && !bobLines->empty());
        BEAST_EXPECT(cache->totalLineCount() >= 1);
        BEAST_EXPECT(cache->totalLineCount() <= 1);
    }

public:
    void
    run() override
    {
        testBudgetZeroEmptyStubAndExpand();
        testEmptyAccountCachedNotRescanned();
        testPendingExpandWhileShared();
        testSessionPinsSharedHub();
        testAdvanceLedgerSoftRetainAndForceClear();
        testSoftAdvanceResetsIncompleteCursor();
        testSoftAdvanceExpandHonorsReloadHint();
        testBudgetClampKeepsReloadHint();
        testLoadScopeDrainsIncompleteSharedHit();
        testReuseWindowExpiryReloads();
        testReuseWindowExpiryKeepsCompleteLineCount();
        testMaxLinesPerAccountAndChunk();
        testGlobalBudgetBlocksNewLines();
        testLineEpochBumpsOnLoadAndExpand();
        testConcurrentReadersAndAdvance();
        testReleaseSessionRefusesLaterPins();
        testReleaseSessionBeforeFirstPin();
        testForgetSessionAllowsReuse();
        testReleaseSessionRacesInFlightPin();
        testUnpinnedCompleteDroppedOnAdvance();
        testUnpinnedCompleteReclaimedOnBudget();
    }
};

BEAST_DEFINE_TESTSUITE(AssetCache, rpc, xrpl);

}  // namespace xrpl
