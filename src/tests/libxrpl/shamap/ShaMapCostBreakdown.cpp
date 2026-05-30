// Plan 7 — Phase 0 cost-breakdown benchmark.
//
// Splits per-close SHAMap cost into three buckets so the Phase-1 vs Phase-2
// build decision rests on measured numbers, not the (corrected) cost model in
// tasks/plan-7-deferred-shamap.md. See tasks/plan-7-quantify.md for the why.
//
//   COW              = clone allocations on first touch  (already deduped today)
//   traversal+dirty  = descent + setItem/setChild        (Phase-1 bulkApply target)
//   serial hashing   = bottom-up unshare() at close       (Phase-2 parallel target)
//
// Gated behind the SHAMAP_BENCH env var so it never runs in normal CI.
// Run with:  SHAMAP_BENCH=1 ./xrpl.test.shamap

#include <helpers/TestFamily.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace xrpl::test {

namespace {

using Clock = std::chrono::steady_clock;
using Ns = std::chrono::nanoseconds;

[[nodiscard]] bool
benchEnabled()
{
    char const* v = std::getenv("SHAMAP_BENCH");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

// Fill a uint256 with 32 pseudo-random bytes. State keys on mainnet are
// SHA-512Half digests, i.e. uniformly distributed — random bytes give a
// representative (uniform, depth ~log16 N) tree shape.
[[nodiscard]] uint256
randomKey(std::mt19937_64& rng)
{
    uint256 k;
    auto* p = k.data();
    for (std::size_t i = 0; i < k.size(); i += 8)
    {
        std::uint64_t const r = rng();
        std::memcpy(p + i, &r, 8);
    }
    return k;
}

// A ~128-byte value whose leading bytes encode `salt`, so successive
// replacements of the same key always differ (forcing setItem to re-dirty).
constexpr std::size_t kValueBytes = 128;

[[nodiscard]] boost::intrusive_ptr<SHAMapItem const>
makeItem(uint256 const& key, std::uint64_t salt)
{
    std::array<std::uint8_t, kValueBytes> buf{};
    std::memcpy(buf.data(), &salt, sizeof(salt));
    std::memcpy(buf.data() + sizeof(salt), key.data(), 16);
    return makeShamapitem(key, Slice(buf.data(), buf.size()));
}

struct Trial
{
    double tColdNs = 0;     // traversal + COW + dirty
    double tWarmNs = 0;     // traversal + dirty (no COW)
    double tHashNs = 0;     // serial bottom-up hash recompute (unshare)
    double tHashParNs = 0;  // updateHashesParallel(kParWorkers)
    int dirtyNodes = 0;     // nodes processed by the close-time recompute
};

constexpr int kParWorkers = 8;

[[nodiscard]] double
median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

// One (N, M) measurement: build a base map of N entries, then on fresh
// snapshots time M random replacements (cold), the same again warm, and the
// close-time recompute.
[[nodiscard]] Trial
measure(std::size_t N, std::size_t M, int iters, beast::Journal j)
{
    std::mt19937_64 rng(0xC0FFEEull ^ (N * 1000003ull + M));

    TestFamily family(j);
    auto base = std::make_shared<SHAMap>(SHAMapType::STATE, family);
    base->setUnbacked();

    std::vector<uint256> keys;
    keys.reserve(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        uint256 const k = randomKey(rng);
        keys.push_back(k);
        base->addItem(SHAMapNodeType::TnAccountState, makeItem(k, 0));
    }
    base->getHash();   // settle: all base nodes become shared (cowid 0)

    std::uniform_int_distribution<std::size_t> pick(0, N - 1);

    std::vector<double> cold, warm, hash, hashPar;
    std::vector<int> dirty;
    cold.reserve(iters);
    warm.reserve(iters);
    hash.reserve(iters);
    hashPar.reserve(iters);
    dirty.reserve(iters);

    for (int it = 0; it < iters; ++it)
    {
        // Choose M distinct existing keys for this iteration.
        std::vector<uint256> sel;
        sel.reserve(M);
        {
            std::vector<bool> seen(N, false);
            while (sel.size() < M)
            {
                std::size_t const idx = pick(rng);
                if (!seen[idx])
                {
                    seen[idx] = true;
                    sel.push_back(keys[idx]);
                }
            }
        }

        // --- COLD: traversal + COW + dirty, on a fresh snapshot ---
        {
            auto m = base->snapShot(/*isMutable=*/true);
            auto const t0 = Clock::now();
            std::uint64_t salt = 1;
            for (auto const& k : sel)
                m->updateGiveItem(
                    SHAMapNodeType::TnAccountState, makeItem(k, salt++));
            auto const t1 = Clock::now();
            cold.push_back(
                std::chrono::duration_cast<Ns>(t1 - t0).count());

            // --- HASH: the serial recompute getHash() would drive ---
            auto const h0 = Clock::now();
            int const flushed = m->unshare();
            auto const h1 = Clock::now();
            hash.push_back(
                std::chrono::duration_cast<Ns>(h1 - h0).count());
            dirty.push_back(flushed);
        }

        // --- WARM: traversal + dirty, no COW (nodes already at cowid) ---
        {
            auto m = base->snapShot(/*isMutable=*/true);
            std::uint64_t salt = 1;
            for (auto const& k : sel)  // warm-up pass clones every path
                m->updateGiveItem(
                    SHAMapNodeType::TnAccountState, makeItem(k, salt++));

            auto const w0 = Clock::now();
            for (auto const& k : sel)  // timed pass: no clones
                m->updateGiveItem(
                    SHAMapNodeType::TnAccountState, makeItem(k, salt++));
            auto const w1 = Clock::now();
            warm.push_back(
                std::chrono::duration_cast<Ns>(w1 - w0).count());
        }

        // --- PARALLEL HASH: the Phase-2 path on a fresh dirty snapshot ---
        {
            auto m = base->snapShot(/*isMutable=*/true);
            std::uint64_t salt = 1;
            for (auto const& k : sel)
                m->updateGiveItem(
                    SHAMapNodeType::TnAccountState, makeItem(k, salt++));

            auto const p0 = Clock::now();
            m->updateHashesParallel(kParWorkers);
            auto const p1 = Clock::now();
            hashPar.push_back(
                std::chrono::duration_cast<Ns>(p1 - p0).count());
        }
    }

    Trial r;
    r.tColdNs = median(cold);
    r.tWarmNs = median(warm);
    r.tHashNs = median(hash);
    r.tHashParNs = median(hashPar);
    r.dirtyNodes = dirty.empty() ? 0 : dirty[dirty.size() / 2];
    return r;
}

void
printRow(std::size_t N, std::size_t M, Trial const& t)
{
    double const cowNs = std::max(0.0, t.tColdNs - t.tWarmNs);
    auto us = [](double ns) { return ns / 1000.0; };
    std::cout << std::fixed << std::setprecision(1) << "  " << std::setw(9) << N
              << std::setw(7) << M << " |" << std::setw(9) << us(t.tWarmNs)
              << std::setw(9) << us(cowNs) << std::setw(10) << us(t.tHashNs)
              << std::setw(10) << us(t.tHashParNs) << " |" << std::setw(7)
              << t.dirtyNodes << std::setw(9)
              << (t.tHashParNs > 0 ? t.tHashNs / t.tHashParNs : 0.0) << "x\n";
}

// --- Backed-map flush split (Phase-3 sizing) ---------------------------------
//
// The real close path computes the state root via flushDirty() == hash + write
// to the nodestore, in one serial walk — NOT via getHash(). updateHashesParallel
// only parallelizes the hash half; the write half (writeNode → canonicalize is
// serialized by the TreeNodeCache's single mutex) stays serial. So the
// realizable close win is bounded by the hash fraction of flush.
//
// We measure on a BACKED map (memory nodestore, like production) per (N,M):
//   flush      = flushDirty()      — today's serial close cost (hash + write)
//   s.hash     = unshare()         — serial hash only
//   p.hash     = updateHashesParallel
// Projected Phase-3 close = flush - (s.hash - p.hash)   [replace serial hash
// with parallel hash; write half unchanged].

struct BackedTrial
{
    double flushNs = 0;
    double sHashNs = 0;
    double pHashNs = 0;
    int dirtyNodes = 0;
};

[[nodiscard]] BackedTrial
measureBacked(std::size_t N, std::size_t M, int iters, beast::Journal j)
{
    std::mt19937_64 rng(0xF1A7ull ^ (N * 1000003ull + M));

    TestFamily family(j);  // backed: do NOT call setUnbacked()
    auto base = std::make_shared<SHAMap>(SHAMapType::STATE, family);

    std::vector<uint256> keys;
    keys.reserve(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        uint256 const k = randomKey(rng);
        keys.push_back(k);
        base->addItem(SHAMapNodeType::TnAccountState, makeItem(k, 0));
    }
    base->flushDirty(NodeObjectType::AccountNode);  // settle + persist base

    std::uniform_int_distribution<std::size_t> pick(0, N - 1);
    std::vector<double> flush, sHash, pHash;
    std::vector<int> dirty;

    auto dirtySnapshot = [&](std::vector<uint256> const& sel) {
        auto m = base->snapShot(/*isMutable=*/true);
        std::uint64_t salt = 1;
        for (auto const& k : sel)
            m->updateGiveItem(SHAMapNodeType::TnAccountState, makeItem(k, salt++));
        return m;
    };

    for (int it = 0; it < iters; ++it)
    {
        std::vector<uint256> sel;
        sel.reserve(M);
        std::vector<bool> seen(N, false);
        while (sel.size() < M)
        {
            std::size_t const idx = pick(rng);
            if (!seen[idx])
            {
                seen[idx] = true;
                sel.push_back(keys[idx]);
            }
        }

        {
            auto m = dirtySnapshot(sel);
            auto const t0 = Clock::now();
            int const f = m->flushDirty(NodeObjectType::AccountNode);
            flush.push_back(
                std::chrono::duration_cast<Ns>(Clock::now() - t0).count());
            dirty.push_back(f);
        }
        {
            auto m = dirtySnapshot(sel);
            auto const t0 = Clock::now();
            m->unshare();
            sHash.push_back(
                std::chrono::duration_cast<Ns>(Clock::now() - t0).count());
        }
        {
            auto m = dirtySnapshot(sel);
            auto const t0 = Clock::now();
            m->updateHashesParallel(kParWorkers);
            pHash.push_back(
                std::chrono::duration_cast<Ns>(Clock::now() - t0).count());
        }
    }

    BackedTrial r;
    r.flushNs = median(flush);
    r.sHashNs = median(sHash);
    r.pHashNs = median(pHash);
    r.dirtyNodes = dirty.empty() ? 0 : dirty[dirty.size() / 2];
    return r;
}

void
printBackedRow(std::size_t N, std::size_t M, BackedTrial const& t)
{
    auto us = [](double ns) { return ns / 1000.0; };
    double const projected = std::max(0.0, t.flushNs - (t.sHashNs - t.pHashNs));
    double const hashFrac = t.flushNs > 0 ? t.sHashNs / t.flushNs : 0.0;
    std::cout << std::fixed << std::setprecision(1) << "  " << std::setw(9) << N
              << std::setw(7) << M << " |" << std::setw(10) << us(t.flushNs)
              << std::setw(10) << us(t.sHashNs) << std::setw(10) << us(t.pHashNs)
              << std::setw(11) << us(projected) << " |" << std::setw(7)
              << std::setprecision(0) << (hashFrac * 100) << "%"
              << std::setw(8) << std::setprecision(2)
              << (projected > 0 ? t.flushNs / projected : 0.0) << "x\n";
}

}  // namespace

TEST(ShaMapCostBreakdown, Report)
{
    if (!benchEnabled())
        GTEST_SKIP() << "set SHAMAP_BENCH=1 to run the cost-breakdown benchmark";

    beast::Journal const j{beast::Journal::getNullSink()};

    std::cout << "\nPlan 7 Phase-0 — SHAMap per-close cost breakdown\n"
              << "(median over iterations; times in microseconds for the whole "
                 "batch of M replaces)\n\n"
              << "        N      M | travrsl      COW    s.hash   p.hash |"
                 "  dirty  speedup\n"
              << "  -----------------------------------------------------------"
                 "---------------\n";

    struct Case
    {
        std::size_t N;
        std::size_t M;
        int iters;
    };
    std::array<Case, 4> const cases{
        {{50'000, 1'000, 7},
         {50'000, 3'000, 7},
         {200'000, 1'000, 5},
         {200'000, 3'000, 5}}};

    for (auto const& c : cases)
        printRow(c.N, c.M, measure(c.N, c.M, c.iters, j));

    std::cout << "\n  travrsl = traversal+dirty (Phase-1 bulkApply ceiling)\n"
                 "  COW     = clone allocs (already deduped by cowid today)\n"
                 "  s.hash  = serial bottom-up recompute (status quo at close)\n"
                 "  p.hash  = updateHashesParallel(" << kParWorkers
              << ") (Phase-2)\n"
                 "  speedup = s.hash / p.hash\n\n";
}

TEST(ShaMapCostBreakdown, BackedFlush)
{
    if (!benchEnabled())
        GTEST_SKIP() << "set SHAMAP_BENCH=1 to run the cost-breakdown benchmark";

    beast::Journal const j{beast::Journal::getNullSink()};

    std::cout << "\nPlan 7 Phase-3 sizing — backed-map flush split\n"
                 "(real close computes the root via flushDirty = hash + write; "
                 "memory nodestore)\n\n"
                 "        N      M |    flush    s.hash    p.hash  projected |"
                 " hashfr speedup\n"
                 "  --------------------------------------------------------"
                 "------------------\n";

    struct Case
    {
        std::size_t N;
        std::size_t M;
        int iters;
    };
    std::array<Case, 4> const cases{
        {{50'000, 1'000, 5},
         {50'000, 3'000, 5},
         {200'000, 1'000, 4},
         {200'000, 3'000, 4}}};

    for (auto const& c : cases)
        printBackedRow(c.N, c.M, measureBacked(c.N, c.M, c.iters, j));

    std::cout << "\n  flush     = flushDirty() — today's serial close (hash+write)\n"
                 "  projected = flush - (s.hash - p.hash)  [parallel hash, "
                 "write half unchanged]\n"
                 "  hashfr    = s.hash / flush  (hash share of close)\n"
                 "  speedup   = flush / projected  (realizable Phase-3 close lift)\n\n";
}

}  // namespace xrpl::test
