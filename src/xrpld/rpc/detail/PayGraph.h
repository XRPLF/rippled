#pragma once

//------------------------------------------------------------------------------
/*
    PayGraph — Persistent, incrementally-updated asset-exchange graph for
               XRPL pathfinding.

    LIFETIME
    --------
    PayGraph is created ONCE at startup (or after a long catchup) and lives
    for the duration of the process.  It is NOT rebuilt per ledger.  Instead,
    applyLedgerDelta() is called at each ledger close — typically updating
    fewer than 100 edges in a few microseconds.

    ASSET-EXCHANGE GRAPH
    --------------------
    Vertices = distinct assets  (IOU {currency,issuer}, MPT IDs, XRP)
    Edges    = order books and AMM pools between asset pairs
    Scale    = ~500 vertices, ~1 000 edges on mainnet -> trivially small

    Trust-line rippling between issuers of the same currency is handled
    implicitly by the XRPL payment engine (rippleCalculate) and does not
    require explicit traversal here.

    WHY THIS HELPS
    --------------
    The current Pathfinder BFS on the combined account+asset graph has
    O(A^D) fanout (A ~ 20 trust-line neighbours, D ~ 7 hops) before any
    pruning, then calls rippleCalculate on every one of up to 1 000
    candidates.

    New algorithm:
      1. Run Yen's K-Shortest on the tiny asset graph   O((V+E) log V)
         => microseconds, returns <=6 abstract asset-type paths
      2. Materialise each path into concrete offer-node STPath objects
      3. Call rippleCalculate only for the top <=6 candidates
      4. Emit first result to WebSocket subscriber immediately

    SNAPSHOT / COPY-ON-WRITE MODEL
    --------------------------------
    Pathfinding threads and the ledger-close thread access the graph
    concurrently.  We use an atomic shared_ptr to an immutable Snapshot:

        Pathfinder thread:
            auto snap = graph.snapshot();
            // Use snap freely -- no locks held.

        Ledger-close thread:
            graph.applyLedgerDelta(newLedger, changedBooks);
            // Copies current snapshot, patches changed edges O(C),
            // atomically publishes new snapshot.  Readers already
            // holding the old snapshot are unaffected.

    Since the entire graph fits in ~50 KB, copying on each ledger close
    is negligible.  Pathfinders hold zero locks during search.

    INCREMENTAL UPDATE
    ------------------
    applyLedgerDelta() receives the set of Book pairs that changed in the
    ledger (derived from transaction metadata -- ltOFFER creates/consumed/
    cancelled and ltAMM changes).  For each changed book it re-queries the
    top-of-book offer in O(1) from the new ledger and updates qualityFixed
    on the corresponding edge.  No SHAMap walk is performed.

    Full rebuild is reserved for startup and long-catchup via rebuild().
*/
//------------------------------------------------------------------------------

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace xrpl {

class PayGraph
{
public:
    //--------------------------------------------------------------------------
    // Public types
    //--------------------------------------------------------------------------

    /// Opaque vertex identifier.  Stable across incremental updates
    /// (vertices are never removed once added; only edge weights change).
    using VID = uint32_t;
    static constexpr VID kNull = ~VID{0};

    /// How value flows across this edge.
    enum class EdgeKind : uint8_t {
        OrderBook,  ///< Best offer in an order book
        AMM,        ///< Constant-product AMM pool
    };

    /// One directed edge in the asset-exchange graph.
    /// 16 bytes -- fits in one cache line.
    struct Edge
    {
        VID to{};                 ///< Receiving-asset vertex
        uint32_t qualityFixed{};  ///< log2(cost_ratio) in 16.16 fixed-point,
                                  ///  stored as int32 bit-pattern (lower signed
                                  ///  value = cheaper).  Path search *adds*
                                  ///  these so multi-hop cost = log2(∏ rates).
                                  ///  kNoLiquidity = structural empty book.
        uint32_t liquidityLog{};  ///< log2(|sum(takerGets)|) as biased 16.16
                                  ///  fixed-point: (log2(amount) + 64) * 65536.
                                  ///  Higher = more depth. 0 = empty/unknown.
        EdgeKind kind{};
    };

    static_assert(sizeof(Edge) == 16, "Edge must be 16 bytes");

    /// Sentinel quality: edge structurally exists but has no current offers.
    /// Dijkstra traverses these at maximum cost so they rank last; structural
    /// presence is preserved because offers may exist in the ledger that
    /// weren't visible when the snapshot was built.
    static constexpr uint32_t kNoLiquidity = 0xFFFF'FFFFu;

    //--------------------------------------------------------------------------
    // Abstract path through the asset-exchange graph.
    //   vids[0]       = source asset
    //   vids[1..n-2]  = bridge assets
    //   vids[n-1]     = destination asset
    //--------------------------------------------------------------------------
    struct AssetPath
    {
        std::vector<VID> vids;
        uint64_t cumQuality{};  ///< Rank of sum of log2 edge weights
                                ///< (lower = better).  Encodes signed path
                                ///< cost via costToRank so multi-hop products
                                ///< compare correctly against direct paths.
    };

    //--------------------------------------------------------------------------
    // Diagnostic counters
    //--------------------------------------------------------------------------
    struct Stats
    {
        uint32_t vertices{};
        uint32_t edges{};
        uint32_t orderBooks{};
        uint32_t ammPools{};
        uint32_t lastDeltaBooks{};     ///< Books patched in the most recent delta
        uint32_t totalDeltasCalled{};  ///< Cumulative applyLedgerDelta() calls
    };

    //--------------------------------------------------------------------------
    // Immutable graph snapshot
    //
    // Pathfinder threads call graph.snapshot() to grab a shared_ptr to the
    // current Snapshot and hold it for the duration of their search.
    // The ledger-close thread atomically publishes a new Snapshot without
    // blocking any in-flight readers.
    //
    // The Snapshot is tiny (~50 KB on mainnet): copying it at ledger-close
    // is cheaper than any fine-grained locking scheme on per-edge data.
    //--------------------------------------------------------------------------
    struct Snapshot
    {
        /// Adjacency list indexed by VID.  adj[v] = outgoing edges from v.
        std::vector<std::vector<Edge>> adj;

        /// Vertex -> Asset mapping (adj.size() == assets.size()).
        std::vector<Asset> assets;

        /// Asset -> VID for O(1) lookup.
        hash_map<Asset, VID> index;

        /// Johnson potentials h[v] so reweighted edge costs
        ///   w'(u,v) = log2(rate) + h[u] - h[v]
        /// are non-negative.  Computed once per snapshot (build/delta), not
        /// per pathfind, so Dijkstra stays O((V+E) log V) at query time.
        std::vector<std::int64_t> potential;

        Stats stats;
    };

    //--------------------------------------------------------------------------
    // Lifecycle
    //--------------------------------------------------------------------------

    /// Full build from scratch.  Called once at startup (or post-catchup).
    /// Scans all order books known to bookDB and queries the ledger for AMM
    /// pools.  Complexity: O(B) where B = number of active books/pools.
    static std::shared_ptr<PayGraph>
    build(
        OrderBookDB& bookDB,
        ReadView const& ledger,
        std::optional<uint256> const& domain,
        beast::Journal j);

    PayGraph(PayGraph const&) = delete;
    PayGraph&
    operator=(PayGraph const&) = delete;
    ~PayGraph() = default;

    //--------------------------------------------------------------------------
    // Incremental update -- call at each ledger close.
    //
    // changedBooks: Book pairs that had at least one offer created, consumed,
    //   or cancelled in the just-closed ledger.  Callers derive this from
    //   AcceptedLedger transaction metadata: scan ltOFFER node changes and
    //   extract (takerPays.asset, takerGets.asset).  Typically < 100 / ledger.
    //
    // For each changed book:
    //   * If offers remain: re-query top offer -> update qualityFixed.
    //   * If no offers remain: set qualityFixed = kNoLiquidity.
    //   * If the book / vertex did not exist yet: add it.
    //
    // A copy of the current Snapshot is made, patched, then atomically stored.
    // Pathfinders holding the old Snapshot see a consistent graph throughout.
    //
    // Complexity: O(V + E) copy  +  O(C) book lookups
    //   where V, E ~ 1 000 and C ~ 100 -> well under 1 ms.
    //--------------------------------------------------------------------------
    void
    applyLedgerDelta(
        OrderBookDB& bookDB,
        ReadView const& newLedger,
        std::vector<Book> const& changedBooks);

    /// Full rebuild.  Safe to call at any time; replaces the snapshot
    /// atomically like applyLedgerDelta() does.  Prefer the delta path
    /// for normal operation.
    void
    rebuild(OrderBookDB& bookDB, ReadView const& ledger, std::optional<uint256> const& domain);

    //--------------------------------------------------------------------------
    // Snapshot access for pathfinding threads
    //
    // Grab ONCE at the start of a pathfinding request and keep it for the
    // duration.  The shared_ptr keeps the snapshot alive even if a new one
    // is published mid-search.
    //--------------------------------------------------------------------------
    std::shared_ptr<Snapshot const>
    snapshot() const;

    //--------------------------------------------------------------------------
    // Vertex helpers (operate on current snapshot)
    //--------------------------------------------------------------------------

    VID
    vertexOf(Asset const& asset) const;

    Asset const&
    assetOf(VID v) const;

    //--------------------------------------------------------------------------
    // K-Shortest asset paths  (Yen's algorithm over Dijkstra)
    //
    // snap       -- snapshot obtained from snapshot() at the start of the request
    // src/dst    -- vertex IDs of source and destination assets
    // k          -- maximum number of paths to return
    // dstAmount  -- destination payment size for liquidity-aware ranking.
    //               When non-zero, edges whose book depth cannot cover this
    //               amount are penalized so thin top-of-book paths do not
    //               consume scarce k-shortest candidate slots.  beast::kZero
    //               keeps pure top-of-book ranking (legacy behavior).
    //
    // Returns up to k paths ordered by ascending cumQuality (best first).
    // Returns {} if no path exists between src and dst.
    //
    // Complexity: O(k * (V + E) log V) -- typically < 1 ms for k = 6.
    //--------------------------------------------------------------------------
    static std::vector<AssetPath>
    kShortestPaths(
        Snapshot const& snap,
        VID src,
        VID dst,
        int k,
        STAmount const& dstAmount = beast::kZero);

    /// Convenience: grab current snapshot and run kShortestPaths.
    std::vector<AssetPath>
    findPaths(Asset const& src, Asset const& dst, int k, STAmount const& dstAmount = beast::kZero)
        const;

    //--------------------------------------------------------------------------
    Stats
    currentStats() const;

private:
    explicit PayGraph(std::optional<uint256> const& domain, beast::Journal j);

    //--------------------------------------------------------------------------
    // Build / patch helpers
    //--------------------------------------------------------------------------

    /// Allocate a Snapshot populated from bookDB + ledger (no atomic store).
    static std::shared_ptr<Snapshot>
    buildSnapshot(
        OrderBookDB& bookDB,
        ReadView const& ledger,
        std::optional<uint256> const& domain,
        beast::Journal j);

    /// Ensure a vertex for 'asset' exists in snap.  Returns its VID.
    static VID
    ensureVertex(Snapshot& snap, Asset const& asset);

    /// Find or create the directed edge (from -> to) of the given kind in snap.
    /// Returns a reference into snap.adj so the caller can set qualityFixed.
    static Edge&
    ensureEdge(Snapshot& snap, VID from, VID to, EdgeKind kind);

    /// Query top-of-book quality from the ledger for a given order book.
    /// Returns kNoLiquidity when no offers remain in the book.
    static uint32_t
    topOfBookQuality(ReadView const& ledger, Book const& book);

    /// Full-book depth as biased log2 fixed-point (see Edge::liquidityLog).
    /// Sums takerGets across all offers so thin top-of-book rates can still
    /// be scored against total available liquidity.
    static uint32_t
    bookLiquidityLog(ReadView const& ledger, Book const& book);

    //--------------------------------------------------------------------------
    // Shortest-path internals (Dijkstra over Johnson-reweighted log-weights)
    //--------------------------------------------------------------------------
    struct DijkResult
    {
        std::vector<std::int64_t> dist;  ///< min reweighted distance from src
        std::vector<VID> prev;           ///< predecessor (kNull = none)
    };

    /// A set of directed edges (from, to) to treat as absent during search.
    using BlockedEdges = std::vector<std::pair<VID, VID>>;

    /// Recompute Snapshot::potential after edges change (build / ledger delta).
    static void
    computePotentials(Snapshot& snap);

    /// Binary-heap Dijkstra on non-negative reweighted log-costs.
    static DijkResult
    dijkstra(
        Snapshot const& snap,
        VID src,
        std::vector<bool> const* blockedVerts = nullptr,
        BlockedEdges const* blockedEdges = nullptr,
        STAmount const* dstAmount = nullptr);

    static std::vector<VID>
    reconstructPath(DijkResult const& res, VID src, VID dst);

    //--------------------------------------------------------------------------
    // State
    //--------------------------------------------------------------------------

    /// Current snapshot.  Read via snapshot(); written only by writeMu_ holder.
    /// Uses a plain shared_ptr + std::atomic_{load,store}_explicit with
    /// explicit memory orders.  C++20's std::atomic<shared_ptr<T>> would be
    /// preferable but Apple libc++ has not yet implemented the specialisation,
    /// so the deprecated free-function API is wrapped with a localised
    /// diagnostic-suppression pragma in PayGraph.cpp.
    mutable std::shared_ptr<Snapshot const> snap_;

    /// Serialises applyLedgerDelta() / rebuild() calls.
    /// Never held during pathfinding.
    std::mutex writeMu_;

    std::optional<uint256> domain_;
    beast::Journal j_;
};

}  // namespace xrpl
