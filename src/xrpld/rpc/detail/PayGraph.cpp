#include <xrpld/rpc/detail/PayGraph.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>

#include "xrpl/basics/UnorderedContainers.h"
#include "xrpl/basics/base_uint.h"
#include "xrpl/beast/utility/Journal.h"
#include "xrpl/ledger/OrderBookDB.h"
#include "xrpl/protocol/Asset.h"
#include "xrpl/protocol/Book.h"
#include "xrpl/protocol/Issue.h"

#include <algorithm>
#include <atomic>
#include <cassert>
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

//==============================================================================
// Internal helpers
//==============================================================================

namespace {

/// Convert a raw Quality uint64 (from getQuality()) into our 16.16
/// fixed-point edge weight.
///
/// XRPL Quality encoding stores (out/in) as a floating-point mantissa in
/// the high bits.  A quality of 1.0 (par) ≡ QUALITY_ONE = 1e9.
/// We normalise so that par = 65536 (1 << 16) and compute:
///   qualityFixed = round(65536 * QUALITY_ONE / rawQuality)
/// where rawQuality is the encoded uint64 integer representation.
///
/// Lower qualityFixed = better rate (taker pays less per unit received).
///
/// Note: Quality::rate() returns (in/out) = the cost to the taker.
///       qualityFixed encodes cost, so lower is better — consistent with
///       Dijkstra minimisation.
uint32_t
qualityToFixed(uint64_t rawQuality)
{
    // Quality encoding: value = mantissa * 10^(exponent-97) where the
    // packed bits store mantissa and exponent.  The simplest path is
    // to reconstruct the rate via Quality::rate() which gives an STAmount.
    // However STAmount involves heap allocation.  Instead we use the
    // relationship:  rate = QUALITY_ONE / rawQuality_as_double.

    if (rawQuality == 0)
        return PayGraph::kNoLiquidity;

    // Quality stores (out / in) as a fixed-mantissa, fixed-exponent value.
    // Higher raw value = better quality for the taker (more out per in).
    // We want: lower edge weight = better.  So we invert:
    //   qualityFixed = round(kPar * kQualityMax / rawQuality)
    // clamp to [1, kNoLiquidity-1].
    static constexpr uint64_t kPar = 65536;
    static constexpr uint64_t kQualityMax = 0xFFFF'FFFF'FFFF'FFFFull;

    // Avoid divide-by-zero; rawQuality == 0 already handled above.
    uint64_t fixed = kPar * (kQualityMax / rawQuality);
    if (fixed == 0)
        fixed = 1;
    if (fixed >= PayGraph::kNoLiquidity)
        fixed = PayGraph::kNoLiquidity - 1;
    return static_cast<uint32_t>(fixed);
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
    return std::atomic_load_explicit(
        &const_cast<PayGraph*>(this)->snap_, std::memory_order_acquire);
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

            uint32_t const q = topOfBookQuality(ledger, book);
            // Add edge src -> dst even if no liquidity (structural presence).

            VID const vSrc = ensureVertex(*snap, src);
            VID const vDst = ensureVertex(*snap, dst);
            Edge& e = ensureEdge(*snap, vSrc, vDst, EdgeKind::OrderBook);
            e.qualityFixed = q;
            ++snap->stats.orderBooks;

            enqueue(dst);
        }
    }

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
    std::atomic_store_explicit(
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

    std::atomic_store_explicit(
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
    auto cur = std::atomic_load_explicit(&snap_, std::memory_order_acquire);
    if (!cur)
    {
        // No snapshot yet — do a full build instead.
        auto fresh = buildSnapshot(bookDB, newLedger, domain_, j_);
        std::atomic_store_explicit(
            &snap_, std::shared_ptr<Snapshot const>(std::move(fresh)), std::memory_order_release);
        return;
    }

    auto next = std::make_shared<Snapshot>(*cur);  // value copy
    next->stats.lastDeltaBooks = static_cast<uint32_t>(changedBooks.size());
    next->stats.totalDeltasCalled = cur->stats.totalDeltasCalled + 1;

    // ---------- patch changed edges ---------------------------------------
    for (Book const& book : changedBooks)
    {
        uint32_t const newQ = topOfBookQuality(newLedger, book);

        // Ensure both endpoints exist (a book might be new this ledger).
        VID const vSrc = ensureVertex(*next, book.in);
        VID const vDst = ensureVertex(*next, book.out);
        Edge& e = ensureEdge(*next, vSrc, vDst, EdgeKind::OrderBook);
        e.qualityFixed = newQ;

        // Also update the reverse direction if that book exists.
        // (Order books are one-directional by definition, so we only update
        //  the forward edge.  The reverse book is a separate changedBook
        //  entry if it also had activity.)
    }

    JLOG(j_.trace()) << "PayGraph::applyLedgerDelta: patched " << changedBooks.size()
                     << " books, delta #" << next->stats.totalDeltasCalled;

    // ---------- publish ---------------------------------------------------
    std::atomic_store_explicit(
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
// Dijkstra — single-source shortest paths on the asset graph.
//
// blocked: optional bitmask of vertices to treat as unreachable (used by
//          Yen's algorithm to enumerate k-shortest paths by temporarily
//          removing spur vertices).
//==============================================================================

PayGraph::DijkResult
PayGraph::dijkstra(
    Snapshot const& snap,
    VID src,
    std::vector<bool> const* blockedVerts,
    BlockedEdges const* blockedEdges)
{
    uint32_t const n = static_cast<uint32_t>(snap.assets.size());

    DijkResult res;
    res.dist.assign(n, std::numeric_limits<uint64_t>::max());
    res.prev.assign(n, kNull);

    if (src >= n)
        return res;
    if ((blockedVerts != nullptr) && src < blockedVerts->size() && (*blockedVerts)[src])
        return res;

    res.dist[src] = 0;

    // Min-heap: (cost, vertex)
    using PQ = std::priority_queue<
        std::pair<uint64_t, VID>,
        std::vector<std::pair<uint64_t, VID>>,
        std::greater<>>;

    PQ pq;
    pq.emplace(0ull, src);

    while (!pq.empty())
    {
        auto [cost, u] = pq.top();
        pq.pop();

        if (cost > res.dist[u])
            continue;  // stale entry

        if (u >= snap.adj.size())
            continue;

        for (Edge const& e : snap.adj[u])
        {
            VID const v = e.to;
            if (v >= n)
                continue;
            if ((blockedVerts != nullptr) && v < blockedVerts->size() && (*blockedVerts)[v])
                continue;

            // Check if this specific edge (u -> v) is blocked.
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

            // Edges with no current top-of-book offer are traversed with a
            // very high cost so they rank last.  rippleCalculate is the
            // authoritative liquidity check — we must not skip structural
            // edges, as offers placed before the graph was built may still
            // be present in the ledger.
            uint64_t const edgeCost = (e.qualityFixed == kNoLiquidity)
                ? static_cast<uint64_t>(kNoLiquidity - 1)
                : e.qualityFixed;

            uint64_t const newCost = cost + edgeCost;
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
    if (res.dist[dst] == std::numeric_limits<uint64_t>::max())
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
PayGraph::kShortestPaths(Snapshot const& snap, VID src, VID dst, int k)
{
    uint32_t const n = static_cast<uint32_t>(snap.assets.size());
    if (src >= n || dst >= n || k <= 0)
        return {};

    std::vector<AssetPath> a;  // confirmed k-shortest paths
    a.reserve(k);

    // Candidate set: (cumQuality, path) ordered by quality ascending.
    using Candidate = std::pair<uint64_t, std::vector<VID>>;
    auto cmpCand = [](Candidate const& a, Candidate const& b) {
        return a.first > b.first;  // min-heap
    };
    std::priority_queue<Candidate, std::vector<Candidate>, decltype(cmpCand)> b(cmpCand);

    // Find the first (shortest) path.
    {
        auto res = dijkstra(snap, src);
        auto path = reconstructPath(res, src, dst);
        if (path.empty())
            return {};  // no path at all
        b.emplace(res.dist[dst], std::move(path));
    }

    while (!b.empty() && static_cast<int>(a.size()) < k)
    {
        auto [cost, prev] = b.top();
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

        a.push_back({prev, cost});

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
            // critical part of Yen's: without it, Dijkstra just finds the
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

            auto res = dijkstra(snap, spurNode, &blockedVerts, &blockedEdges);
            auto spur = reconstructPath(res, spurNode, dst);
            if (spur.empty())
                continue;

            // Full candidate path = rootPath + spur (excluding duplicate spurNode).
            std::vector<VID> candidate = rootPath;
            candidate.insert(candidate.end(), spur.begin() + 1, spur.end());

            uint64_t candidateCost = 0;
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
                uint32_t bestEdge = 0;
                bool found = false;
                for (auto const& e : snap.adj[u])
                {
                    if (e.to == v)
                    {
                        uint32_t const w =
                            (e.qualityFixed == kNoLiquidity) ? (kNoLiquidity - 1) : e.qualityFixed;
                        if (!found || w < bestEdge)
                        {
                            bestEdge = w;
                            found = true;
                        }
                    }
                }
                if (!found)
                {
                    valid = false;
                    break;
                }
                candidateCost += bestEdge;
            }
            if (valid)
                b.emplace(candidateCost, std::move(candidate));
        }
    }

    return a;
}

//==============================================================================
// PayGraph::findPaths() — convenience wrapper
//==============================================================================

std::vector<PayGraph::AssetPath>
PayGraph::findPaths(Asset const& src, Asset const& dst, int k) const
{
    auto s = snapshot();
    if (!s)
        return {};

    auto itSrc = s->index.find(src);
    auto itDst = s->index.find(dst);
    if (itSrc == s->index.end() || itDst == s->index.end())
        return {};

    return kShortestPaths(*s, itSrc->second, itDst->second, k);
}

}  // namespace xrpl
