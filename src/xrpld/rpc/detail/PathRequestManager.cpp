#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/shamap/SHAMapMissingNode.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

static_assert(
    rpc::tuning::kPathSteadyUpdateParallelism == kPathFindWorkLimit,
    "JtPathFindWork JobTypes limit must match path_find steady parallelism");

void
PathRequestManager::publishCacheStats(AssetCache const& cache)
{
    auto const hits = cache.cacheHits();
    auto const misses = cache.cacheMisses();
    auto const loaded = cache.linesLoaded();
    auto const advances = cache.ledgerAdvances();

    if (hits > lastCacheHits_)
        cacheHits_ += static_cast<beast::insight::Counter::value_type>(hits - lastCacheHits_);
    if (misses > lastCacheMisses_)
        cacheMisses_ += static_cast<beast::insight::Counter::value_type>(misses - lastCacheMisses_);
    if (loaded > lastLinesLoaded_)
        linesLoaded_ += static_cast<beast::insight::Counter::value_type>(loaded - lastLinesLoaded_);
    if (advances > lastLedgerAdvances_)
        cacheLedgerAdvances_ +=
            static_cast<beast::insight::Counter::value_type>(advances - lastLedgerAdvances_);

    lastCacheHits_ = hits;
    lastCacheMisses_ = misses;
    lastLinesLoaded_ = loaded;
    lastLedgerAdvances_ = advances;

    JLOG(journal_.debug()) << "AssetCache stats lines=" << cache.totalLineCount()
                           << " hits=" << hits << " misses=" << misses << " loaded=" << loaded
                           << " advances=" << advances;
}

std::shared_ptr<AssetCache>
PathRequestManager::getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative)
{
    std::scoped_lock const sl(lock_);

    std::uint32_t const lineSeq = assetCache_ ? assetCache_->getLedger()->seq() : 0;
    std::uint32_t const lgrSeq = ledger->seq();
    JLOG(journal_.debug()) << "getAssetCache has cache for " << lineSeq << ", considering "
                           << lgrSeq << " authoritative=" << authoritative;

    if (!assetCache_)
    {
        JLOG(journal_.debug()) << "getAssetCache creating new cache for " << lgrSeq;
        auto const& cfg = app_.config();
        assetCache_ = std::make_shared<AssetCache>(
            ledger,
            app_.getJournal("AssetCache"),
            cfg.pathFindMaxTotalLines,
            cfg.pathFindMaxLinesPerAccount,
            cfg.pathCacheReuseLedgers,
            cfg.pathFindLineChunkSize);
        return assetCache_;
    }

    if (lineSeq == lgrSeq)
    {
        // Same sequence: still prefer an authoritative closed view over a prior
        // open mid-close view (open and closed share the upcoming seq).
        if (authoritative && !ledger->open())
        {
            auto const cur = assetCache_->getLedger();
            if (cur && cur->open())
            {
                JLOG(journal_.debug())
                    << "getAssetCache replacing open view with closed at seq " << lgrSeq;
                assetCache_->advanceLedger(ledger, /*forceClear=*/false);
            }
        }
        return assetCache_;
    }

    // Large jumps always force a rebuild so the soft-reuse window cannot
    // straddle a huge gap. Matches historical getLineCache policy.
    bool const largeJumpForward = lgrSeq > (lineSeq + 8);
    bool const largeJumpBack = (lgrSeq + 8) < lineSeq;
    if (largeJumpForward || (authoritative && largeJumpBack))
    {
        JLOG(journal_.info()) << "getAssetCache large ledger jump " << lineSeq << " -> " << lgrSeq
                              << "; force rebuild";
        assetCache_->advanceLedger(ledger, /*forceClear=*/true);
        return assetCache_;
    }

    // Only authoritative (validated/closed) ledgers soft-advance the shared
    // cache across sequence changes. Open mid-close may still advance when
    // called with authoritative=true from updateAll so revalidate sees the
    // open view; create/legacy use authoritative=false and never mutate.
    if (authoritative && lgrSeq > lineSeq)
    {
        assetCache_->advanceLedger(ledger, /*forceClear=*/false);
        return assetCache_;
    }

    return assetCache_;
}

namespace {

/**
 * Run steady revalidates in bounded parallel batches via JobQueue.
 *
 * Matches project convention (no std::async): each unit is JtPathFindWork so
 * concurrency is visible to job accounting and capped by JobTypes limit (32).
 *
 * Fork-join from a JobQueue thread: queue count-1 workers, run one unit on
 * this thread, then wait. That always makes progress even if the pool is
 * saturated (avoids "all workers waiting for more workers" deadlock).
 *
 * Completes the full work vector — do not abort mid-wave for new path_find
 * sessions (that stretched mean update gap under load).
 */
void
runParallel(
    JobQueue& jobQueue,
    std::vector<PathRequest::pointer> const& work,
    int parallelism,
    std::function<bool(PathRequest::pointer const&, bool, bool)> const& runOne,
    bool pinIndex,
    bool revalidateOnly,
    std::function<void(PathRequest::pointer const&)> const& onDrop,
    int& processed)
{
    if (work.empty())
        return;

    auto const par = static_cast<std::size_t>(std::max(1, parallelism));

    for (std::size_t batch = 0; batch < work.size(); batch += par)
    {
        if (jobQueue.isStopping())
            break;

        auto const end = std::min(batch + par, work.size());
        auto const count = end - batch;

        if (count == 1)
        {
            bool const keep = runOne(work[batch], pinIndex, revalidateOnly);
            ++processed;
            if (!keep)
                onDrop(work[batch]);
            continue;
        }

        // results[i] = keep for work[batch + i]
        auto results = std::make_shared<std::vector<char>>(count, 0);
        auto remaining = std::make_shared<std::atomic<std::size_t>>(count);
        std::mutex doneMutex;
        std::condition_variable doneCv;

        auto finishOne = [&doneMutex, &doneCv, remaining]() {
            if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard const lk(doneMutex);
                doneCv.notify_one();
            }
        };

        auto runUnit =
            [&runOne, pinIndex, revalidateOnly](PathRequest::pointer const& req) -> bool {
            try
            {
                return runOne(req, pinIndex, revalidateOnly);
            }
            catch (...)
            {
                // Never leave the batch barrier hanging; ClaimGuard in runOne
                // still clears inProgress_ on the request.
                return false;
            }
        };

        // Queue all but the last unit on JobQueue (JtPathFindWork).
        for (std::size_t b = batch; b + 1 < end; ++b)
        {
            auto const idx = b - batch;
            auto req = work[b];
            bool const queued = jobQueue.addJob(
                JtPathFindWork, "PthFindSteady", [runUnit, req, results, idx, finishOne]() {
                    (*results)[idx] = runUnit(req) ? 1 : 0;
                    finishOne();
                });
            if (!queued)
            {
                // Shutdown / queue full: run inline so the barrier still completes.
                (*results)[idx] = runUnit(req) ? 1 : 0;
                finishOne();
            }
        }

        // Last unit always runs on this thread (fork-join progress guarantee).
        {
            auto const idx = count - 1;
            auto const& req = work[batch + idx];
            (*results)[idx] = runUnit(req) ? 1 : 0;
            finishOne();
        }

        {
            std::unique_lock lk(doneMutex);
            doneCv.wait(lk, [&] { return remaining->load(std::memory_order_acquire) == 0; });
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            ++processed;
            if (!(*results)[i])
                onDrop(work[batch + i]);
        }
    }
}

}  // namespace

void
PathRequestManager::scheduleMidCloseRefresh()
{
    // Only one mid-close timer in flight; re-armed after each tick while live.
    if (midCloseScheduled_.exchange(true, std::memory_order_acq_rel))
        return;

    midCloseTimer_.expires_after(app_.config().pathMidCloseDelay);
    midCloseTimer_.async_wait([this](boost::system::error_code const& waitEc) {
        midCloseScheduled_.store(false, std::memory_order_release);
        if (waitEc || app_.isStopping() || !requestsPending())
            return;

        // Non-blocking: dispatch revalidate on JtRpc so it never waits behind
        // JtUpdatePf (limit 1) closed-ledger / first-update waves. Skip if a
        // prior tick is still queued or running (wave overran the period).
        if (!revalidateJobPending_.exchange(true, std::memory_order_acq_rel))
        {
            bool const queued = app_.getJobQueue().addJob(JtRpc, "PthFindReval", [this]() {
                try
                {
                    runPeriodicRevalidate();
                }
                catch (std::exception const& ex)
                {
                    JLOG(journal_.info()) << "periodic path revalidate exception: " << ex.what();
                }
                // Clear only after the wave finishes so ticks cannot overlap.
                revalidateJobPending_.store(false, std::memory_order_release);
            });
            if (!queued)
            {
                revalidateJobPending_.store(false, std::memory_order_release);
                JLOG(journal_.debug()) << "periodic path revalidate job not queued";
            }
            else
            {
                JLOG(journal_.debug()) << "periodic path revalidate job queued";
            }
        }

        // Keep ticking every pathMidCloseDelay while sessions remain.
        if (requestsPending() && !app_.isStopping())
            scheduleMidCloseRefresh();
    });
}

void
PathRequestManager::runPeriodicRevalidate()
{
    if (app_.isStopping() || !requestsPending())
        return;

    // Open ledger: fresh offers/balances for rippleCalculate without waiting
    // for the next validated close. Does not go through LedgerMaster::updatePaths.
    auto const ledger = app_.getOpenLedger().current();
    if (!ledger)
        return;

    try
    {
        JLOG(journal_.debug()) << "runPeriodicRevalidate open seq=" << ledger->seq();
        updateAll(ledger, /*midClose=*/true);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // Mirror LedgerMaster::updatePaths: missing nodes are best-effort;
        // do not leave sessions stuck. Next tick will retry.
        JLOG(journal_.info()) << "During periodic path revalidate: " << mn.what();
        if (ledger->open())
        {
            app_.getInboundLedgers().acquire(
                ledger->header().parentHash,
                ledger->header().seq - 1,
                InboundLedger::Reason::GENERIC);
        }
        else
        {
            app_.getInboundLedgers().acquire(
                ledger->header().hash, ledger->header().seq, InboundLedger::Reason::GENERIC);
        }
    }
}

void
PathRequestManager::updateAll(std::shared_ptr<ReadView const> const& inLedger, bool midClose)
{
    auto event = app_.getJobQueue().makeLoadEvent(JtPathFind, "PathRequest::updateAll");

    // Mid-close must not block behind a closed wave (that re-created the
    // ledger-bound gap). Skip the tick if a closed/create wave holds the lock.
    std::unique_lock waveLock(waveMutex_, std::defer_lock);
    if (midClose)
    {
        if (!waveLock.try_lock())
        {
            JLOG(journal_.debug()) << "mid-close skipped: closed/create wave in progress";
            return;
        }
    }
    else
    {
        waveLock.lock();
    }

    std::vector<PathRequest::wptr> requests;
    std::shared_ptr<AssetCache> cache;

    {
        std::scoped_lock const sl(lock_);
        requests = requests_;
        // Closed / create: authoritative advance. Mid-close: do not advance the
        // shared cache ledger_ to open (avoids races with closed waves); calc
        // ledger is passed separately into doUpdate for PaymentSandbox.
        bool const authoritative = !(midClose && inLedger->open());
        cache = getAssetCache(inLedger, authoritative);
    }

    // Pure mid-close / periodic: never consume pathFindNewRequest_ (that would
    // steal creates from LedgerMaster::updatePaths). Closed / create wakes do.
    bool newRequests = false;
    if (!midClose)
        newRequests = app_.getLedgerMaster().isNewPathRequest();

    bool mustBreak = false;
    // Validated/closed ledgers: refresh every session once per seq.
    // Open mid-close (periodic JtRpc tick): revalidate established sessions.
    // Open create wake (via updatePaths): brand-new path_find only.
    bool const closedLedger = !inLedger->open();
    bool const processSteadyOnOpen = midClose && !closedLedger;

    // Snapshot once per wave — avoid repeated getLedger() shared_lock on the
    // hot partition/claim path under parallel steady updates.
    auto const ledgerSeq = cache->getLedger()->seq();
    // Open mid-close: use the open view for pricing (issue #3).
    std::shared_ptr<ReadView const> const calcLedger =
        processSteadyOnOpen ? inLedger : std::shared_ptr<ReadView const>{};

    JLOG(journal_.trace()) << "updateAll seq=" << ledgerSeq << ", " << requests.size()
                           << " requests steadyParallel="
                           << rpc::tuning::kPathSteadyUpdateParallelism
                           << " closed=" << closedLedger << " midClose=" << processSteadyOnOpen;

    int processed = 0;
    int removed = 0;

    auto getSubscriber = [](PathRequest::pointer const& request) -> InfoSub::pointer {
        if (auto ipSub = request->getSubscriber(); ipSub && ipSub->getRequest() == request)
            return ipSub;
        request->doAborting();
        return nullptr;
    };

    // Returns true if request should be kept.
    // pinIndex: pin lastIndex_ only for closed ledgers (open first-update must
    // not skip the same-seq closed wave). Mid-close never pins.
    // revalidateOnly: mid-close only — never Pathfinder.
    auto runOne =
        [&](PathRequest::pointer const& request, bool pinIndex, bool revalidateOnly) -> bool {
        if (!request)
            return false;

        // Always clear inProgress_ even if doUpdate throws.
        struct ClaimGuard
        {
            PathRequest& req;
            bool active{true};
            bool completed{false};
            std::optional<LedgerIndex> pin;
            ~ClaimGuard()
            {
                if (active)
                    req.updateComplete(pin, completed);
            }
            void
            pinTo(LedgerIndex seq)
            {
                pin = seq;
                completed = true;
            }
            void
            markCompleted()
            {
                completed = true;
            }
        } guard{.req = *request, .active = true, .completed = false, .pin = std::nullopt};

        try
        {
            auto continueCallback = [&getSubscriber, &request]() {
                return static_cast<bool>(getSubscriber(request));
            };

            if (auto ipSub = getSubscriber(request))
            {
                if (ipSub->getConsumer().warn())
                {
                    // Dropped for resource pressure — do not mark completed.
                    return false;
                }

                ipSub.reset();
                json::Value update =
                    request->doUpdate(cache, false, continueCallback, revalidateOnly, calcLedger);
                if (pinIndex)
                    guard.pinTo(ledgerSeq);
                else
                    guard.markCompleted();
                update[jss::type] = "path_find";
                ipSub = getSubscriber(request);
                if (ipSub)
                {
                    ipSub->send(update, false);
                    return true;
                }
                return false;
            }

            if (request->hasCompletion())
            {
                request->doUpdate(cache, false, {}, revalidateOnly, calcLedger);
                if (pinIndex)
                    guard.pinTo(ledgerSeq);
                else
                    guard.markCompleted();
                return false;
            }

            return false;
        }
        catch (...)
        {
            // ClaimGuard clears inProgress_. Propagate after logging.
            JLOG(journal_.info()) << "path request update threw";
            throw;
        }
    };

    auto dropRequest = [&](PathRequest::pointer const& request) {
        std::scoped_lock const sl(lock_);
        auto ret = std::ranges::remove_if(requests_, [&](auto const& wl) {
            auto r = wl.lock();
            if (!r)
            {
                ++removed;
                return true;
            }
            if (request && r == request)
            {
                ++removed;
                return true;
            }
            return false;
        });
        requests_.erase(ret.begin(), ret.end());

        // Always release session pins (resource-pressure / exception drops used
        // to skip this and strand PathFindTrustLine vectors under budget).
        if (assetCache_ && request)
        {
            auto const freed = assetCache_->releaseSession(request->id());
            if (freed > 0)
                publishCacheStats(*assetCache_);
        }

        // Last session gone (or only dead weaks) — free trust-line memory now.
        releaseCacheIfIdleUnlocked();
    };

    do
    {
        JLOG(journal_.trace()) << "updateAll looping";

        std::vector<PathRequest::pointer> firstUpdates;
        std::vector<PathRequest::pointer> steadyUpdates;
        firstUpdates.reserve(requests.size());
        steadyUpdates.reserve(requests.size());

        // Partition before running work. Capture isNew() before needsUpdate
        // (needsUpdate only sets inProgress_; lastIndex_ flips after complete).
        //
        // Open ledger without midClose: only brand-new sessions (newOnly) so
        // ramp stays O(n). Mid-close / closed: include steady revalidates.
        bool const newOnly = newRequests && !closedLedger && !processSteadyOnOpen;

        // Periodic mid-close must re-claim every tick even when lastIndex_
        // already equals the open ledger seq (same seq until the next close).
        // Use max index so needsUpdate only gates on inProgress_.
        auto const claimIndex =
            processSteadyOnOpen ? std::numeric_limits<LedgerIndex>::max() : ledgerSeq;

        for (auto const& wr : requests)
        {
            if (app_.getJobQueue().isStopping())
                break;

            auto request = wr.lock();
            if (!request)
            {
                dropRequest(nullptr);
                continue;
            }

            bool const isFirst = request->isNew();
            if (!request->needsUpdate(newOnly, claimIndex))
                continue;

            if (isFirst)
                firstUpdates.push_back(std::move(request));
            else if (closedLedger || processSteadyOnOpen)
                steadyUpdates.push_back(std::move(request));
            else
            {
                // Open non-midClose: claimed a non-new request somehow — release.
                request->updateComplete();
            }
        }

        // Progressive line fill once per closed/create wave (shared cache), not
        // per session inside parallel doUpdate (avoids N unique-lock expands).
        if (!processSteadyOnOpen && cache)
            cache->expandIncompleteLines();

        // First full updates: serial — avoids ramp load spikes / gap mountains.
        // Pin lastIndex_ only on closed ledgers so an open first-update at seq S
        // does not skip the subsequent closed wave at the same S. isNew() clears
        // via markCompleted without a pin on open.
        std::size_t firstDone = 0;
        for (; firstDone < firstUpdates.size(); ++firstDone)
        {
            if (app_.getJobQueue().isStopping())
                break;
            if (!newRequests && app_.getLedgerMaster().isNewPathRequest())
            {
                mustBreak = true;
                break;
            }

            auto const& req = firstUpdates[firstDone];
            // First update: full Pathfinder (revalidateOnly=false).
            bool const keep = runOne(req, /*pinIndex=*/closedLedger, /*revalidateOnly=*/false);
            ++processed;
            if (!keep)
                dropRequest(req);
        }
        // Release claims for first updates we never started.
        for (std::size_t i = firstDone; i < firstUpdates.size(); ++i)
            firstUpdates[i]->updateComplete();

        if (!mustBreak && !app_.getJobQueue().isStopping())
        {
            // Established sessions: bounded parallel revalidate (main gap win).
            // Closed: pin lastIndex_. Mid-close: do not pin.
            // revalidateOnly ONLY for mid-close so closed waves can rediscover
            // / recover failed searches (staggered / backoff in doUpdate).
            bool const pinSteady = closedLedger;
            bool const revalidateOnly = processSteadyOnOpen;
            runParallel(
                app_.getJobQueue(),
                steadyUpdates,
                rpc::tuning::kPathSteadyUpdateParallelism,
                runOne,
                pinSteady,
                revalidateOnly,
                dropRequest,
                processed);
        }
        else
        {
            // Release steady claims we never started.
            for (auto const& req : steadyUpdates)
                req->updateComplete();
        }

        if (mustBreak)
        {
            // Interrupted to pick up brand-new sessions; loop with newOnly.
            newRequests = true;
        }
        else if (newRequests)
        {
            newRequests = app_.getLedgerMaster().isNewPathRequest();
            if (!newRequests)
            {
                if (closedLedger)
                {
                    // Drain done (newRequests already false). Fall through to
                    // re-snapshot and run one full pass so everyone is updated
                    // for this validated ledger (skips those already completed
                    // via lastIndex_ >= seq).
                }
                else if (processSteadyOnOpen)
                {
                    // Mid-close wave finished (new + steady). Done until next
                    // close or another mid-close timer.
                    break;
                }
                else
                {
                    // Open-ledger create wake: new sessions only.
                    break;
                }
            }
        }
        else
        {
            newRequests = app_.getLedgerMaster().isNewPathRequest();
            if (!newRequests)
                break;
            // New sessions arrived during a full pass — handle them next.
        }

        {
            std::scoped_lock const sl(lock_);
            if (requests_.empty())
                break;
            requests = requests_;
            cache = getAssetCache(cache->getLedger(), false);
        }
        mustBreak = false;
    } while (!app_.getJobQueue().isStopping());

    if (cache)
        publishCacheStats(*cache);

    // Keep the periodic revalidate timer armed while sessions are live.
    // (Also started from insertPathRequest; this re-arms after closed waves.)
    if (requestsPending() && !app_.isStopping())
        scheduleMidCloseRefresh();
    else if (!requestsPending())
    {
        // Drop any residual dead weaks and release cache if fully idle.
        std::scoped_lock const sl(lock_);
        releaseCacheIfIdleUnlocked();
    }

    JLOG(journal_.debug()) << "updateAll complete: " << processed << " processed and " << removed
                           << " removed";
}

bool
PathRequestManager::hasLiveRequestsUnlocked() const
{
    for (auto const& w : requests_)
    {
        if (w.lock())
            return true;
    }
    return false;
}

void
PathRequestManager::releaseCacheIfIdleUnlocked()
{
    // Drop expired weak_ptrs first — they previously kept requests_ non-empty
    // forever after WS disconnect, so AssetCache was never reclaimed.
    auto dead = std::ranges::remove_if(requests_, [](auto const& wl) { return !wl.lock(); });
    requests_.erase(dead.begin(), dead.end());

    if (hasLiveRequestsUnlocked())
    {
        // Per-account pins (releaseSession on each close) already freed
        // unreferenced entries. Remaining map content is still held by live
        // sessions — do not LRU/proportionally evict shared hubs.
        return;
    }

    midCloseTimer_.cancel();
    midCloseScheduled_.store(false, std::memory_order_release);
    revalidateJobPending_.store(false, std::memory_order_release);

    if (!assetCache_)
        return;

    auto const lines = assetCache_->totalLineCount();
    auto const loaded = assetCache_->linesLoaded();
    publishCacheStats(*assetCache_);
    JLOG(journal_.info()) << "releasing AssetCache (no path_find sessions) lines=" << lines
                          << " lifetime_loaded=" << loaded;
    assetCache_.reset();
    // Reset published baselines so the next cache instance does not invent
    // huge insight deltas from a fresh 0-based counter set.
    lastCacheHits_ = 0;
    lastCacheMisses_ = 0;
    lastLinesLoaded_ = 0;
    lastLedgerAdvances_ = 0;
}

bool
PathRequestManager::requestsPending() const
{
    std::scoped_lock const sl(lock_);
    // Only live sessions — expired weak_ptrs must not keep pathfinding "busy"
    // or pin the AssetCache after all websockets have closed.
    return hasLiveRequestsUnlocked();
}

PathRequestManager::CacheStats
PathRequestManager::getCacheStats() const
{
    std::scoped_lock const sl(lock_);
    CacheStats stats;
    if (!assetCache_)
        return stats;

    stats.available = true;
    stats.hits = assetCache_->cacheHits();
    stats.misses = assetCache_->cacheMisses();
    stats.linesLoaded = assetCache_->linesLoaded();
    stats.ledgerAdvances = assetCache_->ledgerAdvances();
    stats.totalLines = assetCache_->totalLineCount();
    return stats;
}

void
PathRequestManager::removePathRequest(PathRequest* request)
{
    std::scoped_lock const sl(lock_);
    auto ret = std::ranges::remove_if(requests_, [&](auto const& wl) {
        auto r = wl.lock();
        return !r || (request && r.get() == request);
    });
    requests_.erase(ret.begin(), ret.end());

    // Drop this session's account pins. Accounts still pinned by other live
    // path_finds are kept; only exclusively held (or last holder) entries free.
    if (assetCache_ && request)
    {
        auto const freed = assetCache_->releaseSession(request->id());
        if (freed > 0)
            publishCacheStats(*assetCache_);
    }

    releaseCacheIfIdleUnlocked();
}

void
PathRequestManager::insertPathRequest(PathRequest::pointer const& req)
{
    bool armTimer = false;
    {
        std::scoped_lock const sl(lock_);

        auto ret = std::ranges::find_if(requests_, [](auto const& wl) {
            auto r = wl.lock();
            return r && !r->isNew();
        });

        armTimer = !hasLiveRequestsUnlocked();
        requests_.emplace(ret, req);
    }

    // First live session: start periodic revalidate ticks immediately so gaps
    // are not gated on waiting for a full closed-ledger wave to finish.
    if (armTimer && !app_.isStopping())
        scheduleMidCloseRefresh();
}

json::Value
PathRequestManager::makePathRequest(
    std::shared_ptr<InfoSub> const& subscriber,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& requestJson)
{
    auto req = std::make_shared<PathRequest>(app_, subscriber, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(getAssetCache(inLedger, false), requestJson);

    if (valid)
    {
        subscriber->setRequest(req);
        insertPathRequest(req);
        app_.getLedgerMaster().newPathRequest();
    }
    return std::move(jvRes);
}

json::Value
PathRequestManager::makeLegacyPathRequest(
    PathRequest::pointer& req,
    std::function<void(void)> completion,
    resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& request)
{
    req = std::make_shared<PathRequest>(
        app_, completion, consumer, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(getAssetCache(inLedger, false), request);

    if (!valid)
    {
        req.reset();
    }
    else
    {
        insertPathRequest(req);
        if (!app_.getLedgerMaster().newPathRequest())
        {
            jvRes = rpcError(RpcTooBusy);
            req.reset();
        }
    }

    return std::move(jvRes);
}

json::Value
PathRequestManager::doLegacyPathRequest(
    resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& request)
{
    auto const& cfg = app_.config();
    auto cache = std::make_shared<AssetCache>(
        inLedger,
        app_.getJournal("AssetCache"),
        cfg.pathFindMaxTotalLines,
        cfg.pathFindMaxLinesPerAccount,
        cfg.pathCacheReuseLedgers,
        cfg.pathFindLineChunkSize);

    auto req =
        std::make_shared<PathRequest>(app_, [] {}, consumer, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(cache, request);
    if (valid)
        jvRes = req->doUpdate(cache, false);
    return std::move(jvRes);
}

}  // namespace xrpl
