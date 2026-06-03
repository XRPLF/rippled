#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PayGraph.h>
#include <xrpld/rpc/detail/RippleLineCache.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl {

class PathRequestManager
{
public:
    /** A collection of all PathRequest instances. */
    PathRequestManager(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app), journal_(journal), lastIdentifier_(0)
    {
        fast_ = collector->makeEvent("pathfind_fast");
        full_ = collector->makeEvent("pathfind_full");
    }

    /** Update all of the contained PathRequest instances.

        @param ledger Ledger we are pathfinding in.
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

    /// Build or rebuild the (non-domain) PayGraph for the given ledger if it
    /// has not yet been built or is stale.  Cheap: PayGraph::build() reads
    /// only OrderBookDB's in-memory maps (no SHAMap walk).  Used by the
    /// synchronous entry points (ripple_path_find, path_find create) so the
    /// first response is never empty just because the async updateAll() job
    /// hasn't run yet.
    std::shared_ptr<PayGraph>
    ensurePayGraph(std::shared_ptr<ReadView const> const& inLedger)
    {
        std::scoped_lock const sl(lock_);
        auto const seq = inLedger->seq();
        if (!payGraph_ || legacyGraphSeq_ != seq)
        {
            payGraph_ = PayGraph::build(app_.getOrderBookDB(), *inLedger, std::nullopt, journal_);
            legacyGraphSeq_ = seq;
        }
        return payGraph_;
    }

    /// Called by LedgerMaster after OrderBookDB completes its first full scan.
    /// Gates the initial PayGraph build so it never runs against an empty
    /// allBooks_ (the race that occurs on networked nodes where the scan is
    /// async).  Safe to call multiple times — only the first call matters.
    void
    signalOrderBookReady()
    {
        orderBookReady_.store(true, std::memory_order_release);
    }

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

    // Ledger sequence at which the sync (doLegacyPathRequest) graph was last
    // built.  When a new ledger arrives we rebuild from the updated
    // OrderBookDB (in-memory only, no SHAMap walk) so tests and one-shot
    // ripple_path_find calls always see current books.
    LedgerIndex legacyGraphSeq_{0};

    // Set by signalOrderBookReady() when OrderBookDB finishes its first full
    // ledger scan.  Prevents PayGraph::build() from running against an empty
    // allBooks_ on networked nodes where the scan is async (the race that
    // causes the PayGraph to have no edges until restart).
    std::atomic<bool> orderBookReady_{false};

    std::atomic<int> lastIdentifier_;

    std::recursive_mutex mutable lock_;
};

}  // namespace xrpl
