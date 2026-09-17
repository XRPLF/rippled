/**
 * AppMetricGauges implementation — the pull-model half of the OTel metric
 * surface.
 *
 * This file contains:
 * - Registration of every observable instrument whose callback samples live
 *   server state: cache hit rates, TxQ state, CountedObject instances, load
 *   factors, NodeStore I/O, server info, complete ledger ranges, validator
 *   health, peer quality, reduce-relay efficiency, ledger economy, state
 *   tracking, storage detail and validation agreement.
 * - The nodestore_state helpers those callbacks publish values through.
 * - The arm and disarm entry points for the whole set.
 */

// On Windows, OTel's spin_lock_mutex.h (transitively included from
// MetricsRegistry.h) defines _WINSOCKAPI_ and includes <windows.h>.
// This poisons the include state for boost/asio/detail/socket_types.hpp,
// which requires winsock2.h to be included first.  Pre-including the
// boost/asio socket types header gets winsock2.h in before the OTel
// headers can interfere.
#ifdef _MSC_VER
#include <boost/asio/detail/socket_types.hpp>
#endif

#include <xrpld/telemetry/AppMetricGauges.h>

#ifdef XRPL_ENABLE_TELEMETRY

// The app and overlay includes below are why
// .github/scripts/levelization/results/loops.txt records
// `xrpld.app <-> xrpld.telemetry` and `xrpld.overlay <-> xrpld.telemetry` as
// cycles, rather than an acyclic ordering.txt entry placing telemetry strictly
// below both. The observable gauges are pull-model: their callbacks sample live
// state when the reader thread fires, so they need the concrete types to call
// getJqTransOverflow(), size(), getPeerDisconnectCharges(), foreach() and
// txMetrics().
//
// The cycle is confined to this translation unit. No telemetry header includes
// app or overlay -- the callbacks reach every service through the
// ServiceRegistry reference they are given -- and all of src/xrpld builds into a
// single CMake target, so there is no header cycle and no link cycle to break.
//
// Inverting it properly means declaring a metrics-source interface below overlay
// and implementing it there, which is deliberately left as follow-up rather than
// widening this change. Note loops.txt is generated: it can only change as a
// consequence of changing these includes, never by editing the baseline.
#include <xrpld/app/ledger/AcquireStats.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/telemetry/MetricNames.h>

#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#endif  // XRPL_ENABLE_TELEMETRY

namespace xrpl::telemetry {

AppMetricGauges::AppMetricGauges(
    [[maybe_unused]] MetricsRegistry& core,
    [[maybe_unused]] ServiceRegistry& app,
    [[maybe_unused]] beast::Journal journal)
#ifdef XRPL_ENABLE_TELEMETRY
    : core_(core)
    , app_(app)
    // The core logs through the same partition, so one log-level setting
    // covers the whole metric pipeline.
    , journal_(journal)
#endif
{
}

AppMetricGauges::~AppMetricGauges()
{
    // A last resort, not the teardown path: the flag this sets lives here, so
    // it cannot protect anything once this object is gone. The safe order is
    // detachCallbacks(), then the core's stop() to join the reader thread,
    // then destruction.
    detachCallbacks();
}

void
AppMetricGauges::startAsyncGauges()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!core_.isEnabled())
        return;

    // One arm per life. A second call would create a second set of
    // same-named instruments, and a call after stop() would register on a
    // provider that is gone. Checked before the pipeline, so a call after
    // stop() is reported as what it is and not as a build failure. The core
    // is enabled by here, so a false recording() means exactly stopped.
    bool const stopped = !core_.recording();
    if (armed_ || stopped)
    {
        JLOG(journal_.warn()) << "MetricsRegistry: startAsyncGauges() called "
                              << (stopped ? "after stop()" : "twice") << "; ignored";
        return;
    }

    // The pipeline failed to build: the meter is a no-op, so registering
    // gauges on it would only log a success that is not one. armed_ stays
    // false, so a second call lands here again and logs the same message.
    // Idempotent.
    if (!core_.hasPipeline())
    {
        JLOG(journal_.warn()) << "MetricsRegistry: startAsyncGauges() without a pipeline; "
                                 "no gauges registered";
        return;
    }
    armed_ = true;

    registerAsyncGauges();

    JLOG(journal_.info()) << "MetricsRegistry: started successfully";
#endif  // XRPL_ENABLE_TELEMETRY
}

void
AppMetricGauges::detachCallbacks() noexcept
{
#ifdef XRPL_ENABLE_TELEMETRY
    // Release so every subsequent callback acquire-load sees true.
    callbacksDetached_.store(true, std::memory_order_release);
#endif  // XRPL_ENABLE_TELEMETRY
}

// -----------------------------------------------------------------
// Observable gauge callbacks
// -----------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

void
AppMetricGauges::registerAsyncGauges()
{
    // Each helper creates one observable instrument and attaches one
    // callback. Keeping the registration bodies in separate methods
    // preserves the 80-line-per-function limit enforced by CLAUDE.md.
    registerJqTransOverflowCounter();
    registerCacheHitRateGauge();
    registerTxqGauge();
    registerObjectCountGauge();
    registerLoadFactorGauge();
    registerNodeStoreGauge();
    registerRotationStateGauge();
    registerServerInfoGauge();
    registerBuildInfoGauge();
    registerCompleteLedgersGauge();
    registerDbMetricsGauge();
    registerValidatorHealthGauge();
    registerPeerQualityGauge();
    registerReduceRelayGauge();
    registerLedgerEconomyGauge();
    registerStateTrackingGauge();
    registerStorageDetailGauge();
    registerValidationAgreementGauge();
    registerValidationTotalsCounters();
    registerUnlQuorumGauge();
    registerClockSkewGauge();
    registerSyncStateGauge();
    registerStallEventsCounter();
    registerSyncAcquireGauge();
    registerCacheHitRateDetailGauge();
    registerJobQueueSaturationGauge();
    registerPeerLedgerSupplyGauge();
    registerSlotCensusGauge();
    registerAmendmentBlockGauge();
    registerLedgerQuorumPublishGauge();
}

void
AppMetricGauges::registerJqTransOverflowCounter()
{
    // jq_trans_overflow_total is observed from Overlay's existing cumulative
    // atomic (Overlay::getJqTransOverflow()) rather than pushed. The overlay
    // owns the only increment site (PeerImp), so an ObservableCounter reads the
    // live total each collection cycle without threading a push path through
    // develop-owned overlay code.
    //
    // Registered with the gauges, not with the synchronous instruments: the
    // callback reads getOverlay(), which asserts overlay_ is non-null. Arming
    // it any earlier would let a reader tick fire before the overlay exists,
    // and an assert is not caught by the try block below.
    jqTransOverflowObservable_ = core_.meter()->CreateInt64ObservableCounter(
        "jq_trans_overflow_total", "Total job queue transaction overflows");
    jqTransOverflowObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(static_cast<int64_t>(self->app_.getOverlay().getJqTransOverflow()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerCacheHitRateGauge()
{
    // --- Cache hit rate and size gauges ---
    cacheHitRateGauge_ =
        core_.meter()->CreateDoubleObservableGauge("cache_metrics", "Cache hit rates and sizes");
    cacheHitRateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                // SLE cache hit rate (0.0 - 1.0).
                auto sleRate = app.getCachedSLEs().rate();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(sleRate, {{label::metric, "SLE_hit_rate"}});

                // Ledger cache hit rate.
                // TaggedCache::getHitRate() returns 0-100; normalize to
                // 0.0-1.0 so the Grafana panel using "percentunit" renders
                // correctly.
                auto ledgerRate = app.getLedgerMaster().getCacheHitRate() / 100.0;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(ledgerRate, {{label::metric, "ledger_hit_rate"}});

                // AcceptedLedger cache hit rate (also 0-100 from
                // TaggedCache; normalize to 0.0-1.0).
                auto alRate = app.getAcceptedLedgerCache().getHitRate() / 100.0;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(alRate, {{label::metric, "AL_hit_rate"}});

                // TreeNode cache size.
                auto tnCacheSize = app.getNodeFamily().getTreeNodeCache()->getCacheSize();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(
                        static_cast<double>(tnCacheSize), {{label::metric, "treenode_cache_size"}});

                // TreeNode track size.
                auto tnTrackSize = app.getNodeFamily().getTreeNodeCache()->getTrackSize();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(
                        static_cast<double>(tnTrackSize), {{label::metric, "treenode_track_size"}});

                // FullBelow cache size.
                auto fbSize = app.getNodeFamily().getFullBelowCache()->size();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(static_cast<double>(fbSize), {{label::metric, "fullbelow_size"}});

                // AcceptedLedger cache size (entry count).
                auto alSize = app.getAcceptedLedgerCache().size();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(static_cast<double>(alSize), {{label::metric, "AL_size"}});

                // Longest TaggedCache mutex hold since the last tick.
                // Split out to keep this callback under the 80-line limit.
                AppMetricGauges::observeCacheLockHoldPeaks(result, app);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::observeCacheLockHoldPeaks(
    opentelemetry::metrics::ObserverResult& result,
    ServiceRegistry& app)
{
    auto const tnPeak = app.getNodeFamily().getTreeNodeCache()->takeLockHoldPeak();
    opentelemetry::nostd::get<
        opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)
        ->Observe(
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(tnPeak).count()),
            {{label::metric, lval::cache_metrics::treenodeLockHoldPeakUs}});

    auto const fbPeak = app.getNodeFamily().getFullBelowCache()->takeLockHoldPeak();
    opentelemetry::nostd::get<
        opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result)
        ->Observe(
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(fbPeak).count()),
            {{label::metric, lval::cache_metrics::fullbelowLockHoldPeakUs}});
}

void
AppMetricGauges::registerTxqGauge()
{
    // --- TxQ metrics gauges ---
    txqGauge_ =
        core_.meter()->CreateDoubleObservableGauge("txq_metrics", "Transaction queue metrics");
    txqGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const metrics = app.getTxQ().getMetrics(*app.getOpenLedger().current());

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                observe("txq_count", static_cast<double>(metrics.txCount));
                observe(
                    "txq_max_size",
                    metrics.txQMaxSize ? static_cast<double>(*metrics.txQMaxSize) : 0.0);
                observe("txq_in_ledger", static_cast<double>(metrics.txInLedger));
                observe("txq_per_ledger", static_cast<double>(metrics.txPerLedger));
                observe(
                    "txq_reference_fee_level",
                    static_cast<double>(metrics.referenceFeeLevel.fee()));
                observe(
                    "txq_min_processing_fee_level",
                    static_cast<double>(metrics.minProcessingFeeLevel.fee()));
                observe("txq_med_fee_level", static_cast<double>(metrics.medFeeLevel.fee()));
                observe(
                    "txq_open_ledger_fee_level",
                    static_cast<double>(metrics.openLedgerFeeLevel.fee()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if TxQ or OpenLedger are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerObjectCountGauge()
{
    // --- Counted object instance gauges ---
    objectCountGauge_ = core_.meter()->CreateInt64ObservableGauge(
        "object_count", "Live instance counts for key internal object types");
    objectCountGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                // Iterate through all CountedObject types via the linked
                // list in CountedObjects.  We report all types with count
                // > 0, filtering to the key types of interest.
                auto counts = CountedObjects::getInstance().getCounts(0);
                for (auto const& [name, count] : counts)
                {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(static_cast<int64_t>(count), {{"type", name}});
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerLoadFactorGauge()
{
    // --- Load factor breakdown gauges ---
    loadFactorGauge_ = core_.meter()->CreateDoubleObservableGauge(
        "load_factor_metrics", "Fee load factor breakdown");
    loadFactorGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto& feeTrack = app.getFeeTrack();
                auto const loadBase = static_cast<double>(feeTrack.getLoadBase());

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Combined load factor (server component).
                observe(
                    "load_factor_server", static_cast<double>(feeTrack.getLoadFactor()) / loadBase);

                // Individual factor components.
                observe(
                    "load_factor_local", static_cast<double>(feeTrack.getLocalFee()) / loadBase);
                observe("load_factor_net", static_cast<double>(feeTrack.getRemoteFee()) / loadBase);
                observe(
                    "load_factor_cluster",
                    static_cast<double>(feeTrack.getClusterFee()) / loadBase);

                // Fee escalation factors from TxQ.
                auto const metrics = app.getTxQ().getMetrics(*app.getOpenLedger().current());
                auto refLevel = static_cast<double>(metrics.referenceFeeLevel.fee());
                if (refLevel > 0)
                {
                    observe(
                        "load_factor_fee_escalation",
                        static_cast<double>(metrics.openLedgerFeeLevel.fee()) / refLevel);
                    observe(
                        "load_factor_fee_queue",
                        static_cast<double>(metrics.minProcessingFeeLevel.fee()) / refLevel);
                }

                // Combined load factor (max of server and fee escalation).
                auto const loadFactorServer = feeTrack.getLoadFactor();
                auto const loadBaseServer = feeTrack.getLoadBase();
                double combined = static_cast<double>(loadFactorServer) / loadBase;
                if (refLevel > 0)
                {
                    double const feeEscalation =
                        static_cast<double>(metrics.openLedgerFeeLevel.fee()) * loadBaseServer /
                        refLevel;
                    if (feeEscalation > static_cast<double>(loadFactorServer))
                    {
                        combined = feeEscalation / loadBase;
                    }
                }
                observe("load_factor", combined);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::observeNodeStoreTotals(node_store::Database& db, ObserveFn const& observe)
{
    // Cumulative counters (monotonically increasing).
    observe("node_reads_total", static_cast<std::int64_t>(db.getFetchTotalCount()));
    observe("node_reads_hit", static_cast<std::int64_t>(db.getFetchHitCount()));
    observe("node_writes", static_cast<std::int64_t>(db.getStoreCount()));
    observe("node_written_bytes", static_cast<std::int64_t>(db.getStoreSize()));
    observe("node_read_bytes", static_cast<std::int64_t>(db.getFetchSize()));

    // Cumulative I/O durations, read straight off the atomics.
    observe("node_reads_duration_us", static_cast<std::int64_t>(db.getFetchDurationUs()));
    observe("node_writes_duration_us", static_cast<std::int64_t>(db.getStoreDurationUs()));

    // Mean latencies. A cumulative total cannot separate "every read took
    // 9 us" from "most took 2 and a few took 900", and the second is the
    // cold-store signature: the hit rate reads the same either way, only the
    // latency differs. Each mean is omitted rather than reported as zero
    // when nothing has been read or written, so a dashboard shows a gap
    // instead of a plausible wrong number.
    if (auto const mean =
            MetricsRegistry::scaledMean(db.getFetchDurationUs(), db.getFetchTotalCount()))
        observe("read_mean_us", *mean);
    if (auto const mean = MetricsRegistry::scaledMean(db.getStoreDurationUs(), db.getStoreCount()))
        observe("write_mean_us", *mean);

    // Write load score (instantaneous).
    observe("write_load", static_cast<std::int64_t>(db.getWriteLoad()));
}

void
AppMetricGauges::observeWritePathDetail(node_store::Database const& db, ObserveFn const& observe)
{
    auto const ws = db.getWriteStats();
    if (!ws)
        return;

    observe("nudb_writers_in_flight", static_cast<std::int64_t>(ws->concurrentWriters));
    observe("nudb_insert_max_us", static_cast<std::int64_t>(ws->insertMaxUs));

    if (auto const mean = MetricsRegistry::scaledMean(ws->insertTotalUs, ws->insertCount))
        observe("nudb_insert_mean_us", *mean);

    // Mean writer depth times 100. NuDB serializes inserts behind one
    // mutex, so this depth is the queue length at that mutex and sits just
    // above 1.0 even under load. An integral gauge would truncate that to 1
    // and lose the whole signal, hence the fixed-point scale -- which the
    // name states, so nobody reads 140 as 140 writers.
    if (auto const mean = MetricsRegistry::scaledMean(ws->depthSum, ws->depthSamples, 100))
        observe("nudb_writer_depth_x100", *mean);
}

void
AppMetricGauges::observeAcquireStats(AcquireStats const& stats, ObserveFn const& observe)
{
    // Published unconditionally: for a counter, zero is the meaningful
    // "no such event yet" reading, unlike for a mean. The diagnostic value
    // is in the pairs -- deferrals rising while timeouts stay flat means the
    // give-up path cannot fire, so an acquisition never ends.
    observe("acquire_deferrals", static_cast<std::int64_t>(stats.getDeferrals()));
    observe("acquire_timeouts", static_cast<std::int64_t>(stats.getTimeouts()));

    // The same two events, narrowed to ledger acquisition. The pair above
    // sums every TimeoutCounter subclass, so a busy replay lane can imitate
    // a stalled ledger acquisition; compare these two instead when asking
    // whether ledger acquisition's give-up path is advancing.
    observe("acquire_ledger_deferrals", static_cast<std::int64_t>(stats.getLedgerDeferrals()));
    observe("acquire_ledger_timeouts", static_cast<std::int64_t>(stats.getLedgerTimeouts()));
    observe("acquire_give_ups", static_cast<std::int64_t>(stats.getGiveUps()));
    observe("acquire_aborts", static_cast<std::int64_t>(stats.getAborts()));
    observe("acquire_aborts_partial", static_cast<std::int64_t>(stats.getAbortsWithPartialWork()));
    observe("acquire_completions", static_cast<std::int64_t>(stats.getCompletions()));
    observe("acquire_sweep_evictions", static_cast<std::int64_t>(stats.getSweepEvictions()));
}

void
AppMetricGauges::observeReadQueue(node_store::Database& db, ObserveFn const& observe)
{
    json::Value obj(json::ValueType::Object);
    db.getCountsJson(obj);

    if (obj.isMember("read_queue"))
        observe("read_queue", static_cast<std::int64_t>(obj["read_queue"].asUInt()));

    // Read thread pool stats (native JSON ints, no jss:: constants).
    for (auto const* key : {"read_request_bundle", "read_threads_running", "read_threads_total"})
    {
        if (obj.isMember(key))
            observe(key, static_cast<std::int64_t>(obj[key].asInt()));
    }
}

void
AppMetricGauges::registerNodeStoreGauge()
{
    // --- NodeStore I/O gauges ---
    // The cumulative counters (reads, writes, bytes) are also exposed here
    // as observable gauges.  This avoids adding an xrpld dependency into the
    // libxrpl nodestore code — the callback reads the existing atomic
    // counters from Database via its public accessors.
    //
    // Every value multiplexes onto this one gauge through its `metric`
    // label, so a new value needs no new instrument. The body is split
    // across four helpers, one per domain, to stay inside the per-function
    // line budget and to keep each domain testable on its own.
    nodeStoreGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::nodestoreState,
        "NodeStore I/O counters, latencies, write-queue depth and acquisition stalls");
    nodeStoreGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto& db = app.getNodeStore();

                ObserveFn const observe = [&](char const* name, std::int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Qualified because the enclosing lambda captures nothing:
                // these are static members, and the explicit scope says so.
                AppMetricGauges::observeNodeStoreTotals(db, observe);
                AppMetricGauges::observeWritePathDetail(db, observe);
                AppMetricGauges::observeAcquireStats(app.getAcquireStats(), observe);
                AppMetricGauges::observeReadQueue(db, observe);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerRotationStateGauge()
{
    // --- Sync diagnostics: what an online_delete rotation costs ---
    // A rotation performs writes an ordinary fetch would not: the archive
    // backend is about to be deleted, so any node body it serves during the
    // rotation window has to be rewritten into the writable backend to survive.
    // That work scales with the archive, competes with sync I/O, and appears
    // ONLY on a populated, already-rotated online_delete database -- which is
    // why it never shows up on a fresh node and why it was never measured. The
    // count existed as copyForwardCount_ but was log-only and reset per
    // rotation.
    //
    // Polled from the node store rather than pushed, matching
    // registerNodeStoreGauge above: DatabaseRotatingImp lives in libxrpl and
    // cannot include xrpld/telemetry, so the counters are read through the
    // DatabaseRotating accessors on each collection tick instead.
    rotationStateGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::rotationState, "Online-delete rotation state and copy-forward write total");
    rotationStateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                // Only a rotating store has a rotation to report. On a node
                // without online_delete the node store is a DatabaseNodeImp, so
                // the cast fails and NO series is published -- deliberately, so
                // that an absent series means "rotation is not configured"
                // rather than a zero that would read as "rotation is free".
                auto* rotating = dynamic_cast<node_store::DatabaseRotating*>(&app.getNodeStore());
                if (rotating == nullptr)
                    return;

                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                // The window the extra writes happen in. A panel needs this to
                // know when the copy-forward total is expected to move.
                observe(
                    lval::rotation_state::inFlight,
                    static_cast<int64_t>(rotating->isRotationInFlight() ? 1 : 0));

                // Monotonic, so the panel takes rate() over it. The per-rotation
                // tally that rotate() logs is reset on every swap and is
                // therefore unusable here.
                observe(
                    lval::rotation_state::copyForward,
                    static_cast<int64_t>(rotating->copyForwardTotal()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerServerInfoGauge()
{
    // --- Server info gauges ---
    serverInfoGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::serverInfo, "Server-level health metrics");
    serverInfoGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Server operating mode (DISCONNECTED=0 .. FULL=4).
                observe("server_state", static_cast<int64_t>(app.getOPs().getOperatingMode()));

                // Uptime in seconds since server start.
                observe(
                    "uptime", static_cast<int64_t>(UptimeClock::now().time_since_epoch().count()));

                // Total peer count (inbound + outbound).
                observe("peers", static_cast<int64_t>(app.getOverlay().size()));

                // Validated ledger sequence (0 if none yet).
                observe(
                    "validated_ledger_seq",
                    static_cast<int64_t>(app.getLedgerMaster().getValidLedgerIndex()));

                // Current open ledger sequence.
                observe(
                    "ledger_current_index",
                    static_cast<int64_t>(app.getLedgerMaster().getCurrentLedgerIndex()));

                // Cumulative resource-related peer disconnects.
                observe(
                    "peer_disconnects_resources",
                    static_cast<int64_t>(app.getOverlay().getPeerDisconnectCharges()));

                // Last consensus round data (from JSON — only public API).
                auto const consensusInfo = app.getOPs().getConsensusInfo();
                if (consensusInfo.isMember("previous_proposers"))
                {
                    observe(
                        "last_close_proposers",
                        static_cast<int64_t>(consensusInfo["previous_proposers"].asUInt()));
                }
                if (consensusInfo.isMember("previous_mseconds"))
                {
                    observe(
                        "last_close_converge_time_ms",
                        static_cast<int64_t>(consensusInfo["previous_mseconds"].asUInt()));
                }

                // Network close time of the last closed ledger, as NetClock
                // seconds since the XRPL epoch (2000-01-01). Unlike a span
                // timestamp, a gauge value survives as a queryable time series,
                // so dashboards can show last-close age (staleness) via
                // now - value. The close interval comes from the
                // ledgers_closed_total counter, not a delta of this gauge
                // (a timestamp gauge's delta aliases to the scrape period).
                // Skip until a ledger has closed.
                if (auto const closed = app.getLedgerMaster().getClosedLedger())
                {
                    observe(
                        "last_close_time",
                        static_cast<int64_t>(
                            closed->header().closeTime.time_since_epoch().count()));
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerBuildInfoGauge()
{
    // --- Build info gauge ---
    buildInfoGauge_ =
        core_.meter()->CreateInt64ObservableGauge("build_info", "Build version information");
    buildInfoGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* /* state */) {
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(1, {{"version", std::string(build_info::getVersionString())}});
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
            }
        },
        nullptr);
}

void
AppMetricGauges::registerCompleteLedgersGauge()
{
    // --- Complete ledgers range gauge ---
    completeLedgersGauge_ = core_.meter()->CreateInt64ObservableGauge(
        "complete_ledgers", "Complete ledger range start/end pairs");
    completeLedgersGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const rangeStr = app.getLedgerMaster().getCompleteLedgers();
                if (rangeStr.empty() || rangeStr == "empty")
                    return;

                // Parse comma-separated ranges like
                // "32570-50000,50005-75891421". A range of one ledger arrives
                // as a bare sequence number, so parseLedgerRange() decides what
                // a segment is; only genuinely unreadable ones are skipped.
                std::size_t rangeIndex = 0;
                std::istringstream stream(rangeStr);
                std::string segment;
                while (std::getline(stream, segment, ','))
                {
                    auto const range = MetricsRegistry::parseLedgerRange(segment);
                    if (!range)
                        continue;

                    auto const idxStr = std::to_string(rangeIndex);

                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(
                            static_cast<int64_t>(range->first),
                            {{"bound", "start"}, {"index", idxStr}});

                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(
                            static_cast<int64_t>(range->second),
                            {{"bound", "end"}, {"index", idxStr}});

                    ++rangeIndex;
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on parse error or if services not ready.
            }
        },
        this);
}

void
AppMetricGauges::registerDbMetricsGauge()
{
    // --- Database size and fetch rate gauges ---
    dbMetricsGauge_ = core_.meter()->CreateInt64ObservableGauge(
        "db_metrics", "Database storage sizes and fetch rates");
    dbMetricsGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                auto& rdb = app.getRelationalDatabase();
                observe("db_kb_total", static_cast<int64_t>(rdb.getKBUsedAll()));
                observe("db_kb_ledger", static_cast<int64_t>(rdb.getKBUsedLedger()));
                observe("db_kb_transaction", static_cast<int64_t>(rdb.getKBUsedTransaction()));

                // Historical ledger fetches per minute.
                observe(
                    "historical_perminute",
                    static_cast<int64_t>(app.getInboundLedgers().fetchRate()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerValidatorHealthGauge()
{
    // --- Validator health gauges ---
    validatorHealthGauge_ = core_.meter()->CreateDoubleObservableGauge(
        "validator_health", "Validator health indicators");
    validatorHealthGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                observe("amendment_blocked", app.getOPs().isAmendmentBlocked() ? 1.0 : 0.0);
                observe("unl_blocked", app.getOPs().isUNLBlocked() ? 1.0 : 0.0);
                observe("validation_quorum", static_cast<double>(app.getValidators().quorum()));

                // Days until UNL list expiry (-1 if no expiry known).
                auto const expiry = app.getValidators().expires();
                if (expiry)
                {
                    auto const now = app.getTimeKeeper().closeTime();
                    auto const diffHours =
                        std::chrono::duration_cast<std::chrono::hours>(*expiry - now).count();
                    observe("unl_expiry_days", static_cast<double>(diffHours) / 24.0);
                }
                else
                {
                    observe("unl_expiry_days", -1.0);
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerPeerQualityGauge()
{
    // --- Peer quality gauges ---
    // Uses Peer::json() to read latency and version since those accessors
    // are not on the abstract Peer interface (they live on PeerImp).
    peerQualityGauge_ = core_.meter()->CreateDoubleObservableGauge(
        metric::peerQuality, "Peer network quality metrics");
    peerQualityGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Collect latencies, version info, and tracking state from
                // each peer's JSON.
                std::vector<int> latencies;
                int higherVersionCount = 0;
                int totalPeers = 0;
                int divergedCount = 0;

                // Encode a version string into BuildInfo's comparable numeric
                // form. Peers report the full "xrpld-3.3.0-b0" string while
                // our baseline is the bare "3.3.0-b0", and SemanticVersion
                // requires a leading digit, so strip any non-digit prefix
                // first. Numeric encoding avoids the lexicographic bug where
                // "2.3.0" > "2.10.0" and "xrpld-..." > "3...".
                auto const encodeVersion = [](std::string_view v) -> std::uint64_t {
                    auto const firstDigit = v.find_first_of("0123456789");
                    if (firstDigit == std::string_view::npos)
                        return 0;
                    return build_info::encodeSoftwareVersion(v.substr(firstDigit));
                };
                auto const ownEncoded = encodeVersion(build_info::getVersionString());

                app.getOverlay().foreach([&](std::shared_ptr<Peer> const& peer) {
                    ++totalPeers;
                    auto const pj = peer->json();
                    if (pj.isMember(jss::latency))
                    {
                        latencies.push_back(pj[jss::latency].asInt());
                    }
                    if (pj.isMember(jss::version))
                    {
                        // Unparseable peer versions encode below ownEncoded, so
                        // the comparison correctly leaves them uncounted.
                        auto const pv = pj[jss::version].asString();
                        if (encodeVersion(pv) > ownEncoded)
                            ++higherVersionCount;
                    }
                    // PeerImp::json() sets "track" to "diverged" when the peer's
                    // tracking state is Tracking::Diverged (i.e. it is following
                    // a different ledger chain than us).
                    if (pj.isMember(jss::track) && pj[jss::track].asString() == "diverged")
                        ++divergedCount;
                });

                // P90 latency across connected peers.
                if (!latencies.empty())
                {
                    std::ranges::sort(latencies);
                    auto p90idx = static_cast<std::size_t>(latencies.size() * 0.9);
                    if (p90idx >= latencies.size())
                        p90idx = latencies.size() - 1;
                    observe("peer_latency_p90_ms", static_cast<double>(latencies[p90idx]));
                }
                else
                {
                    observe("peer_latency_p90_ms", 0.0);
                }

                // Percentage of peers running a higher version.
                double const higherPct = totalPeers > 0
                    ? (static_cast<double>(higherVersionCount) / totalPeers * 100.0)
                    : 0.0;
                observe("peers_higher_version_pct", higherPct);

                // Count peers diverged from our ledger chain, read from the
                // peer's "track" JSON field (set by PeerImp::json()). Diverged
                // peers are following a different chain and are a leading
                // indicator of local sync trouble.
                observe("peers_insane_count", static_cast<double>(divergedCount));

                // Binary flag: recommend upgrade if >60% run a newer version.
                observe("upgrade_recommended", higherPct > 60.0 ? 1.0 : 0.0);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerReduceRelayGauge()
{
    // Transaction reduce-relay efficiency. Overlay::txMetrics() exposes the
    // rolling averages as a JSON object with string values (std::to_string),
    // so parse each field. A high suppressed:selected ratio proves the
    // feature is saving bandwidth; a high not_enabled count means stale peers
    // force full relay.
    reduceRelayGauge_ = core_.meter()->CreateInt64ObservableGauge(
        "reduce_relay_metrics", "Transaction reduce-relay efficiency metrics");
    reduceRelayGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const tm = app.getOverlay().txMetrics();

                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Each field is a decimal string; emit when present and parseable.
                auto observeField = [&](auto const& field, char const* name) {
                    if (tm.isMember(field))
                    {
                        auto const s = tm[field].asString();
                        if (!s.empty())
                            observe(name, static_cast<int64_t>(std::stoll(s)));
                    }
                };

                observeField(jss::txr_selected_cnt, "selected_peers");
                observeField(jss::txr_suppressed_cnt, "suppressed_peers");
                observeField(jss::txr_not_enabled_cnt, "not_enabled_peers");
                observeField(jss::txr_missing_tx_freq, "missing_tx_freq");
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready or a value is
                // not parseable.
            }
        },
        this);
}

void
AppMetricGauges::registerLedgerEconomyGauge()
{
    // --- Ledger economy gauges ---
    ledgerEconomyGauge_ = core_.meter()->CreateDoubleObservableGauge(
        metric::ledgerEconomy, "Ledger fee and economy metrics");
    ledgerEconomyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Fee and reserve values from the validated ledger.
                auto const ledger = app.getLedgerMaster().getValidatedLedger();
                if (ledger)
                {
                    auto const& fees = ledger->fees();
                    // Cost of a reference transaction (drops).
                    observe("base_fee_xrp", static_cast<double>(fees.base.drops()));
                    // Base reserve = one account, zero owned objects:
                    // accountReserve(ownerCount=0, accountCount=1) == reserve.
                    observe(
                        "reserve_base_xrp", static_cast<double>(fees.accountReserve(0, 1).drops()));
                    observe("reserve_inc_xrp", static_cast<double>(fees.increment.drops()));
                }

                // Seconds since the last validated ledger closed.
                auto const age = app.getLedgerMaster().getValidatedLedgerAge();
                observe("ledger_age_seconds", static_cast<double>(age.count()));

                // Transaction rate from the open ledger's tx count.
                // OpenView::txCount() tracks transactions in the current
                // open ledger; dividing by the ledger age gives an
                // approximate rate.
                auto const& openLedger = app.getOpenLedger();
                auto const txInLedger = openLedger.current()->txCount();
                auto const ageVal = age.count();
                if (ageVal > 0)
                {
                    observe(
                        "transaction_rate",
                        static_cast<double>(txInLedger) / static_cast<double>(ageVal));
                }
                else
                {
                    observe("transaction_rate", 0.0);
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerStateTrackingGauge()
{
    // --- State tracking gauges ---
    stateTrackingGauge_ = core_.meter()->CreateDoubleObservableGauge(
        metric::stateTracking, "Node state and mode tracking");
    stateTrackingGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // State value: 0-4 from OperatingMode, 5=validating, 6=proposing.
                auto const mode = app.getOPs().getOperatingMode();
                auto stateValue = static_cast<double>(std::to_underlying(mode));

                // If FULL, refine using consensus info for validating/proposing.
                if (mode == OperatingMode::FULL)
                {
                    auto const info = app.getOPs().getConsensusInfo();
                    if (info.isMember("proposing") && info["proposing"].asBool())
                    {
                        stateValue = 6.0;
                    }
                    else if (info.isMember("validating") && info["validating"].asBool())
                    {
                        stateValue = 5.0;
                    }
                }
                observe("state_value", stateValue);

                // Time spent in the current operating mode, sourced from
                // NetworkOPs' StateAccounting via a lightweight accessor.
                auto const stateDurUs = app.getOPs().getServerStateDurationUs();
                observe(
                    "time_in_current_state_seconds", static_cast<double>(stateDurUs.count()) / 1e6);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerStorageDetailGauge()
{
    // --- Storage detail gauges ---
    // Reports the cumulative payload bytes handed to the NodeStore. See the
    // note at the observe() call below: this is logical bytes stored, not
    // on-disk file size, because no accessor for the latter exists. The label
    // value names it that way so it is not read as a filesystem measurement.
    storageDetailGauge_ =
        core_.meter()->CreateInt64ObservableGauge("storage_detail", "Storage detail metrics");
    storageDetailGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Cumulative payload bytes handed to the NodeStore. This is
                // not an on-disk file size: getStoreSize() sums the object
                // payloads this process has written, so it excludes NuDB's
                // keys, bucket padding and log, and it resets with the
                // process while the files do not. The value comes from
                // Database, not from any backend, so it carries no nudb_
                // prefix -- it reads the same on RocksDB.
                //
                // This is the same call node_written_bytes makes on the
                // nodestore_state gauge, so the two series are equal by
                // construction and their ratio is a constant 1.0. It is not
                // a write-amplification measure. Backend exposes no file
                // size accessor, so there is nothing better to read here;
                // computing one would mean stat()ing the backend's files
                // from the reader thread, which needs a new Backend method
                // rather than a change at this call site.
                observe(
                    "stored_object_bytes", static_cast<int64_t>(app.getNodeStore().getStoreSize()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerValidationAgreementGauge()
{
    // --- Validation agreement gauges ---
    // Reports rolling-window agreement percentages and counts from
    // ValidationTracker.  reconcile() is called at the start of the
    // callback so that pending ledger events are resolved before the
    // window data is read (the callback fires every ~10 s from the
    // PeriodicExportingMetricReader thread).
    validationAgreementGauge_ = core_.meter()->CreateDoubleObservableGauge(
        "validation_agreement", "Validation agreement percentages and counts (1h/24h windows)");
    validationAgreementGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;

            try
            {
                // Reconcile pending events before reading window data.
                self->core_.getValidationTracker().reconcile();

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                observe("agreement_pct_1h", self->core_.getValidationTracker().agreementPct1h());
                observe("agreement_pct_24h", self->core_.getValidationTracker().agreementPct24h());
                observe(
                    "agreements_1h",
                    static_cast<double>(self->core_.getValidationTracker().agreements1h()));
                observe(
                    "missed_1h",
                    static_cast<double>(self->core_.getValidationTracker().missed1h()));
                observe(
                    "agreements_24h",
                    static_cast<double>(self->core_.getValidationTracker().agreements24h()));
                observe(
                    "missed_24h",
                    static_cast<double>(self->core_.getValidationTracker().missed24h()));

                // 7-day window (matches external xrpl-validator-dashboard).
                observe("agreement_pct_7d", self->core_.getValidationTracker().agreementPct7d());
                observe(
                    "agreements_7d",
                    static_cast<double>(self->core_.getValidationTracker().agreements7d()));
                observe(
                    "missed_7d",
                    static_cast<double>(self->core_.getValidationTracker().missed7d()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerValidationTotalsCounters()
{
    // Lifetime validation agreement/miss counters.
    //
    // These are monotonic ObservableCounters rather than synchronous Counters:
    // a Prometheus _total must never decrease, but ValidationTracker's
    // NET totals are non-monotonic (a late repair decrements the net miss
    // count). We therefore observe the tracker's GROSS lifetime tallies, which
    // count each ledger once at first classification and are never adjusted on
    // repair (initial-classification semantics — see ValidationTracker). The
    // repaired/agreement view remains available from validation_agreement.
    //
    // reconcile() is called first so pending events are resolved before the
    // tallies are read; the callback fires every ~10 s from the
    // PeriodicExportingMetricReader thread.
    validationAgreementsObservable_ = core_.meter()->CreateInt64ObservableCounter(
        "validation_agreements_total",
        "Lifetime validations that initially agreed with network consensus");
    validationAgreementsObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                self->core_.getValidationTracker().reconcile();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(
                        static_cast<int64_t>(
                            self->core_.getValidationTracker().totalAgreementsEver()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);

    validationMissedObservable_ = core_.meter()->CreateInt64ObservableCounter(
        "validation_missed_total", "Lifetime validations that initially missed network consensus");
    validationMissedObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                self->core_.getValidationTracker().reconcile();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(
                        static_cast<int64_t>(self->core_.getValidationTracker().totalMissedEver()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
AppMetricGauges::registerUnlQuorumGauge()
{
    // --- Sync diagnostics: trusted UNL size against required quorum ---
    // validator_health already exports the quorum on its own; pairing it
    // with the trusted-key count in one instrument is what makes the
    // "can this node ever validate?" comparison a single query.
    unlQuorumGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::unlQuorum, "Trusted UNL key count vs required quorum");
    unlQuorumGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                auto& validators = app.getValidators();

                // Trusted master keys currently in effect. Zero means no
                // usable UNL: quorum can never be met.
                observe(
                    lval::unl_quorum::trustedKeys,
                    static_cast<int64_t>(validators.trustedKeyCount()));

                // Validations required for a ledger to be fully validated.
                // ValidatorList disables quorum by returning SIZE_MAX when too
                // many publishers are unavailable, so the raw value must not be
                // cast to int64_t: it would wrap to -1 and make the headroom
                // (trusted_keys - quorum) read positive on a node that can
                // never validate.
                auto const quorum = validators.quorum();
                // A disabled quorum therefore omits the series rather than
                // publishing a sentinel. Both consumers of this gauge are
                // timeseries panels
                // sharing one axis with trusted_keys, so a huge value would
                // flatten the key line to the baseline and hide the outage it
                // was meant to signal. The boolean below carries the state, and
                // a missing quorum line is itself the visible anomaly.
                bool const quorumDisabled = quorum == std::numeric_limits<std::size_t>::max();
                if (!quorumDisabled)
                    observe(lval::unl_quorum::quorum, static_cast<int64_t>(quorum));
                observe(lval::unl_quorum::quorumDisabled, quorumDisabled ? 1 : 0);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerClockSkewGauge()
{
    // --- Sync diagnostics: network close-time offset ---
    // A persistent offset shows the local clock disagrees with the
    // network, which delays consensus participation. server_info hides
    // this below 60 s, so export it continuously instead.
    clockSkewGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::clockCloseOffsetSeconds,
        "Network close time offset from the local clock, in seconds");
    clockSkewGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // Negative when the local clock runs ahead of the network.
                observe(
                    lval::clock_offset::offset,
                    static_cast<int64_t>(app.getTimeKeeper().closeOffset().count()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerSyncStateGauge()
{
    // --- Sync diagnostics: why a fresh node is not FULL yet ---
    // Four values otherwise visible only in a log line or in server_info
    // JSON. All four are cheap reads pulled on the ~10 s reader tick.
    syncStateGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::syncState, "Sync-pipeline health signals");
    syncStateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                auto& ops = app.getOPs();

                // Time to first FULL. Zero means the node has not synced yet,
                // which is exactly the case this signal exists to expose.
                observe(
                    lval::sync_state::initialFullDurationUs,
                    static_cast<int64_t>(ops.getInitialSyncDurationUs()));

                // 1 = still waiting for a full network ledger. While this is
                // set the node refuses transactions and cannot reach FULL.
                observe(lval::sync_state::networkLedgerGate, ops.isNeedNetworkLedger() ? 1 : 0);

                // Current main-loop stall duration; 0 when healthy.
                observe(
                    lval::sync_state::serverStallSeconds,
                    static_cast<int64_t>(app.getLoadManager().getCurrentStallSeconds()));

                // Distance from the network tip, floored at zero by the
                // accessor.
                observe(
                    lval::sync_state::ledgersBehind,
                    static_cast<int64_t>(ops.getLedgersBehindNetwork()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerStallEventsCounter()
{
    // --- Sync diagnostics: stall episode count ---
    // Observed rather than pushed: LoadManager's monitor thread already owns
    // the cumulative tally, and an ObservableCounter reads it each collection
    // cycle without threading a push path through the load-monitor loop.
    // Kept out of the sync_state gauge because a cumulative total needs
    // counter aggregation for rate() to be meaningful.
    stallEventsObservable_ = core_.meter()->CreateInt64ObservableCounter(
        metric::serverStallEventsTotal, "Total server main-loop stall episodes");
    stallEventsObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(
                        static_cast<int64_t>(self->app_.getLoadManager().getStallEventCount()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerSyncAcquireGauge()
{
    // --- Sync diagnostics: is ledger acquisition actually progressing? ---
    // Aggregated on purpose: a per-ledger label would add one series per ledger
    // acquired, which is unbounded. The per-ledger view lives on the
    // ledger.acquire span instead.
    syncAcquireGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::syncAcquire, "Aggregate ledger-acquire progress across in-flight acquires");
    syncAcquireGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, name}});
                };

                // One snapshot feeds all four series, so they are mutually
                // consistent rather than read at four different instants.
                auto const progress = app.getInboundLedgers().acquireProgress();

                // Flat and non-zero across ticks = this acquire will never
                // finish. Shrinking = slow but alive.
                observe(
                    lval::sync_acquire::missingStateNodesMax,
                    static_cast<int64_t>(progress.maxMissingStateNodes));
                observe(
                    lval::sync_acquire::missingTxNodesMax,
                    static_cast<int64_t>(progress.maxMissingTxNodes));

                // Deep stash = arriving data outpaces processing.
                observe(
                    lval::sync_acquire::receivedDataDepth,
                    static_cast<int64_t>(progress.receivedDataDepth));

                // Context for the three above: zero everywhere with zero
                // in-flight acquires is idle, not healthy.
                observe(lval::sync_acquire::inFlight, static_cast<int64_t>(progress.inFlight));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerCacheHitRateDetailGauge()
{
    // --- Sync diagnostics: SHAMap tree-node cache hit rate ---
    // The memory layer above the node store: a miss here is what causes a
    // node-store read, which the NuDB hit-ratio panel then measures.
    shamapCacheHitRateGauge_ = core_.meter()->CreateDoubleObservableGauge(
        metric::shamapCacheHitRate, "SHAMap tree-node cache hit rate (0.0-1.0), by cache");
    shamapCacheHitRateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                // TaggedCache::getHitRate() returns 0-100; normalize to 0.0-1.0
                // so the panel can use Grafana's "percentunit" unit, matching
                // how cache_metrics already reports its rates.
                auto const rate = app.getNodeFamily().getTreeNodeCache()->getHitRate() / 100.0F;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(
                        static_cast<double>(rate), {{label::metric, lval::shamap_cache::treenode}});
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerJobQueueSaturationGauge()
{
    // --- Sync diagnostics: is the whole worker pool exhausted? ---
    // Attributes a broad multi-stage slowdown to the pool once, instead of
    // leaving it to look like an independent fault in every subsystem whose
    // jobs are queued behind it.
    jobQueueSaturationGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::jobqSaturation,
        "Worker-pool saturation: tasks in flight, worker threads, jobs queued");
    jobQueueSaturationGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                // One reading feeds all three series so the ratio and the
                // backlog describe the same instant.
                auto const saturation = app.getJobQueue().getWorkerSaturation();
                observe(lval::jobq_saturation::runningTasks, saturation.runningTasks);

                // The denominator for the ratio panel. Derived at startup from
                // [workers], node size and hardware concurrency, so it cannot
                // be hardcoded in a dashboard.
                observe(lval::jobq_saturation::workerThreads, saturation.workerThreads);

                // Ratio at 1.0 alone is a busy pool; ratio at 1.0 with a
                // non-zero backlog is an exhausted one.
                observe(lval::jobq_saturation::totalWaiting, saturation.totalWaiting);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerPeerLedgerSupplyGauge()
{
    // --- Sync diagnostics: can the network even serve what I need? ---
    // Each peer advertises its ledger range and the connection caches it, but
    // nothing ever compared those ranges, so "no peer holds the sequence I
    // want" looked exactly like "my peers are slow" -- two faults with
    // completely different fixes.
    peerLedgerSupplyGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::peerLedgerSupply, "Peer coverage of the ledger sequence this node needs");
    peerLedgerSupplyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                // One pass over the peers, so all five series describe the
                // same peer set at the same instant.
                auto const supply = app.getOverlay().getPeerLedgerSupply(
                    app.getLedgerMaster().getValidLedgerIndex());

                // The denominator. Zero serving out of zero reporting is
                // silence; zero out of many is a real supply gap.
                observe(lval::peer_supply::peersReporting, supply.peersReporting);
                observe(lval::peer_supply::peersServingValidated, supply.peersServingValidated);

                // The verdict: zero here while peers_reporting is non-zero
                // means waiting cannot finish the sync.
                observe(lval::peer_supply::peersServingNext, supply.peersServingNext);

                // The window the peer set covers, so an operator can tell a
                // request for discarded history from one for an unreached tip.
                observe(lval::peer_supply::supplyMinSeq, supply.supplyMinSeq);
                observe(lval::peer_supply::supplyMaxSeq, supply.supplyMaxSeq);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerSlotCensusGauge()
{
    // --- Sync diagnostics: why can this node not get peers? ---
    // All nine numbers already exist inside PeerFinder; only the two active
    // counts are exported today, which cannot distinguish "not dialling",
    // "dialling and failing" and "nothing to dial".
    slotCensusGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::peerfinderSlotCensus, "PeerFinder slots, connection attempts and address caches");
    slotCensusGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                // One snapshot under one PeerFinder lock acquire, so occupancy
                // and capacity can be compared against each other.
                auto const census = app.getOverlay().getSlotCensus();

                observe(lval::slot_census::outActive, census.outActive);
                observe(lval::slot_census::outMax, census.outMax);
                observe(lval::slot_census::inActive, census.inActive);
                observe(lval::slot_census::inMax, census.inMax);

                // Dials in flight. Non-zero while out_active stays under
                // out_max is the "starting and never completing" case.
                observe(lval::slot_census::connecting, census.connecting);

                // fixed_active below fixed_configured names a configured peer
                // that cannot be reached.
                observe(lval::slot_census::fixedConfigured, census.fixedConfigured);
                observe(lval::slot_census::fixedActive, census.fixedActive);

                // Both at zero on a fresh node means there is nothing to dial.
                observe(lval::slot_census::bootcache, census.bootcache);
                observe(lval::slot_census::livecache, census.livecache);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerAmendmentBlockGauge()
{
    // --- Sync diagnostics: how long until this node stops validating? ---
    // The existing validator_health{metric="amendment_blocked"} reports the
    // terminal state, when nothing can be done. This is the window before it.
    amendmentBlockGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::amendmentBlock,
        "Amendment-block warning and seconds until the node stops validating");
    amendmentBlockGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                // An unsupported amendment has reached majority. Until now this
                // only surfaced as an admin-only server_info warning.
                observe(lval::amendment_block::warned, app.getOPs().isAmendmentWarned() ? 1 : 0);

                // Seconds until that amendment activates. -1 means nothing is
                // pending: a distinct healthy value rather than an absent
                // series, matching validator_health{metric="unl_expiry_days"}.
                std::int64_t secondsToBlock = -1;
                if (auto const expected = app.getAmendmentTable().firstUnsupportedExpected())
                {
                    // NetClock's representation is unsigned, so the difference
                    // is taken in int64_t: subtracting the time_points directly
                    // would wrap once the activation time has passed.
                    auto const expectedSecs =
                        static_cast<std::int64_t>(expected->time_since_epoch().count());
                    auto const nowSecs = static_cast<std::int64_t>(
                        app.getTimeKeeper().closeTime().time_since_epoch().count());

                    // Clamped at 0: past due means the block is imminent, not
                    // overdue by an amount worth charting.
                    secondsToBlock = std::max<std::int64_t>(expectedSecs - nowSecs, 0);
                }
                observe(lval::amendment_block::secondsToBlock, secondsToBlock);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
AppMetricGauges::registerLedgerQuorumPublishGauge()
{
    // --- Sync diagnostics: the quorum gate and the publish pipeline ---
    // The last two stages of a fresh sync, and the two whose failures are
    // hardest to see: a node can hold every ledger it needs and still never
    // declare one validated (quorum short), or validate correctly and never
    // publish (pipeline behind). The quorum shortfall is otherwise only a
    // trace log line; the publish lag is not derivable from any other signal.
    ledgerQuorumPublishGauge_ = core_.meter()->CreateInt64ObservableGauge(
        metric::ledgerQuorumPublish,
        "Pre-accept quorum gate and publish lag (tally vs quorum, first-validated, lag)");
    ledgerQuorumPublishGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<AppMetricGauges*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{label::metric, field}});
                };

                auto const& ledgerMaster = app.getLedgerMaster();

                // The pair that separates "slow" from "stuck". A tally climbing
                // toward the target will get there; a tally flat below it never
                // will, and no acquire or peer panel says which is happening.
                observe(
                    lval::quorum_publish::trustedValidationTally,
                    ledgerMaster.getTrustedValidationTally());

                // What the last gate evaluation actually required, as opposed to
                // unl_quorum{quorum} which is what the trusted list configures.
                // Already clamped against the SIZE_MAX "quorum disabled"
                // sentinel by LedgerMaster, so this never wraps negative.
                observe(lval::quorum_publish::quorumTarget, ledgerMaster.getQuorumTarget());

                // One-shot: a value is the time the first ledger took to pass
                // the gate, and 0 means it never has. Not a trend.
                observe(
                    lval::quorum_publish::timeToFirstValidatedUs,
                    ledgerMaster.getTimeToFirstValidatedUs());

                // Validated but not yet published. pubLedgerSeq_ was never
                // exported, so this gap was not derivable from any other series.
                observe(lval::quorum_publish::publishLag, ledgerMaster.getPublishLag());
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
