#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
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

PathRequestManager::PathRequestManager(
    Application& app,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector)
    : app_(app)
    , journal_(journal)
    , midCloseBag_(std::make_shared<PathFindLifetime>())
    , midCloseTimer_(app.getIOContext())
    , lastIdentifier_(0)
{
    midCloseBag_->manager = this;
    fast_ = collector->makeEvent("pathfind_fast");
    full_ = collector->makeEvent("pathfind_full");
    cacheHits_ = collector->makeCounter("pathfind_cache_hits");
    cacheMisses_ = collector->makeCounter("pathfind_cache_misses");
    linesLoaded_ = collector->makeCounter("pathfind_lines_loaded");
    cacheLedgerAdvances_ = collector->makeCounter("pathfind_cache_advances");
}

PathRequestManager::~PathRequestManager()
{
    // 1. Stop new callbacks from observing *this. A PathRequest whose
    //    destructor is already running has use_count()==0, so wr.lock()
    //    below cannot detach it — that thread may already have copied the
    //    manager pointer. Nulling manager first makes enterOwner() fail for
    //    any callback that has not yet incremented inFlight.
    {
        std::lock_guard const lk(midCloseBag_->mutex);
        midCloseBag_->manager = nullptr;
        cancelMidCloseTimerUnlocked();
    }

    // 2. Detach remaining live PathRequests so later ~PathRequest / doClose
    //    skip the owner_ fast path. Force-dropped sessions were already
    //    detached. Do not wait here — in-flight callbacks still need members.
    {
        std::scoped_lock const sl(lock_);
        for (auto const& wr : requests_)
        {
            if (auto req = wr.lock())
                req->detachFromManager();
        }
        requests_.clear();
        assetCache_.reset();
    }

    // 3. Wait for every callback that already entered inFlight (mid-close
    //    timer / JtRpc, and PathRequest doClose / destructor / metrics).
    //    Do not hold bag->mutex across refresh — that stalled io_context.
    {
        std::unique_lock lk(midCloseBag_->mutex);
        midCloseBag_->idle.wait(lk, [this] { return midCloseBag_->inFlight == 0; });
    }
}

void
PathRequestManager::publishCacheStats(AssetCache const& cache)
{
    // lastCache* baselines are shared with removePathRequest / dropRequest /
    // releaseCacheIfIdleUnlocked (WS close threads). Always take lock_ —
    // recursive so call sites that already hold it are fine; updateAll's
    // end-of-wave publish must not race unlocked.
    std::scoped_lock const sl(lock_);

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

    // Large jumps force a rebuild so the soft-reuse window cannot straddle a
    // huge gap. largeJumpForward is *not* gated on authoritative: the cache is
    // a strong shared_ptr for the life of any session, and creates
    // (makePathRequest / makeLegacyPathRequest) pass authoritative=false. If
    // updatePaths is starved, those creates must not serve routes from a ledger
    // more than 8 sequences behind. largeJumpBack stays authoritative-only
    // (historical getLineCache).
    bool const largeJumpForward = lgrSeq > (lineSeq + 8);
    bool const largeJumpBack = (lgrSeq + 8) < lineSeq;
    if (largeJumpForward || (authoritative && largeJumpBack))
    {
        JLOG(journal_.info()) << "getAssetCache large ledger jump " << lineSeq << " -> " << lgrSeq
                              << "; force rebuild";
        assetCache_->advanceLedger(ledger, /*forceClear=*/true);
        return assetCache_;
    }

    // Small sequence changes: only authoritative (validated/closed or closed
    // create waves) mutate the shared cache. Non-authoritative callers (WS
    // create / legacy doCreate / mid-close) share the live view — Pathfinder
    // snapshots cache->getLedger() at construct; a slightly older closed view
    // than the ledger passed in is the intentional reuse window.
    if (!authoritative)
        return assetCache_;

    if (lgrSeq > lineSeq)
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
 * concurrency is visible to job accounting. Requested width is
 * kPathSteadyUpdateParallelism (== kPathFindWorkLimit); effective width is:
 *   workers < 3  → serial (no fork-join)
 *   workers >= 3 → min(requested, workers - 1) per batch
 *                  (1 unit inline + ≤ workers - 2 JtPathFindWork siblings)
 *
 * workerCount comes from JobQueue::getWorkerCount() (actual pool size), not a
 * re-derived Config estimate — the old floor of 2 forced serial revalidate on
 * every default multi-thread node (real pools are typically 2+min(hw,4) ≥ 3).
 *
 * Fork-join from a JobQueue thread: queue siblings, run one unit on this
 * thread, then wait on doneCv. That blocks a pool thread, so fan-out needs
 * spare workers that can still drain JtPathFindWork.
 *
 * Deadlock class (workers == 2; or workers == 3 with batch > workers - 1):
 *   - Thread A: updateAll holds waveMutex_, forks siblings, waits doneCv
 *   - Thread B: second updateAll blocks on waveMutex_ (closed vs mid-close)
 *   - No free worker left for JtPathFindWork → both wait forever
 *
 * Mitigations (thresholds above): serial for workers < 3; batch ≤ workers - 1
 * reserves one pool thread for a concurrent waveMutex_ waiter. Safe with
 * mid-close try_lock (skips rather than blocking a third pool thread forever).
 *
 * Completes the full work vector — do not abort mid-wave for new path_find
 * sessions (that stretched mean update gap under load).
 */
void
runParallel(
    JobQueue& jobQueue,
    std::vector<PathRequest::pointer> const& work,
    int parallelism,
    int workerCount,
    std::function<bool(PathRequest::pointer const&, bool, bool)> const& runOne,
    bool pinIndex,
    bool revalidateOnly,
    std::function<void(PathRequest::pointer const&)> const& onDrop,
    int& processed)
{
    if (work.empty())
        return;

    auto runSerial = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i)
        {
            if (jobQueue.isStopping())
            {
                // Release claims for work we never started so inProgress_ cannot
                // stick forever across shutdown (needsUpdate would stay false).
                for (std::size_t j = i; j < end; ++j)
                    work[j]->updateComplete();
                break;
            }
            bool const keep = runOne(work[i], pinIndex, revalidateOnly);
            ++processed;
            if (!keep)
                onDrop(work[i]);
        }
    };

    // Need: this parent (doneCv) + ≥1 sibling runner + spare for a concurrent
    // waveMutex_ waiter. With only 2 workers the spare is gone as soon as a
    // second updateAll blocks on the wave lock — permanent freeze.
    if (workerCount < 3 || parallelism <= 1)
    {
        runSerial(0, work.size());
        return;
    }

    // 1 unit inline on this thread; queue ≤ workerCount-2 siblings so that
    // even if another JobQueue thread is blocked on waveMutex_, the remaining
    // workers can still complete the barrier.
    auto const maxBatch = static_cast<std::size_t>(workerCount - 1);
    auto const par = static_cast<std::size_t>(
        std::max(std::size_t{1}, std::min(static_cast<std::size_t>(parallelism), maxBatch)));

    for (std::size_t batch = 0; batch < work.size(); batch += par)
    {
        if (jobQueue.isStopping())
        {
            // Release claims for batches we never started.
            for (std::size_t i = batch; i < work.size(); ++i)
                work[i]->updateComplete();
            break;
        }

        auto const end = std::min(batch + par, work.size());
        auto const count = end - batch;

        if (count == 1)
        {
            runSerial(batch, end);
            continue;
        }

        // results[i] = keep for work[batch + i]
        auto results = std::make_shared<std::vector<char>>(count, 0);

        // Barrier must outlive any helper still finishing notify after the
        // coordinator sees remaining==0 and leaves wait (stack mutex/cv UAF):
        // last helper does fetch_sub then needs to lock+notify; coordinator may
        // already have observed remaining==0 (predicate / spurious wake) and
        // destroyed stack sync objects. Shared ownership keeps them alive.
        struct BatchBarrier
        {
            std::mutex mutex;
            std::condition_variable cv;
            std::atomic<std::size_t> remaining{0};
        };
        auto barrier = std::make_shared<BatchBarrier>();
        barrier->remaining.store(count, std::memory_order_relaxed);

        auto finishOne = [barrier]() {
            if (barrier->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard const lk(barrier->mutex);
                barrier->cv.notify_one();
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
                // Never leave the batch barrier hanging. ClaimGuard in runOne
                // clears inProgress_ so the next wave can retry.
                //
                // Return true (keep): a false result is treated as onDrop and
                // would permanently remove an open path_find subscription after
                // a transient ledger error (e.g. SHAMapMissingNode). Serial
                // runOne rethrows to LedgerMaster instead; parallel must not
                // convert the same failure into a silent unsubscribe.
                return true;
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

        // Last unit always runs on this thread so the barrier makes progress
        // even when every other worker is busy (siblings drain on the rest).
        {
            auto const idx = count - 1;
            auto const& req = work[batch + idx];
            (*results)[idx] = runUnit(req) ? 1 : 0;
            finishOne();
        }

        {
            std::unique_lock lk(barrier->mutex);
            barrier->cv.wait(
                lk, [&] { return barrier->remaining.load(std::memory_order_acquire) == 0; });
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
PathRequestManager::cancelMidCloseTimerUnlocked()
{
    // Caller holds midCloseBag_->mutex. Bump epoch so a pending async_wait
    // (operation_aborted or late fire) cannot clear scheduled after a re-arm
    // or call onMidCloseTimer on a cancelled generation.
    ++midCloseBag_->epoch;
    midCloseBag_->scheduled = false;
    midCloseTimer_.cancel();
}

void
PathRequestManager::scheduleMidCloseRefresh()
{
    // Only one mid-close timer in flight; re-armed after each tick while live.
    // Serialize all timer ops on bag->mutex — boost::asio::steady_timer is not
    // safe for concurrent expires_after/async_wait vs cancel from other threads
    // (insertPathRequest, last-session release, destructor, timer re-arm).
    auto bag = midCloseBag_;
    std::uint64_t epoch = 0;
    {
        std::lock_guard const lk(bag->mutex);
        if (!bag->manager || bag->scheduled)
            return;
        bag->scheduled = true;
        epoch = ++bag->epoch;
        midCloseTimer_.expires_after(app_.config().pathMidCloseDelay);
        // Capture bag (not raw this). Enter inFlight only while using manager so
        // ~PathRequestManager can wait without cancel() having to join io threads.
        // Never hold bag->mutex across onMidCloseTimer / revalidate (io stall).
        midCloseTimer_.async_wait([bag, epoch](boost::system::error_code const& waitEc) {
            PathRequestManager* self = nullptr;
            {
                std::lock_guard const lk(bag->mutex);
                // Stale generation (cancelled or superseded) — ignore.
                if (epoch != bag->epoch)
                    return;
                bag->scheduled = false;
                if (!bag->manager)
                    return;
                self = bag->manager;
                ++bag->inFlight;
            }
            struct InFlightGuard
            {
                PathFindLifetime& bag;
                ~InFlightGuard()
                {
                    std::lock_guard const lk(bag.mutex);
                    --bag.inFlight;
                    bag.idle.notify_all();
                }
            } const inFlightGuard{*bag};
            self->onMidCloseTimer(waitEc);
        });
    }
}

void
PathRequestManager::onMidCloseTimer(boost::system::error_code const& waitEc)
{
    // Caller has entered PathFindLifetime::inFlight so *this stays alive.
    // bag->scheduled was cleared by the async_wait handler under bag->mutex.
    if (waitEc || app_.isStopping() || !requestsPending())
        return;

    // Non-blocking: dispatch revalidate on JtRpc so it never waits behind
    // JtUpdatePf (limit 1) closed-ledger / first-update waves. Skip if a
    // prior tick is still queued or running (wave overran the period).
    auto bag = midCloseBag_;
    if (!revalidateJobPending_.exchange(true, std::memory_order_acq_rel))
    {
        bool const queued = app_.getJobQueue().addJob(JtRpc, "PthFindReval", [bag]() {
            // Brief bag lock only to claim manager + inFlight. Release before
            // runPeriodicRevalidate so io_context timer threads are never
            // blocked for the whole wave waiting on bag->mutex.
            PathRequestManager* self = nullptr;
            {
                std::lock_guard const lk(bag->mutex);
                if (!bag->manager)
                    return;
                self = bag->manager;
                ++bag->inFlight;
            }
            struct InFlightGuard
            {
                PathFindLifetime& bag;
                PathRequestManager* self;
                ~InFlightGuard()
                {
                    // Always clear pending (including non-std::exception) so a
                    // stuck flag cannot permanently suppress mid-close ticks.
                    if (self)
                        self->revalidateJobPending_.store(false, std::memory_order_release);
                    std::lock_guard const lk(bag.mutex);
                    --bag.inFlight;
                    bag.idle.notify_all();
                }
            } const inFlightGuard{*bag, self};

            try
            {
                self->runPeriodicRevalidate();
            }
            catch (std::exception const& ex)
            {
                JLOG(self->journal_.info()) << "periodic path revalidate exception: " << ex.what();
            }
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

    // Pin / claim index must track the wave's ledger, not a lagging cache view.
    // Closed waves: inLedger is authoritative. Mid-close does not pin lastIndex_
    // (pinIndex=false); cache seq is fine for logging / claimIndex edge cases.
    auto const ledgerSeq = closedLedger ? inLedger->seq() : cache->getLedger()->seq();
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
        removed += static_cast<int>(rebuildRequestsUnlocked(request ? request.get() : nullptr));

        // Always release session pins (resource-pressure / exception drops used
        // to skip this and strand PathFindTrustLine vectors under budget).
        if (assetCache_ && request)
        {
            auto const freed = assetCache_->releaseSession(request->id());
            if (freed > 0)
                publishCacheStats(*assetCache_);
        }

        // Force-drop removes the weak from requests_ but the PathRequest may
        // outlive this wave (WS still holds a shared_ptr). Clear owner_ so
        // ~PathRequest does not call removePathRequest on a manager that was
        // never told to detach this session (manager dtor only detaches
        // entries still in requests_ — force-dropped ones would UAF).
        if (request)
            request->detachFromManager();

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

        // Progressive line fill once per closed/create wave (shared cache), not
        // per session inside parallel doUpdate (avoids N unique-lock expands).
        //
        // Must run BEFORE needsUpdate claims inProgress_. getItemsChunk can
        // throw SHAMapMissingNode (incomplete ledger data). If expand ran after
        // claims and threw, LedgerMaster/runPeriodicRevalidate catch the error
        // but never clear inProgress_ — open subscriptions then permanently
        // skip every later wave (needsUpdate returns false while inProgress_).
        // waveMutex_ already serializes waves, so expand-before-claim is safe.
        if (!processSteadyOnOpen && cache)
            cache->expandIncompleteLines();

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

            // Mid-close is revalidate-only for established WS subscriptions.
            // Never first-Pathfind brand-new sessions or complete one-shot
            // legacy ripple_path_find against the open ledger here — those
            // belong to create/closed waves (updatePaths).
            if (processSteadyOnOpen)
            {
                if (isFirst || request->hasCompletion())
                {
                    request->updateComplete();  // release claim only
                    continue;
                }
                steadyUpdates.push_back(std::move(request));
                continue;
            }

            if (isFirst)
                firstUpdates.push_back(std::move(request));
            else if (closedLedger)
                steadyUpdates.push_back(std::move(request));
            else
            {
                // Open non-midClose: claimed a non-new request somehow — release.
                request->updateComplete();
            }
        }

        // First full updates: serial — avoids ramp load spikes / gap mountains.
        // Pin lastIndex_ only on closed ledgers so an open first-update at seq S
        // does not skip the subsequent closed wave at the same S. isNew() clears
        // via markCompleted without a pin on open.
        //
        // needsUpdate already set inProgress_ for every entry in firstUpdates /
        // steadyUpdates. If runOne throws (e.g. SHAMapMissingNode), release all
        // remaining claims before rethrowing — otherwise those sessions stay
        // inProgress forever and never receive another update.
        std::size_t firstDone = 0;
        try
        {
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
                // Established sessions: steady revalidate (main gap win).
                // runParallel is serial when JobQueue workers < 3, else batches
                // of min(kPathSteadyUpdateParallelism, workers - 1).
                // Closed: pin lastIndex_. Mid-close: do not pin.
                // revalidateOnly ONLY for mid-close so closed waves can rediscover
                // / recover failed searches (staggered / backoff in doUpdate).
                // runParallel's runUnit catches per-unit errors (no claim leak).
                bool const pinSteady = closedLedger;
                bool const revalidateOnly = processSteadyOnOpen;
                runParallel(
                    app_.getJobQueue(),
                    steadyUpdates,
                    rpc::tuning::kPathSteadyUpdateParallelism,
                    app_.getJobQueue().getWorkerCount(),
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
        }
        catch (...)
        {
            // runOne's ClaimGuard already cleared the throwing request. Clear
            // every other claimed session so they are not frozen (updateComplete
            // is idempotent when inProgress_ is already false).
            for (std::size_t i = firstDone; i < firstUpdates.size(); ++i)
                firstUpdates[i]->updateComplete();
            for (auto const& req : steadyUpdates)
                req->updateComplete();
            throw;
        }

        if (mustBreak)
        {
            // Interrupted to pick up brand-new sessions; loop with newOnly.
            newRequests = true;
        }
        else if (processSteadyOnOpen)
        {
            // One mid-close revalidate pass. Never poll/consume
            // pathFindNewRequest_ here — that would steal creates from
            // LedgerMaster::updatePaths. updatePaths checks the flag before
            // calling updateAll; if mid-close clears it, the create job exits
            // with "Nothing to do" and brand-new path_find clients wait until
            // the next closed ledger for their first full Pathfinder result.
            break;
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

    // Publish only while this wave's cache is still the manager's live
    // instance. If the last subscription ended mid-update, dropRequest /
    // releaseCacheIfIdle already published deltas and zeroed lastCache*
    // baselines; publishing the local shared_ptr again would re-count the
    // entire cache lifetime as a new insight delta (inflated counters).
    {
        std::scoped_lock const sl(lock_);
        if (cache && assetCache_ == cache)
            publishCacheStats(*cache);
    }

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
    // Use expired() — do not promote weak_ptr to shared_ptr. A temporary
    // shared_ptr that is the last owner would run ~PathRequest → removePathRequest
    // and re-enter while callers iterate requests_.
    for (auto const& w : requests_)
    {
        if (!w.expired())
            return true;
    }
    return false;
}

std::size_t
PathRequestManager::rebuildRequestsUnlocked(PathRequest* request)
{
    // Hold every successfully locked request until AFTER requests_ is replaced.
    // Otherwise the temporary shared_ptr from weak_ptr::lock() can be the last
    // owner; its destructor calls removePathRequest and erases requests_ while
    // the caller is still mid-iteration (recursive_mutex allows re-entry).
    std::vector<PathRequest::pointer> keepAlive;
    std::vector<PathRequest::wptr> survivors;
    keepAlive.reserve(requests_.size());
    survivors.reserve(requests_.size());

    std::size_t removed = 0;
    for (auto const& wl : requests_)
    {
        auto r = wl.lock();
        if (!r)
        {
            ++removed;
            continue;
        }
        keepAlive.push_back(r);
        if (request && r.get() == request)
        {
            ++removed;
            continue;
        }
        survivors.push_back(wl);
    }
    requests_ = std::move(survivors);
    // keepAlive destructs after requests_ is stable; nested removePathRequest
    // (if any) only rebuilds an already-consistent vector.
    return removed;
}

void
PathRequestManager::releaseCacheIfIdleUnlocked()
{
    // Drop expired weak_ptrs first — they previously kept requests_ non-empty
    // forever after WS disconnect, so AssetCache was never reclaimed.
    // expired() only — never lock() here (see rebuildRequestsUnlocked).
    auto dead = std::ranges::remove_if(requests_, [](auto const& wl) { return wl.expired(); });
    requests_.erase(dead.begin(), dead.end());

    if (hasLiveRequestsUnlocked())
    {
        // Per-account pins (releaseSession on each close) already freed
        // unreferenced entries. Remaining map content is still held by live
        // sessions — do not LRU/proportionally evict shared hubs.
        return;
    }

    {
        // Timer cancel must use the same mutex as scheduleMidCloseRefresh
        // (insert / timer / destructor threads).
        std::lock_guard const lk(midCloseBag_->mutex);
        cancelMidCloseTimerUnlocked();
    }
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
    rebuildRequestsUnlocked(request);

    // Drop this session's account pins. Accounts still pinned by other live
    // path_finds are kept; only exclusively held (or last holder) entries free.
    if (assetCache_ && request)
    {
        auto const freed = assetCache_->releaseSession(request->id());
        if (freed > 0)
            publishCacheStats(*assetCache_);
    }

    // Clear owner_ once unlinked so a later ~PathRequest (or a second close)
    // does not re-enter after this manager is destroyed. Manager dtor only
    // detaches sessions still listed in requests_; closed/force-dropped ones
    // would otherwise keep a dangling owner_.
    if (request)
        request->detachFromManager();

    releaseCacheIfIdleUnlocked();
}

void
PathRequestManager::insertPathRequest(PathRequest::pointer const& req)
{
    bool armTimer = false;
    {
        std::scoped_lock const sl(lock_);

        // Promote only while scanning; keep alive until after emplace so a
        // last-ref temporary cannot re-enter removePathRequest mid-find.
        std::vector<PathRequest::pointer> keepAlive;
        auto insertAt = requests_.end();
        for (auto it = requests_.begin(); it != requests_.end(); ++it)
        {
            auto r = it->lock();
            if (!r)
                continue;
            keepAlive.push_back(r);
            if (!r->isNew())
            {
                insertAt = it;
                break;
            }
        }

        armTimer = !hasLiveRequestsUnlocked();
        requests_.emplace(insertAt, req);
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
