#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>

#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl {

class PathRequestManager
{
public:
    PathRequestManager(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector);

    /**
     * Detach mid-close handlers, wait for any in-flight timer/JtRpc work that
     * already holds a manager pointer, then cancel the timer. io_context
     * threads may outlive this object; MidCloseBag keeps them from using a
     * dangling PathRequestManager without holding bag->mutex across refresh.
     */
    ~PathRequestManager();

    /**
     * @param midClose When true and ledger is open, also revalidate established
     *        sessions (not just brand-new creates). Used for sub-close-interval
     *        updates; does not pin lastIndex_ for steady sessions so the next
     *        closed ledger still refreshes everyone.
     */
    void
    updateAll(std::shared_ptr<ReadView const> const& ledger, bool midClose = false);

    bool
    requestsPending() const;

    /**
     * Arm the periodic revalidate timer (Config::pathMidCloseDelay). Safe to
     * call repeatedly; only one timer is in flight.
     */
    void
    scheduleMidCloseRefresh();

    std::shared_ptr<AssetCache>
    getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative);

    json::Value
    makePathRequest(
        std::shared_ptr<InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        json::Value const& request);

    json::Value
    makeLegacyPathRequest(
        PathRequest::pointer& req,
        std::function<void(void)> completion,
        resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    json::Value
    doLegacyPathRequest(
        resource::Consumer& consumer,
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

    /**
     * Snapshot of AssetCache counters for get_counts / monitoring.
     */
    struct CacheStats
    {
        bool available = false;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t linesLoaded = 0;
        std::uint64_t ledgerAdvances = 0;
        std::size_t totalLines = 0;
    };

    [[nodiscard]] CacheStats
    getCacheStats() const;

    /**
     * Drop a finished/closed path_find session. When the last live session is
     * gone, releases AssetCache so trust-line memory and get_counts cache
     * counters reclaim (pathfind_cache_lines / pathfind_lines_loaded → 0).
     */
    void
    removePathRequest(PathRequest* request);

private:
    void
    insertPathRequest(PathRequest::pointer const&);

    /**
     * Publish AssetCache counter deltas to insight collectors.
     * Takes lock_ (recursive) — serializes lastCache* baselines with close paths.
     */
    void
    publishCacheStats(AssetCache const& cache);

    /**
     * Caller holds lock_. Rebuild requests_ without expired weaks, and drop
     * @a request if non-null. Strong refs are held until after the vector is
     * replaced so ~PathRequest → removePathRequest cannot re-enter mid-erase
     * (recursive_mutex would allow that and invalidate iterators).
     *
     * @return number of entries removed (expired + matched request).
     */
    std::size_t
    rebuildRequestsUnlocked(PathRequest* request = nullptr);

    /**
     * Caller holds lock_. Erase expired weak_ptrs; if no live sessions remain,
     * destroy assetCache_ (reclaims PathFindTrustLine memory).
     */
    void
    releaseCacheIfIdleUnlocked();

    [[nodiscard]] bool
    hasLiveRequestsUnlocked() const;

    /**
     * Open-ledger revalidate-only wave for all established sessions. Runs on
     * JtRpc (not JtUpdatePf) so it never waits behind closed-ledger Pathfinder.
     */
    void
    runPeriodicRevalidate();

    /**
     * Timer completion body. Invoked only while MidCloseBag holds a lock and
     * manager is non-null (or via a JobQueue job that re-checks the bag).
     */
    void
    onMidCloseTimer(boost::system::error_code const& waitEc);

    Application& app_;
    beast::Journal journal_;

    beast::insight::Event fast_;
    beast::insight::Event full_;
    beast::insight::Counter cacheHits_;
    beast::insight::Counter cacheMisses_;
    beast::insight::Counter linesLoaded_;
    beast::insight::Counter cacheLedgerAdvances_;

    std::uint64_t lastCacheHits_{0};
    std::uint64_t lastCacheMisses_{0};
    std::uint64_t lastLinesLoaded_{0};
    std::uint64_t lastLedgerAdvances_{0};

    std::vector<PathRequest::wptr> requests_;

    // Strong while any path_find session is live; released when idle so
    // trust-line vectors are not pinned forever after WS disconnect.
    std::shared_ptr<AssetCache> assetCache_;

    /**
     * Lifetime token for mid-close async_wait / JtRpc jobs. Handlers capture
     * shared_ptr<MidCloseBag>. bag->mutex is only held to publish/check manager
     * and to enter/leave inFlight — never across runPeriodicRevalidate so
     * network io_context threads are not stalled on a long refresh.
     *
     * Protocol: under mutex, if manager is non-null, ++inFlight and copy the
     * pointer, unlock, use the pointer, then --inFlight and notify. Destructor
     * nulls manager then waits until inFlight == 0 so no handler uses a
     * destroyed PathRequestManager (io threads outlive this object).
     */
    struct MidCloseBag
    {
        std::mutex mutex;
        std::condition_variable idle;
        PathRequestManager* manager{nullptr};
        int inFlight{0};
    };
    std::shared_ptr<MidCloseBag> midCloseBag_;

    boost::asio::steady_timer midCloseTimer_;
    std::atomic<bool> midCloseScheduled_{false};
    // True while a JtRpc periodic revalidate job is queued or running.
    // Cleared only after runPeriodicRevalidate returns (not before).
    std::atomic<bool> revalidateJobPending_{false};

    // Serializes closed/create updateAll vs mid-close. Mid-close uses try_lock
    // so it never blocks behind a long closed wave (skips the tick instead).
    // Closed/create uses lock() and can occupy a JobQueue worker while waiting;
    // runParallel therefore only fans out when workers >= 3 and caps each batch
    // at workers - 1 so that waiter cannot starve JtPathFindWork siblings.
    std::mutex waveMutex_;

    std::atomic<int> lastIdentifier_;

    std::recursive_mutex mutable lock_;
};

}  // namespace xrpl
