#include <xrpld/rpc/detail/PayGraph.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/BookDirs.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Apple libc++ has not yet shipped the C++20 std::atomic<std::shared_ptr<T>>
// specialisation, so we fall back to the (deprecated since C++20) free-function
// API.  Wrap the calls in small helpers so the deprecation warning can be
// suppressed in exactly one place.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

template <class T>
inline std::shared_ptr<T>
atomicLoad(std::shared_ptr<T> const* p, std::memory_order order) noexcept
{
    return std::atomic_load_explicit(p, order);
}

template <class T>
inline void
atomicStore(std::shared_ptr<T>* p, std::shared_ptr<T> v, std::memory_order order) noexcept
{
    std::atomic_store_explicit(p, std::move(v), order);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

//==============================================================================
// Internal helpers
//==============================================================================

namespace {

// 16.16 fixed-point scale for log2(cost_ratio) edge weights.
static constexpr long double kLogWeightScale = 65536.0L;
static constexpr long double kLog2_10 = 3.3219280948873626L;  // log2(10)

// Sentinel path cost (unreachable). Fits in int64 and is far above any
// realistic sum of log-weights on a ~8-hop path.
static constexpr std::int64_t kCostInf = std::numeric_limits<std::int64_t>::max() / 4;

// Empty-book traversal cost: worse than any real path of kMaxPathLength hops
// but still finite so structural edges remain traversable.
static constexpr std::int64_t kEmptyBookCost = kCostInf / 8;

/// Order-preserving map int64 cost -> uint64 for min-heaps / cumQuality.
/// Flips the sign bit so lower signed costs sort as lower unsigned ranks.
inline std::uint64_t
costToRank(std::int64_t cost) noexcept
{
    return static_cast<std::uint64_t>(cost) ^ (std::uint64_t{1} << 63);
}

/// Recover signed log-weight bits stored in Edge::qualityFixed.
/// kNoLiquidity must be checked by the caller before calling this.
inline std::int64_t
signedLogWeight(std::uint32_t qualityFixed) noexcept
{
    return static_cast<std::int32_t>(qualityFixed);
}

/// Convert a raw Quality uint64 (from getQuality()) into a log-space edge
/// weight that can be *added* during path search.
///
/// XRPL quality is the taker cost ratio (takerPays / takerGets).  Exchange
/// rates compose multiplicatively along a path:
///   total_cost = r1 * r2 * ... * rn
/// so the natural additive edge weight is:
///   log2(r1) + log2(r2) + ... + log2(rn) = log2(total_cost)
///
/// We store log2(ratio) in 16.16 fixed-point as the int32 bit-pattern inside
/// a uint32 (kNoLiquidity remains the all-ones sentinel and is never a valid
/// log weight).  Lower signed weight = cheaper rate.
///
/// Note: log2(ratio) may be negative when ratio < 1.  Query-time search uses
/// Dijkstra on Johnson-reweighted edges (potentials computed once per
/// snapshot) so weights are non-negative without a per-hop bias.
uint32_t
qualityToFixed(uint64_t rawQuality)
{
    if (rawQuality == 0)
        return PayGraph::kNoLiquidity;

    // Quality packing (see amountFromQuality / getRate):
    //   ratio = mantissa * 10^exponent
    //   mantissa: low 56 bits, exponent: high 8 bits biased by +100
    uint64_t const mantissa = rawQuality & 0x00FFFFFFFFFFFFFFull;
    int const exponent = static_cast<int>(rawQuality >> 56) - 100;

    if (mantissa == 0)
        return static_cast<uint32_t>(static_cast<int32_t>(0));

    // log2(mantissa * 10^exponent) = log2(mantissa) + exponent * log2(10)
    long double const logValue = std::log2(static_cast<long double>(mantissa)) +
        static_cast<long double>(exponent) * kLog2_10;

    long long const fixed = std::llround(logValue * kLogWeightScale);

    // Clamp into int32 so the bit-pattern fits in qualityFixed and never
    // collides with kNoLiquidity (0xFFFFFFFF == -1 as int32 is a valid small
    // weight; we only use kNoLiquidity as an explicit sentinel checked first).
    constexpr long long kMin =
        std::numeric_limits<int32_t>::min() + 1;  // keep -1 free? not required
    constexpr long long kMax = std::numeric_limits<int32_t>::max();
    long long const clamped = std::clamp(fixed, kMin, kMax);

    // Avoid storing the kNoLiquidity bit pattern by coincidence.
    auto bits = static_cast<uint32_t>(static_cast<int32_t>(clamped));
    if (bits == PayGraph::kNoLiquidity)
        bits = static_cast<uint32_t>(static_cast<int32_t>(clamped - 1));

    return bits;
}

// Bias so log2(amount) packs into a positive uint32 over a wide range.
// Encoding: liquidityLog = round((log2(|amount|) + kLogBias) * 65536)
// Must stay consistent between bookLiquidityLog() and edgeCost().
static constexpr long double kLogBias = 64.0L;
static constexpr long double kFixedScale = 65536.0L;

/// Pack log2(|amount|) into the Edge::liquidityLog encoding.
uint32_t
amountToLogFixed(STAmount const& amount)
{
    if (amount == beast::kZero || amount.mantissa() == 0)
        return 0;

    long double const logValue = std::log2(static_cast<long double>(amount.mantissa())) +
        static_cast<long double>(amount.exponent()) * kLog2_10;

    long long const fixed = std::llround((logValue + kLogBias) * kFixedScale);
    if (fixed < 1)
        return 1;
    if (fixed > static_cast<long long>(PayGraph::kNoLiquidity - 1))
        return PayGraph::kNoLiquidity - 1;
    return static_cast<uint32_t>(fixed);
}

/// True (signed) edge cost in log-space — used for path ranking and as the
/// base for Johnson reweighting:
///   cost = log2(rate)                            when depth covers the payment
///   cost = log2(rate) + log2(payment/depth)      when the book is thin
std::int64_t
edgeCostRaw(PayGraph::Edge const& e, STAmount const* dstAmount)
{
    if (e.qualityFixed == PayGraph::kNoLiquidity)
        return kEmptyBookCost;

    std::int64_t cost = signedLogWeight(e.qualityFixed);

    // No payment size, or no measured depth: pure rate ranking.
    if (dstAmount == nullptr || *dstAmount == beast::kZero || e.liquidityLog == 0)
        return cost;

    // liquidityLog / payLog are both biased by kLogBias; the difference is
    // the unbiased log2(payment/depth) shortfall (or surplus).
    uint32_t const payLog = amountToLogFixed(*dstAmount);
    if (payLog > e.liquidityLog)
        cost += static_cast<std::int64_t>(payLog - e.liquidityLog);

    return cost;
}

/// Non-negative Dijkstra edge weight via Johnson reweighting:
///   w'(u,v) = w(u,v) + h[u] - h[v]
/// where h[] are Snapshot::potential from computePotentials().
/// Path identity: sum w' = sum w + h[src] - h[dst] (no per-hop bias).
std::int64_t
edgeCostDijkstra(
    PayGraph::Snapshot const& snap,
    PayGraph::VID u,
    PayGraph::Edge const& e,
    STAmount const* dstAmount)
{
    std::int64_t const raw = edgeCostRaw(e, dstAmount);
    if (snap.potential.empty())
        return raw > 0 ? raw : 0;

    std::int64_t const hu = (u < snap.potential.size()) ? snap.potential[u] : 0;
    std::int64_t const hv = (e.to < snap.potential.size()) ? snap.potential[e.to] : 0;

    // w' = w + h(u) - h(v).  Guard overflow around empty-book sentinels.
    if (raw >= kEmptyBookCost / 2)
        return kEmptyBookCost;

    std::int64_t const wp = raw + hu - hv;
    // Numerical / incomplete-potential safety: Dijkstra requires >= 0.
    return wp > 0 ? wp : 0;
}

/// Convert Dijkstra reweighted distance into the true signed log-path cost.
inline std::int64_t
truePathCost(
    PayGraph::Snapshot const& snap,
    PayGraph::VID src,
    PayGraph::VID dst,
    std::int64_t dijkstraDist) noexcept
{
    if (dijkstraDist >= kCostInf / 2 || snap.potential.empty())
        return dijkstraDist;

    std::int64_t const hs = (src < snap.potential.size()) ? snap.potential[src] : 0;
    std::int64_t const hd = (dst < snap.potential.size()) ? snap.potential[dst] : 0;
    // sum w = sum w' - h[src] + h[dst]
    return dijkstraDist - hs + hd;
}

}  // namespace

//==============================================================================
// PayGraph — Private constructor
//==============================================================================

PayGraph::PayGraph(std::optional<uint256> const& domain, beast::Journal j) : domain_(domain), j_(j)
{
}

//==============================================================================
// PayGraph::snapshot()
//==============================================================================

std::shared_ptr<PayGraph::Snapshot const>
PayGraph::snapshot() const
{
    return atomicLoad(&snap_, std::memory_order_acquire);
}

//==============================================================================
// PayGraph::currentStats()
//==============================================================================

PayGraph::Stats
PayGraph::currentStats() const
{
    auto s = snapshot();
    return s ? s->stats : Stats{};
}

//==============================================================================
// Static vertex / edge helpers
//==============================================================================

PayGraph::VID
PayGraph::ensureVertex(Snapshot& snap, Asset const& asset)
{
    auto [it, inserted] = snap.index.emplace(asset, static_cast<VID>(snap.assets.size()));
    if (inserted)
    {
        snap.assets.push_back(asset);
        snap.adj.emplace_back();  // empty edge list for new vertex
        ++snap.stats.vertices;
    }
    return it->second;
}

PayGraph::Edge&
PayGraph::ensureEdge(Snapshot& snap, VID from, VID to, EdgeKind kind)
{
    assert(from < snap.adj.size());
    auto& list = snap.adj[from];
    for (auto& e : list)
    {
        if (e.to == to && e.kind == kind)
            return e;
    }
    list.push_back(Edge{.to = to, .qualityFixed = kNoLiquidity, .kind = kind});
    ++snap.stats.edges;
    return list.back();
}

//==============================================================================
// Static: query the top-of-book quality for a book from the ledger.
//
// The order-book directory is keyed by quality, and the first (lowest key)
// directory page is the best-quality (cheapest for the taker) entry.
// ReadView::succ() walks the SHAMap in ascending key order, so we find
// the smallest key >= bookBase and < qualityNext.  That page's key encodes
// the quality directly via getQuality().
//==============================================================================

uint32_t
PayGraph::topOfBookQuality(ReadView const& ledger, Book const& book)
{
    uint256 const base = getBookBase(book);
    uint256 const end = getQualityNext(base);

    auto const firstPage = ledger.succ(base, end);
    if (!firstPage)
        return kNoLiquidity;

    uint64_t const rawQ = getQuality(*firstPage);
    return qualityToFixed(rawQ);
}

//==============================================================================
// Static: single top-of-book offer size as biased log2 fixed-point.
//
// One SLE read only — used for a cheap depth signal.  Never walk the whole
// book on the hot path (that made path_find multi-second on live books).
//==============================================================================

uint32_t
PayGraph::bookLiquidityLog(ReadView const& ledger, Book const& book)
{
    BookDirs dirs(ledger, book);
    for (auto const& sle : dirs)
    {
        if (!sle)
            continue;
        auto const gets = sle->getFieldAmount(sfTakerGets);
        if (gets == beast::kZero)
            continue;
        return amountToLogFixed(gets);
    }
    return 0;
}

//==============================================================================
// Static: build a fresh Snapshot
//==============================================================================

std::shared_ptr<PayGraph::Snapshot>
PayGraph::buildSnapshot(
    OrderBookDB& bookDB,
    ReadView const& ledger,
    std::optional<uint256> const& domain,
    beast::Journal j)
{
    auto snap = std::make_shared<Snapshot>();

    // --- XRP vertex always exists -----------------------------------------
    ensureVertex(*snap, xrpIssue());

    // --- Order books -------------------------------------------------------
    // OrderBookDB tracks every known (takerPays, takerGets) book pair.
    // We seed ALL known takerPays assets up-front via getAllTakerPaysAssets(),
    // then BFS from each to collect edges via getBooksByTakerPays().
    // XRP is always seeded first as the universal bridge asset.

    // We use a simple work-queue BFS over discovered assets.
    std::vector<Asset> workQueue;
    hash_set<Asset> visited;
    workQueue.reserve(1024);  // avoid reallocation while iterating by index

    auto enqueue = [&](Asset const& a) {
        if (visited.insert(a).second)
            workQueue.push_back(a);
    };

    enqueue(xrpIssue());  // seed

    // Also seed from every known takerPays asset so that non-XRP-rooted
    // assets are discovered even if they have no direct XRP book.
    // The BFS deduplicates via `visited`.
    // Sort before enqueuing so VID assignment is deterministic across
    // processes (hardened_hash iteration order is per-process random).
    {
        auto seeds = bookDB.getAllTakerPaysAssets(domain);
        std::ranges::sort(seeds);
        for (Asset const& a : seeds)
            enqueue(a);
    }

    for (std::size_t qi = 0; qi < workQueue.size(); ++qi)
    {
        Asset const src =
            workQueue[qi];  // copy — enqueue() may realloc workQueue, invalidating refs
        auto books = bookDB.getBooksByTakerPays(src, domain);
        // Sort books so edge insertion order (and thus adj[] ordering) is
        // deterministic across processes with different hash seeds.
        std::ranges::sort(books, [](Book const& a, Book const& b) { return a.out < b.out; });
        for (Book const& book : books)
        {
            Asset const& dst = book.out;

            // Full build is rare (startup / full OB rescan), not per path_find.
            // Quality: O(1) succ.  Depth: first offer only (one SLE).
            uint32_t const q = topOfBookQuality(ledger, book);
            uint32_t const depth = bookLiquidityLog(ledger, book);

            VID const vSrc = ensureVertex(*snap, src);
            VID const vDst = ensureVertex(*snap, dst);
            Edge& e = ensureEdge(*snap, vSrc, vDst, EdgeKind::OrderBook);
            e.qualityFixed = q;
            e.liquidityLog = depth;
            ++snap->stats.orderBooks;

            enqueue(dst);
        }
    }

    // One Johnson pass per full build only (not per path_find / not every delta).
    computePotentials(*snap);

    JLOG(j.debug()) << "PayGraph::buildSnapshot: " << snap->stats.vertices << " vertices, "
                    << snap->stats.edges << " edges, " << snap->stats.orderBooks << " order books";

    return snap;
}

//==============================================================================
// PayGraph::build() — factory, called once at startup
//==============================================================================

std::shared_ptr<PayGraph>
PayGraph::build(
    OrderBookDB& bookDB,
    ReadView const& ledger,
    std::optional<uint256> const& domain,
    beast::Journal j)
{
    // Private constructor accessible through this factory only.
    auto pg = std::shared_ptr<PayGraph>(new PayGraph(domain, j));
    auto snap = buildSnapshot(bookDB, ledger, domain, j);
    atomicStore(
        &pg->snap_, std::shared_ptr<Snapshot const>(std::move(snap)), std::memory_order_release);
    return pg;
}

//==============================================================================
// PayGraph::rebuild() — full rebuild, replaces snapshot atomically
//==============================================================================

void
PayGraph::rebuild(OrderBookDB& bookDB, ReadView const& ledger, std::optional<uint256> const& domain)
{
    auto snap = buildSnapshot(bookDB, ledger, domain, j_);

    std::scoped_lock const lk(writeMu_);
    // Preserve cumulative counter from current snapshot.
    if (auto cur = snapshot())
        snap->stats.totalDeltasCalled = cur->stats.totalDeltasCalled;

    atomicStore(
        &snap_, std::shared_ptr<Snapshot const>(std::move(snap)), std::memory_order_release);
}

//==============================================================================
// PayGraph::applyLedgerDelta()
//
// Called by PathRequestManager at each ledger close.  changedBooks contains
// only the books that had offer activity in the just-closed ledger.
// We make a copy of the current snapshot (cheap: ~50 KB), patch each changed
// book's edge weight, then atomically publish the new snapshot.
//==============================================================================

void
PayGraph::applyLedgerDelta(
    OrderBookDB& bookDB,
    ReadView const& newLedger,
    std::vector<Book> const& changedBooks)
{
    if (changedBooks.empty())
        return;

    // ---------- acquire write lock ----------------------------------------
    std::scoped_lock const lk(writeMu_);

    // Shallow-copy the current snapshot.  All vectors are value-copied.
    auto cur = atomicLoad(&snap_, std::memory_order_acquire);
    if (!cur)
    {
        // No snapshot yet — do a full build instead.
        auto fresh = buildSnapshot(bookDB, newLedger, domain_, j_);
        atomicStore(
            &snap_, std::shared_ptr<Snapshot const>(std::move(fresh)), std::memory_order_release);
        return;
    }

    auto next = std::make_shared<Snapshot>(*cur);  // value copy
    next->stats.lastDeltaBooks = static_cast<uint32_t>(changedBooks.size());
    next->stats.totalDeltasCalled = cur->stats.totalDeltasCalled + 1;

    // ---------- patch changed edges ---------------------------------------
    // Only books that had offer activity this ledger (usually << 100).
    // Quality: O(1) succ.  Depth: one top offer if present.  No O(VE)
    // potential recompute — Dijkstra clamps reweighted costs to >= 0 using
    // the last full-build potentials (good enough for ranking).
    for (Book const& book : changedBooks)
    {
        uint32_t const newQ = topOfBookQuality(newLedger, book);
        uint32_t const newDepth = bookLiquidityLog(newLedger, book);

        VID const vSrc = ensureVertex(*next, book.in);
        VID const vDst = ensureVertex(*next, book.out);
        Edge& e = ensureEdge(*next, vSrc, vDst, EdgeKind::OrderBook);
        e.qualityFixed = newQ;
        e.liquidityLog = newDepth;
    }

    // Keep existing potentials; do not recompute O(VE) every ~3s ledger.

    JLOG(j_.trace()) << "PayGraph::applyLedgerDelta: patched " << changedBooks.size()
                     << " books, delta #" << next->stats.totalDeltasCalled;

    // ---------- publish ---------------------------------------------------
    atomicStore(
        &snap_, std::shared_ptr<Snapshot const>(std::move(next)), std::memory_order_release);
}

//==============================================================================
// Vertex helpers (operate on current snapshot)
//==============================================================================

PayGraph::VID
PayGraph::vertexOf(Asset const& asset) const
{
    auto s = snapshot();
    if (!s)
        return kNull;
    auto it = s->index.find(asset);
    return (it != s->index.end()) ? it->second : kNull;
}

Asset const&
PayGraph::assetOf(VID v) const
{
    static Asset const kEmpty;
    auto s = snapshot();
    if (!s || v >= s->assets.size())
        return kEmpty;
    return s->assets[v];
}

//==============================================================================
// Johnson potentials — computed once per snapshot (build / ledger delta).
//
// Signed log2(rate) weights may be negative (cost_ratio < 1).  Dijkstra needs
// non-negative weights, so we compute potentials h[v] such that
//   w'(u,v) = w(u,v) + h[u] - h[v]  >= 0
// for every real edge.  This is Bellman-Ford from a virtual super-source with
// 0-weight edges into every vertex (i.e. initialise h = 0 and relax).  Cost is
// O(VE) once per snapshot — not per pathfind.
//==============================================================================

void
PayGraph::computePotentials(Snapshot& snap)
{
    uint32_t const n = static_cast<uint32_t>(snap.assets.size());
    snap.potential.assign(n, 0);

    if (n == 0)
        return;

    // |V|-1 relaxation rounds.  Early-exit when stable.
    for (uint32_t pass = 0; pass + 1 < n; ++pass)
    {
        bool updated = false;
        for (VID u = 0; u < n; ++u)
        {
            if (u >= snap.adj.size())
                continue;
            for (Edge const& e : snap.adj[u])
            {
                if (e.qualityFixed == kNoLiquidity)
                    continue;  // structural empty — not a real rate
                VID const v = e.to;
                if (v >= n)
                    continue;

                std::int64_t const w = signedLogWeight(e.qualityFixed);
                // h[v] > h[u] + w  →  improve
                if (snap.potential[u] > kCostInf / 2 + w)
                    continue;  // overflow guard
                std::int64_t const cand = snap.potential[u] + w;
                if (cand < snap.potential[v])
                {
                    snap.potential[v] = cand;
                    updated = true;
                }
            }
        }
        if (!updated)
            break;
    }
}

//==============================================================================
// Single-source shortest paths (Dijkstra on Johnson-reweighted log-costs).
//
// True edge costs are signed log2(cost_ratio) (+ optional liquidity penalty).
// Query-time Dijkstra uses non-negative w' = w + h[u] - h[v].  True path cost
// is recovered as dist'[dst] - h[src] + h[dst].
//
// blockedVerts/Edges: Yen's algorithm k-shortest enumeration
// dstAmount: thin-book log-shortfall penalty (request-scoped, >= 0)
//==============================================================================

PayGraph::DijkResult
PayGraph::dijkstra(
    Snapshot const& snap,
    VID src,
    std::vector<bool> const* blockedVerts,
    BlockedEdges const* blockedEdges,
    STAmount const* dstAmount)
{
    uint32_t const n = static_cast<uint32_t>(snap.assets.size());

    DijkResult res;
    res.dist.assign(n, kCostInf);
    res.prev.assign(n, kNull);

    if (src >= n)
        return res;
    if ((blockedVerts != nullptr) && src < blockedVerts->size() && (*blockedVerts)[src])
        return res;

    res.dist[src] = 0;

    // Min-heap: (reweighted cost, vertex)
    using PQ = std::priority_queue<
        std::pair<std::int64_t, VID>,
        std::vector<std::pair<std::int64_t, VID>>,
        std::greater<>>;

    PQ pq;
    pq.emplace(0, src);

    while (!pq.empty())
    {
        auto [cost, u] = pq.top();
        pq.pop();

        if (cost > res.dist[u])
            continue;  // stale heap entry

        if (u >= snap.adj.size())
            continue;

        for (Edge const& e : snap.adj[u])
        {
            VID const v = e.to;
            if (v >= n)
                continue;
            if ((blockedVerts != nullptr) && v < blockedVerts->size() && (*blockedVerts)[v])
                continue;

            if (blockedEdges != nullptr)
            {
                bool edgeBlocked = false;
                for (auto const& [bfrom, bto] : *blockedEdges)
                {
                    if (bfrom == u && bto == v)
                    {
                        edgeBlocked = true;
                        break;
                    }
                }
                if (edgeBlocked)
                    continue;
            }

            std::int64_t const w = edgeCostDijkstra(snap, u, e, dstAmount);
            if (w >= kCostInf / 2 || res.dist[u] >= kCostInf - w)
                continue;

            std::int64_t const newCost = res.dist[u] + w;
            if (newCost < res.dist[v])
            {
                res.dist[v] = newCost;
                res.prev[v] = u;
                pq.emplace(newCost, v);
            }
        }
    }

    return res;
}

std::vector<PayGraph::VID>
PayGraph::reconstructPath(DijkResult const& res, VID src, VID dst)
{
    if (dst >= res.dist.size() || res.dist[dst] >= kCostInf / 2)
        return {};  // unreachable

    std::vector<VID> path;
    for (VID v = dst; v != kNull; v = res.prev[v])
    {
        path.push_back(v);
        if (v == src)
            break;
        if (path.size() > res.dist.size())
            return {};  // cycle guard
    }

    std::ranges::reverse(path);
    return path;
}

//==============================================================================
// Yen's K-Shortest Paths algorithm
//
// Finds up to k shortest simple paths from src to dst using Dijkstra as the
// shortest-path oracle.  On a graph with V=1000, E=1000, k=6 this completes
// in well under 1 ms.
//
// Reference: Yen, J.Y. (1971). "Finding the K Shortest Loopless Paths in a
//            Network". Management Science 17(11): 712–716.
//==============================================================================

std::vector<PayGraph::AssetPath>
PayGraph::kShortestPaths(Snapshot const& snap, VID src, VID dst, int k, STAmount const& dstAmount)
{
    uint32_t const n = static_cast<uint32_t>(snap.assets.size());
    if (src >= n || dst >= n || k <= 0)
        return {};

    STAmount const* const pay = (dstAmount == beast::kZero) ? nullptr : &dstAmount;

    std::vector<AssetPath> a;  // confirmed k-shortest paths
    a.reserve(k);

    // Candidate set: (rank, path) ordered by rank ascending (lower = better).
    // rank = costToRank(signed log-sum) so negative costs order correctly.
    using Candidate = std::pair<uint64_t, std::vector<VID>>;
    auto cmpCand = [](Candidate const& a, Candidate const& b) {
        return a.first > b.first;  // min-heap on rank
    };
    std::priority_queue<Candidate, std::vector<Candidate>, decltype(cmpCand)> b(cmpCand);

    // Find the first (shortest) path.
    {
        auto res = dijkstra(snap, src, nullptr, nullptr, pay);
        auto path = reconstructPath(res, src, dst);
        if (path.empty())
            return {};  // no path at all
        // Rank by true log-cost (unwrap Johnson reweighting).
        b.emplace(costToRank(truePathCost(snap, src, dst, res.dist[dst])), std::move(path));
    }

    while (!b.empty() && static_cast<int>(a.size()) < k)
    {
        auto [rank, prev] = b.top();
        b.pop();

        // Deduplicate (same path may be inserted multiple times).
        bool dup = false;
        for (auto const& ap : a)
        {
            if (ap.vids == prev)
            {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;

        a.push_back({prev, rank});

        if (static_cast<int>(a.size()) == k)
            break;

        // For each spur node along the accepted path (except the last node):
        for (std::size_t i = 0; i + 1 < prev.size(); ++i)
        {
            VID const spurNode = prev[i];
            // Root path = prev[0..i]
            std::vector<VID> const rootPath(prev.begin(), prev.begin() + i + 1);

            // Block vertices in the root path (except spurNode itself) to
            // prevent spur paths from re-using the prefix (avoids cycles).
            std::vector<bool> blockedVerts(n, false);
            for (std::size_t j = 0; j < i; ++j)
                blockedVerts[rootPath[j]] = true;

            // Block forward edges from spurNode that are already used by
            // accepted paths sharing the same root prefix.  This is the
            // critical part of Yen's: without it, the oracle just finds the
            // same path again instead of exploring alternatives.
            BlockedEdges blockedEdges;
            for (auto const& ap : a)
            {
                auto const& av = ap.vids;
                if (av.size() > i + 1 &&
                    std::equal(
                        av.begin(),
                        av.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        rootPath.begin()))
                {
                    blockedEdges.emplace_back(spurNode, av[i + 1]);
                }
            }

            auto res = dijkstra(snap, spurNode, &blockedVerts, &blockedEdges, pay);
            auto spur = reconstructPath(res, spurNode, dst);
            if (spur.empty())
                continue;

            // Full candidate path = rootPath + spur (excluding duplicate spurNode).
            std::vector<VID> candidate = rootPath;
            candidate.insert(candidate.end(), spur.begin() + 1, spur.end());

            // Sum *true* signed log-costs (not reweighted) so ranking matches
            // multiplicative rate composition across hop counts.
            std::int64_t candidateCost = 0;
            bool valid = true;
            for (std::size_t j = 0; j + 1 < candidate.size(); ++j)
            {
                VID const u = candidate[j];
                VID const v = candidate[j + 1];
                if (u >= snap.adj.size())
                {
                    valid = false;
                    break;
                }
                std::int64_t best = kCostInf;
                bool found = false;
                for (auto const& e : snap.adj[u])
                {
                    if (e.to == v)
                    {
                        std::int64_t const w = edgeCostRaw(e, pay);
                        if (!found || w < best)
                        {
                            best = w;
                            found = true;
                        }
                    }
                }
                if (!found || best >= kEmptyBookCost / 2)
                {
                    valid = false;
                    break;
                }
                if (candidateCost >= kCostInf - best)
                {
                    valid = false;
                    break;
                }
                candidateCost += best;
            }
            if (valid)
                b.emplace(costToRank(candidateCost), std::move(candidate));
        }
    }

    return a;
}

//==============================================================================
// PayGraph::findPaths() — convenience wrapper
//==============================================================================

std::vector<PayGraph::AssetPath>
PayGraph::findPaths(Asset const& src, Asset const& dst, int k, STAmount const& dstAmount) const
{
    auto s = snapshot();
    if (!s)
        return {};

    auto itSrc = s->index.find(src);
    auto itDst = s->index.find(dst);
    if (itSrc == s->index.end() || itDst == s->index.end())
        return {};

    return kShortestPaths(*s, itSrc->second, itDst->second, k, dstAmount);
}

}  // namespace xrpl
