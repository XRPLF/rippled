#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PayGraph.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/STPathSet.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

class PathRequestManager
{
public:
    /**
     * A collection of all PathRequest instances.
     */
    PathRequestManager(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app), journal_(journal), lastIdentifier_(0)
    {
        fast_ = collector->makeEvent("pathfind_fast");
        full_ = collector->makeEvent("pathfind_full");
    }

    /**
     * Update all of the contained PathRequest instances.
     *
     * @param ledger Ledger we are pathfinding in.
     */
    void
    updateAll(std::shared_ptr<ReadView const> const& ledger);

    bool
    requestsPending() const;

    std::shared_ptr<AssetCache>
    getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative);

    // Create a new-style path request that pushes
    // updates to a subscriber
    json::Value
    makePathRequest(
        std::shared_ptr<InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        json::Value const& request);

    // Create an old-style path request that is
    // managed by a coroutine and updated by
    // the path engine
    json::Value
    makeLegacyPathRequest(
        PathRequest::pointer& req,
        std::function<void(void)> completion,
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    // Execute an old-style path request immediately
    // with the ledger specified by the caller
    json::Value
    doLegacyPathRequest(
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    void
    reportFast(std::chrono::milliseconds ms)
    {
        fast_.notify(ms);
    }

    void
    reportFull(std::chrono::milliseconds ms)
    {
        full_.notify(ms);
    }

    std::shared_ptr<PayGraph>
    getPayGraph() const
    {
        std::scoped_lock const sl(lock_);
        return payGraph_;
    }

    //----------------------------------------------------------------------
    // Failed AMM book hops (in→out assets).  When rippleCalculate throws
    // FlowException from an AMM offer on a hop, remember that hop so later
    // path_find ranking skips it instead of re-paying Throw/logThrow cost.
    // Cleared on full graph rebuild (signalOrderBookReady / empty rebuild).
    //----------------------------------------------------------------------
    using AmmHop = std::pair<Asset, Asset>;

    void
    noteFailedAmmHop(Asset const& in, Asset const& out)
    {
        std::scoped_lock const sl(failedAmmLock_);
        failedAmmHops_.insert(AmmHop{in, out});
    }

    [[nodiscard]] bool
    isFailedAmmHop(Asset const& in, Asset const& out) const
    {
        std::scoped_lock const sl(failedAmmLock_);
        return failedAmmHops_.contains(AmmHop{in, out});
    }

    void
    clearFailedAmmHops()
    {
        std::scoped_lock const sl(failedAmmLock_);
        failedAmmHops_.clear();
    }

    /// Return the warm in-memory PayGraph for Dijkstra path_find.
    ///
    /// Full rebuild only when:
    ///   - no graph yet, or
    ///   - graph has zero books (built before OrderBookDB finished scanning)
    /// Otherwise return the existing snapshot.  Per-ledger edge updates are
    /// applyLedgerDelta from updateAll (~few changed books), not a full rebuild.
    std::shared_ptr<PayGraph>
    ensurePayGraph(std::shared_ptr<ReadView const> const& inLedger)
    {
        std::scoped_lock const sl(lock_);
        if (!inLedger || !app_.config().pathSearch)
            return payGraph_;

        bool const empty = !payGraph_ || payGraph_->currentStats().orderBooks == 0;
        if (empty)
        {
            payGraph_ = PayGraph::build(app_.getOrderBookDB(), *inLedger, std::nullopt, journal_);
            graphLedgerSeq_ = inLedger->seq();
            clearFailedAmmHops();
        }
        return payGraph_;
    }

    /// OrderBookDB finished a full scan and swapped allBooks_ in (rare — not
    /// every ~3s ledger).  Mark ready and rebuild once from the scanned set so
    /// path_find is not stuck on an empty graph forever.
    void
    signalOrderBookReady(std::shared_ptr<ReadView const> const& ledger)
    {
        orderBookReady_.store(true, std::memory_order_release);
        if (!ledger || !app_.config().pathSearch)
            return;

        std::scoped_lock const sl(lock_);
        payGraph_ = PayGraph::build(app_.getOrderBookDB(), *ledger, std::nullopt, journal_);
        graphLedgerSeq_ = ledger->seq();
        clearFailedAmmHops();
    }

    /// One-shot synchronous helper used by tx-signing autofill (build_path)
    /// and jtx test helpers.  Builds/borrows the PayGraph for `ledger`,
    /// constructs a GraphPathfinder and returns up to `maxPaths` STPaths
    /// from src→dst delivering `dstAmount`.  Returns an empty STPathSet
    /// when no paths are found (or no PayGraph is available).
    STPathSet
    findPaths(
        std::shared_ptr<ReadView const> const& ledger,
        AccountID const& srcAccount,
        AccountID const& dstAccount,
        STAmount const& dstAmount,
        PathAsset const& srcAsset,
        std::optional<AccountID> const& srcIssuer,
        std::optional<uint256> const& domain,
        int maxPaths);

private:
    void
    insertPathRequest(PathRequest::pointer const&);

    Application& app_;
    beast::Journal journal_;

    beast::insight::Event fast_;
    beast::insight::Event full_;

    // Track all requests
    std::vector<PathRequest::wptr> requests_;

    // Use a AssetCache
    std::weak_ptr<AssetCache> assetCache_;

    // Persistent asset-exchange graph.  Built once at startup (after
    // orderBookReady_ is set); mutated incrementally by applyLedgerDelta()
    // at each subsequent ledger close.
    std::shared_ptr<PayGraph> payGraph_;

    // Ledger sequence of the last PayGraph build or incremental delta.
    // Diagnostic / freshness only — ensurePayGraph must NOT full-rebuild
    // solely because this lags the request ledger.
    LedgerIndex graphLedgerSeq_{0};

    // Set by signalOrderBookReady() when OrderBookDB finishes its first full
    // ledger scan.  Prevents PayGraph::build() from running against an empty
    // allBooks_ on networked nodes where the scan is async (the race that
    // causes the PayGraph to have no edges until restart).
    std::atomic<bool> orderBookReady_{false};

    // AMM hops that recently threw FlowException during ranking probes.
    // Separate lock so path_find ranking does not contend with request list.
    hash_set<AmmHop> failedAmmHops_;
    std::mutex mutable failedAmmLock_;

    std::atomic<int> lastIdentifier_;

    std::recursive_mutex mutable lock_;
};

}  // namespace xrpl
