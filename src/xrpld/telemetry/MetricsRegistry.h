#pragma once

/**
 * Central OTel Metrics Registry for xrpld.
 *
 * Owns all OpenTelemetry metric instruments (counters, histograms,
 * observable gauges) that are NOT already covered by the beast::insight
 * StatsD pipeline. The instruments are created once at startup and polled
 * by the OTel PeriodicExportingMetricReader at a configurable interval
 * (default 10 s).
 *
 * When XRPL_ENABLE_TELEMETRY is **not** defined, this class compiles to a
 * lightweight no-op: every public method is an empty inline.
 *
 * Dependency / ownership diagram (ASCII):
 *
 * Application
 * |
 * +-- MetricsRegistry  (unique_ptr, created in setup(), started/stopped with telemetry)
 * |
 * +-- OTel MeterProvider  (owns reader + exporter)
 * |       |
 * |       +-- PeriodicExportingMetricReader
 * |       +-- OtlpHttpMetricExporter
 * |
 * +-- Counters / Histograms   (synchronous instruments)
 * |       +-- rpc_method_started_total
 * |       +-- rpc_method_finished_total
 * |       +-- rpc_method_errored_total
 * |       +-- rpc_method_us (Histogram)
 * |       +-- job_queued_total{job_type,handler}
 * |       +-- job_started_total{job_type,handler}
 * |       +-- job_finished_total{job_type,handler}
 * |       +-- job_queued_us{job_type,handler} (Histogram)
 * |       +-- job_running_us{job_type,handler} (Histogram)
 * |       +-- ledgers_closed_total
 * |       +-- validations_sent_total
 * |       +-- validations_checked_total
 * |       +-- state_changes_total
 * |       +-- ledger_history_mismatch_total{reason}
 * |       +-- txq_expired_total
 * |       +-- txq_dropped_total{reason}
 * |
 * +-- ValidationTracker  (validation agreement tracker)
 * |
 * +-- Observable Gauges  (async callbacks, polled by reader)
 * +-- Cache hit rates  (SLE, ledger, AL)
 * +-- TreeNode / FullBelow sizes
 * +-- TxQ metrics
 * +-- CountedObject counts
 * +-- Load factor breakdown
 * +-- NodeStore I/O gauges (totals, derived means, NuDB write queue,
 *                          ledger-acquisition stall counters)
 * +-- Server info (state, uptime, peers, consensus)
 * +-- Build info (version label)
 * +-- Complete ledger ranges (start/end pairs)
 * +-- DB metrics (storage KB, fetch rate)
 * +-- Validator health (amend blocked, UNL, quorum)
 * +-- Peer quality (P90 latency, version spread)
 * +-- Reduce-relay efficiency (selected/suppressed peers)
 * +-- Ledger economy (fees, reserves, age)
 * +-- State tracking (mode value, time in state)
 * +-- Storage detail (NuDB sizes)
 * +-- Validation agreement (1h/24h pct, counts)
 * +-- jq_trans_overflow_total (observed from Overlay)
 *
 * Control-flow for async gauges:
 *
 * PeriodicExportingMetricReader (background thread, 10 s tick)
 * |
 * v
 * OTel SDK invokes registered ObservableGauge callbacks
 * |
 * v
 * Each callback reads current value from Application services
 * (e.g. app.getTxQ().getMetrics(), app.getFeeTrack().getLoadFactor())
 * |
 * v
 * Result set is exported via OTLP/HTTP to the collector
 *
 * Control-flow for synchronous instruments:
 *
 * PerfLogImp::rpcStart/rpcEnd/jobQueue/jobStart/jobFinish
 * |
 * v
 * MetricsRegistry::recordRpc*(method, ...) / recordJob*(type, ...)
 * |
 * v
 * OTel Counter::Add() or Histogram::Record()
 * |
 * v
 * Periodically flushed by the MetricReader
 *
 * Example usage:
 *
 * @code
 * // In Application::setup(), after telemetry_ is created. Phase 1 needs
 * // only the config strings, so it runs immediately and the meter is live
 * // before any metric-emitting code:
 * metricsRegistry_ = std::make_unique<telemetry::MetricsRegistry>(
 * telemetry_->isEnabled(), app, journal);
 * // The endpoint comes from [telemetry] metrics_endpoint, read directly in
 * // Application::setup() rather than through Telemetry::Setup.
 * metricsRegistry_->start(endpoint, instanceId, nodeId);
 *
 * // Later in setup(), once overlay_ exists (the last of the services the
 * // callbacks read). Phase 2 registers the observable instruments:
 * metricsRegistry_->startAsyncGauges();
 *
 * // In PerfLogImp::rpcStart():
 * if (auto* mr = app_.getMetricsRegistry())
 * mr->recordRpcStarted("server_info");
 *
 * // In PerfLogImp::rpcEnd():
 * if (auto* mr = app_.getMetricsRegistry())
 * {
 * mr->recordRpcFinished("server_info", durationUs);
 * // or: mr->recordRpcErrored("server_info", durationUs);
 * }
 *
 * // In PerfLogImp::jobQueue(). The second argument is the addJob name;
 * // it is sanitised internally into the bounded `handler` label.
 * if (auto* mr = app_.getMetricsRegistry())
 * mr->recordJobQueued("ledgerData", "ProcessLData");
 *
 * // Shutdown:
 * metricsRegistry_->stop();
 * @endcode
 *
 * Caveats:
 * - The MetricsRegistry must be created AFTER the Telemetry object because
 * it reads isEnabled() to decide whether to initialize the OTel SDK.
 * - Observable gauge callbacks capture a reference to the Application; the
 * Application must outlive the MetricsRegistry (guaranteed because
 * MetricsRegistry is stopped before Application teardown).
 * - If a new CountedObject type is added, it will NOT appear automatically
 * in the object_count gauge; the callback iterates a fixed list.
 * - Adding a new synchronous instrument requires updating both the header
 * and the .cpp, then calling the new record*() method from the
 * instrumentation site.
 */

#ifdef XRPL_ENABLE_TELEMETRY
// The tracker is held and exposed only in this configuration, where the gauge
// callbacks that drain it exist.
#include <xrpld/telemetry/ValidationTracker.h>
#endif

#include <xrpl/beast/utility/Journal.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/unique_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>

// These three serve only the telemetry-only members below, so they are guarded
// like their uses: std::atomic by callbacksDetached_, std::function by the
// ObserveFn sink, std::shared_ptr by provider_.
#include <atomic>
#include <functional>
#include <memory>
#endif

namespace xrpl {

class ServiceRegistry;

// Defined in src/xrpld/app/ledger/AcquireStats.h. Forward-declared because
// only the gauge helpers in the .cpp touch it, and pulling an xrpld/app
// header in here would widen the dependencies of every file that includes
// this one.
class AcquireStats;

namespace node_store {
class Database;
}  // namespace node_store

namespace telemetry {

/**
 * Central OpenTelemetry metric registry.
 *
 * Owns all OTel instruments (counters, histograms, observable gauges)
 * that are not covered by the beast::insight StatsD pipeline. See the
 * file-level header comment above for the full dependency diagram,
 * gauge domain list, and usage examples.
 *
 * Class / collaborator diagram (ASCII):
 *
 * +-----------------+        +-------------------+
 * |   Application   |------->|  MetricsRegistry  |
 * +-----------------+        +-------------------+
 * |       |        |
 * creates/owns    v       v        v
 * +-----------+  +---------+  +-------------------+
 * | Meter     |  | Counter |  | ValidationTracker |
 * | Provider  |  | /Hist.  |  | (rolling windows) |
 * +-----------+  +---------+  +-------------------+
 * |
 * v
 * Periodic reader thread (~10 s)
 * -> ObservableGauge callbacks
 * -> OTLP/HTTP export
 *
 * @note Thread safety:
 * - The recordRpc, recordJob, and increment methods are invoked
 * from xrpld hot paths. OTel Counter::Add() and
 * Histogram::Record() are documented thread-safe, and
 * null-guard checks protect uninitialized instruments.
 * - ObservableGauge callbacks run on the OTel SDK background
 * reader thread (~10 s tick), concurrently with writers.
 * Each callback reads only lock-protected or atomic state
 * from Application services and wraps the body in a
 * catch-all try block so a transient failure never crashes
 * the reader thread.
 * - ValidationTracker protects its rolling windows internally.
 * - start(), startAsyncGauges() and stop() are NOT thread-safe
 * with each other and must all be called, in that order, from
 * the single Application lifecycle thread.
 *
 * @note Lifetime:
 * - Must be constructed AFTER telemetry_ (reads isEnabled()).
 * - Must be stopped BEFORE Application services it observes are
 * destroyed; the Application owns it via unique_ptr so normal
 * teardown guarantees this.
 *
 * @note Extending:
 * - Adding a new CountedObject type is auto-picked up by the
 * object_count gauge via iteration.
 * - Adding a new SYNCHRONOUS instrument (counter/histogram): prefer the
 * XRPL_METRIC_* call-site macros in MetricMacros.h -- no header/cpp
 * edit needed. Fall back to a dedicated member + init line + record
 * method (the pattern below) only when the metric needs to be read
 * back by other code (e.g. ValidationTracker-style accumulation) or
 * needs a custom histogram bucket View (see the histogram note in
 * MetricMacros.h).
 * - Adding a new OBSERVABLE gauge still requires eager central
 * registration -- pull-model instruments cannot be lazily created.
 */
class MetricsRegistry
{
public:
    /**
     * Construct a MetricsRegistry.
     *
     * @param enabled  Whether OTel metric export is active. When false,
     * all methods become no-ops.
     * @param app      Reference to the ServiceRegistry (Application) for
     * reading current metric values in gauge callbacks.
     * @param journal  Journal for log output.
     */
    MetricsRegistry(bool enabled, ServiceRegistry& app, beast::Journal journal);

    ~MetricsRegistry();

    /**
     * Non-copyable, non-movable.
     */
    MetricsRegistry(MetricsRegistry const&) = delete;
    MetricsRegistry&
    operator=(MetricsRegistry const&) = delete;

    /**
     * Initialize the OTel metrics pipeline and create the SYNCHRONOUS
     * instruments (counters and histograms).
     *
     * This is the first of two startup phases, and it can be called as soon
     * as the registry is constructed — which is what makes the meter live
     * before the first metric-emitting code runs. Startup RPCs and the first
     * consensus round both record metrics; a call-site metric macro caches
     * its instrument on first use, so a first use before the meter exists
     * latches null for the process lifetime.
     *
     * @note Invariant for future changes: this phase may create only
     * instruments with NO Application-reading callback. Push-model
     * counters and histograms qualify; app code records into them
     * when it is ready. Any observable instrument whose callback
     * reads an Application service belongs in `startAsyncGauges()`,
     * because registering it here arms the reader thread to invoke
     * that callback against a half-built Application. This applies
     * to observable COUNTERS as well as gauges.
     *
     * @param endpoint    OTLP/HTTP endpoint URL for metric export
     * (e.g. "http://localhost:4318/v1/metrics").
     * @param instanceId  Value for the service.instance.id resource
     * attribute. When non-empty, Prometheus metrics
     * carry a service_instance_id label for per-node
     * filtering.
     * @param nodeId      Value for the xrpl.node.id resource attribute (the
     * node's base58 public key). When non-empty, metrics
     * carry the same per-node key that traces do.
     */
    void
    start(
        std::string const& endpoint,
        std::string const& instanceId = {},
        std::string const& nodeId = {});

    /**
     * Register the pull-model observable instruments — the second startup
     * phase. Mostly ObservableGauges, plus the ObservableCounters whose
     * source value is already cumulative.
     *
     * A separate entry point from `start()` because the two halves have
     * different prerequisites. `start()` needs only config strings; these
     * callbacks read live Application services, so this half must run later.
     * Registering an observable also arms the reader thread to invoke its
     * callback on the next tick, which is why the separation is about ordering
     * and not just tidiness.
     *
     * @pre `start()` has already run (the meter exists). If it has not,
     * this is a logged no-op rather than a crash.
     * @pre Every service the callbacks read is constructed. The full set,
     * from the `app.get*()` calls in the registration helpers, is:
     * Overlay, OPs (NetworkOPs), LedgerMaster, OpenLedger, TxQ,
     * NodeStore, NodeFamily, Validators, AcceptedLedgerCache,
     * CachedSLEs, AcquireStats, TimeKeeper, RelationalDatabase,
     * InboundLedgers and FeeTrack.
     * All but Overlay already exist by the time `start()` is
     * callable, so Overlay is what fixes this call's position:
     * `ServiceRegistry::getOverlay()` `XRPL_ASSERT`s that
     * `overlay_` is non-null, and a reader-thread tick before the
     * overlay exists aborts a Debug build. The callbacks' catch-all
     * try block does not catch an assert. `getTxQ()` and
     * `getRelationalDatabase()` assert likewise.
     */
    void
    startAsyncGauges();

    /**
     * Detach all ObservableGauge callbacks so they no-op on the next
     * reader-thread tick.
     *
     * Must be called BEFORE any Application service that the callbacks
     * read (nodeStore, overlay, networkOPs, ledgerMaster, etc.) is
     * stopped. The flag is checked with acquire ordering at the top of
     * every callback; together with the release store here it
     * guarantees that once `detachCallbacks()` returns, no subsequent
     * callback invocation will dereference an already-stopped service.
     *
     * Idempotent, and safe to call multiple times: the flag is one-way,
     * only ever set to true, and nothing clears it. The actual
     * SDK-level provider shutdown still happens in `stop()`.
     *
     * @note One-way means this is a shutdown-only call. Calling it before
     * `startAsyncGauges()` does not "have no effect" — it
     * permanently disarms every gauge the later call registers, so
     * the instruments exist but never observe a value. Only call it
     * once the process is shutting down.
     */
    void
    detachCallbacks() noexcept;

    /**
     * Flush pending metrics and shut down the pipeline.
     *
     * @pre `detachCallbacks()` should have been called earlier in the
     * shutdown sequence; otherwise there is a narrow race between
     * the final reader-thread tick and the destruction of
     * Application services that the gauge callbacks read from.
     */
    void
    stop();

    /**
     * @return true if the registry is actively exporting metrics.
     */
    [[nodiscard]] bool
    isEnabled() const noexcept
    {
        return enabled_;
    }

    // -----------------------------------------------------------------
    // Synchronous instrument recording (called from PerfLog hot paths)
    // -----------------------------------------------------------------

    /**
     * Record an RPC method call start.
     * @param method  The RPC method name (e.g. "server_info").
     */
    void
    recordRpcStarted(std::string_view method);

    /**
     * Record an RPC method call completion.
     * @param method      The RPC method name.
     * @param durationUs  Execution time in microseconds.
     */
    void
    recordRpcFinished(std::string_view method, std::int64_t durationUs);

    /**
     * Record an RPC method call error.
     * @param method      The RPC method name.
     * @param durationUs  Execution time in microseconds.
     */
    void
    recordRpcErrored(std::string_view method, std::int64_t durationUs);

    /**
     * The `handler` label value used for any job name that fails the
     * sanitiser's all-ASCII-letters rule.
     *
     * Public because both sanitiseHandler() and its unit tests must agree
     * on the exact fallback token; a test asserting against its own copy
     * of the string would not catch a change made here.
     *
     * Declared as std::string_view rather than the `constexpr char k[]`
     * form used for instrument names in MetricsRegistry.cpp: this value is
     * *returned* by sanitiseHandler(), whose return type is
     * std::string_view, and is compared against std::string_view in tests.
     * Matching the type avoids array-to-pointer decay and a needless
     * strlen at each use.
     */
    static constexpr std::string_view kHandlerOther{"other"};

    /**
     * Reduce a job name to a bounded-cardinality `handler` label value.
     *
     * A job type can have several producers — both `RcvGetLedger` and
     * `RcvGetObjByHash` run as `JtLedgerReq` — so `job_type` alone cannot
     * attribute a latency spike to one of them. The job name can, but it
     * cannot be used raw: two names embed a ledger sequence number
     * (`"Pub" + std::to_string(seq)` in LedgerPersistence.cpp and
     * `"OB" + std::to_string(...)` in OrderBookDBImpl.cpp), which would
     * mint a fresh Prometheus series for every ledger.
     *
     * The rule is therefore: keep the name only when it is non-empty and
     * every character is an ASCII letter; otherwise return `"other"`.
     * Both dynamic names always contain digits, so they always fold to
     * `"other"`, while every all-letter name is a compile-time literal.
     * The label domain is thus a function of the literals present in the
     * source — 43 names plus `"other"` at the time of writing — and
     * cannot grow at runtime. A name added later that does not satisfy
     * the rule degrades to `"other"` rather than becoming unbounded,
     * which is a stronger guarantee than an allowlist that would have to
     * be maintained by hand.
     *
     * Defined inline so unit tests can call it without linking the rest
     * of the registry: in a telemetry-enabled build MetricsRegistry.cpp
     * is not compiled into the test binary, so an out-of-line definition
     * would be unreachable from tests. Being inline also makes it usable
     * regardless of XRPL_ENABLE_TELEMETRY.
     *
     * @param name  The job name as passed to JobQueue::addJob.
     * @return @p name when it is non-empty and all ASCII letters, else
     * kHandlerOther.
     *
     * @note Pure and reentrant: holds no state, performs no I/O, and is
     * safe to call concurrently from any thread.
     * @note The letter test is an explicit ASCII range check rather than
     * std::isalpha, which classifies by the current C locale. A
     * locale-dependent test could admit non-ASCII bytes and so
     * weaken the cardinality bound this function exists to provide.
     * @note When the name is kept, the returned view aliases @p name, so
     * it must not outlive the caller's buffer. The kHandlerOther case
     * returns a view of a static constant and is always valid.
     *
     * Example:
     * @code
     * sanitiseHandler("RcvGetObjByHash");  // "RcvGetObjByHash"
     * sanitiseHandler("Pub94512331");      // kHandlerOther  (digits)
     * sanitiseHandler("");                 // kHandlerOther  (empty)
     * @endcode
     */
    [[nodiscard]] static constexpr std::string_view
    sanitiseHandler(std::string_view name) noexcept
    {
        auto const isAsciiLetter = [](char const c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        };

        if (name.empty() || !std::ranges::all_of(name, isAsciiLetter))
            return kHandlerOther;

        return name;
    }

    /**
     * Divide a cumulative total by its count, optionally scaled, reporting
     * absence rather than zero when the count is zero.
     *
     * Every cumulative counter this registry publishes has a companion mean
     * that is only defined once the counter has moved. Reporting such a mean
     * as `0` is worse than not reporting it: `0` is a plausible reading, so a
     * dashboard draws a flat line at the bottom of the axis and an operator
     * concludes "reads are instant" when the truth is "nothing has been
     * read". Returning std::nullopt makes the caller skip the observation, so
     * the series has a genuine gap instead.
     *
     * @p scale exists because the gauge these feed is integral. A mean writer
     * depth of 1.4 truncates to 1, which is indistinguishable from a healthy
     * 1.0, so the caller scales by 100 and says so in the metric name.
     *
     * The arithmetic divides before scaling and scales the remainder
     * separately, so a long-lived node cannot overflow the product. Should
     * the result still exceed the gauge's range it saturates at
     * INT64_MAX rather than wrapping, because a wrapped gauge reads as a
     * sudden healthy-looking dip.
     *
     * Defined inline for the same reason as sanitiseHandler(): in a
     * telemetry-enabled build MetricsRegistry.cpp is not compiled into the
     * unit-test binary, so an out-of-line definition would be untestable.
     * constexpr so the cases below are checked at compile time.
     *
     * @param total  Cumulative numerator (e.g. summed microseconds).
     * @param count  Number of samples in @p total.
     * @param scale  Fixed-point multiplier applied to the quotient. Must be
     *               at least 1; 0 is meaningless and yields std::nullopt.
     * @return The scaled mean, or std::nullopt when @p count is 0 (mean
     *         undefined) or @p scale is 0.
     *
     * @note Pure and reentrant: holds no state and performs no I/O.
     * @note Truncates toward zero, like integer division. A mean of 9.9 us
     *       reads as 9 at @p scale 1 and as 990 at @p scale 100.
     *
     * Example:
     * @code
     * scaledMean(500, 4);        // 125   -- mean microseconds
     * scaledMean(7, 5, 100);     // 140   -- mean 1.4, scaled by 100
     * scaledMean(500, 0);        // nullopt -- no samples, so no mean
     * @endcode
     */
    [[nodiscard]] static constexpr std::optional<std::int64_t>
    scaledMean(std::uint64_t total, std::uint64_t count, std::uint64_t scale = 1) noexcept
    {
        if (count == 0 || scale == 0)
            return std::nullopt;

        constexpr auto kInt64Max =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

        auto const whole = total / count;
        if (whole > kInt64Max / scale)
            return static_cast<std::int64_t>(kInt64Max);

        // Scale the remainder too, so `scale` recovers the fractional digits
        // it exists for. Skipped when the product itself would overflow, at
        // which point it is worth less than one part in 2^63 of the result.
        auto const remainder = total % count;
        std::uint64_t fraction = 0;
        if (remainder <= std::numeric_limits<std::uint64_t>::max() / scale)
            fraction = remainder * scale / count;

        auto const scaled = whole * scale;
        if (scaled > kInt64Max - fraction)
            return static_cast<std::int64_t>(kInt64Max);

        return static_cast<std::int64_t>(scaled + fraction);
    }

    /**
     * Read one comma-separated segment of a complete-ledger range string.
     *
     * The producer is xrpl::to_string(RangeSet), documented in
     * xrpl/basics/RangeSet.h. It renders an interval as `first-last`, and an
     * interval whose first equals its last as a bare sequence number. A segment
     * with no dash is therefore a range of one ledger, not a malformed one.
     *
     * Defined inline for the same reason as sanitiseHandler(): in a
     * telemetry-enabled build MetricsRegistry.cpp is not compiled into the
     * unit-test binary, so an out-of-line definition would be untestable.
     *
     * @param segment  One segment, already split on ','. Leading or trailing
     * whitespace is rejected, because the producer emits none.
     * @return The inclusive first and last sequence of the range. The two are
     * equal for a single-ledger range. std::nullopt when @p segment is not
     * something this producer can emit.
     *
     * @note Pure and reentrant: holds no state, performs no I/O, and is safe to
     * call concurrently from any thread.
     * @note Reports malformed input instead of throwing, so one unreadable
     * segment costs its own range and not every range after it.
     * @note A reversed range such as "9-4" is returned as given. RangeSet
     * cannot emit one.
     *
     * Example:
     * @code
     * parseLedgerRange("32570-50000");  // {32570, 50000}
     * parseLedgerRange("5000");         // {5000, 5000}  -- one ledger
     * parseLedgerRange("5-");           // nullopt
     * @endcode
     */
    [[nodiscard]] static std::optional<std::pair<std::uint32_t, std::uint32_t>>
    parseLedgerRange(std::string_view segment) noexcept
    {
        auto const parseSeq = [](std::string_view text) -> std::optional<std::uint32_t> {
            std::uint32_t value = 0;
            auto const* const begin = text.data();
            auto const* const end = begin + text.size();
            auto const [ptr, ec] = std::from_chars(begin, end, value);

            // from_chars stops at the first character it cannot use, so the
            // whole segment counts as read only when it consumed all of it.
            if (ec != std::errc{} || ptr != end)
                return std::nullopt;

            return value;
        };

        auto const dash = segment.find('-');
        if (dash == std::string_view::npos)
        {
            auto const only = parseSeq(segment);
            if (!only)
                return std::nullopt;

            return std::pair{*only, *only};
        }

        auto const first = parseSeq(segment.substr(0, dash));
        auto const last = parseSeq(segment.substr(dash + 1));
        if (!first || !last)
            return std::nullopt;

        return std::pair{*first, *last};
    }

    /**
     * Record a job enqueued event.
     * @param jobType  The job type name (e.g. "ledgerData").
     * @param jobName  The addJob name, reduced to a bounded `handler`
     * label by sanitiseHandler(). Distinguishes producers
     * that share a job type.
     */
    void
    recordJobQueued(std::string_view jobType, std::string_view jobName);

    /**
     * Record a job start event.
     * @param jobType        The job type name.
     * @param jobName        The addJob name; see recordJobQueued().
     * @param queuedDurUs   Time the job spent waiting in the queue (us).
     */
    void
    recordJobStarted(std::string_view jobType, std::string_view jobName, std::int64_t queuedDurUs);

    /**
     * Record a job finish event.
     * @param jobType         The job type name.
     * @param jobName         The addJob name; see recordJobQueued().
     * @param runningDurUs   Execution time in microseconds.
     */
    void
    recordJobFinished(
        std::string_view jobType,
        std::string_view jobName,
        std::int64_t runningDurUs);

    // -----------------------------------------------------------------
    // External dashboard parity counters
    // -----------------------------------------------------------------

    /**
     * Increment the ledgers_closed_total counter.
     *
     * @note Currently has no callers: the ledgers_closed_total counter is
     * incremented at its consensus call site via the XRPL_METRIC_COUNTER_INC
     * macro (see MetricMacros.h). This method and its eagerly-created
     * counter are retained as a fallback and are slated for removal in a
     * separate cleanup once the macro path has proven out.
     */
    void
    incrementLedgersClosed();

    /**
     * Increment the validations_sent_total counter.
     * Called from RCLConsensus::Adaptor::validate() when a validation
     * is produced and broadcast.
     */
    void
    incrementValidationsSent();

    /**
     * Increment the validations_checked_total counter.
     * Called from NetworkOPs::recvValidation() when a network validation
     * is received and checked.
     */
    void
    incrementValidationsChecked();

    /**
     * Increment the state_changes_total counter.
     * Called from NetworkOPsImp::setMode() when the server operating mode
     * changes (e.g. CONNECTED -> SYNCING -> TRACKING -> FULL).
     */
    void
    incrementStateChanges();

    /**
     * Increment the ledger_history_mismatch_total counter for a reason.
     * Called from LedgerHistory::handleMismatch() once the mismatch has
     * been classified. The reason label turns fork diagnosis from a
     * log-grep into a queryable time series.
     * @param reason Classified mismatch cause (e.g. "prior_ledger",
     * "close_time", "consensus_txset", "same_txset_diff_result",
     * "unknown").
     */
    void
    incrementLedgerHistoryMismatch(std::string_view reason);

    /**
     * Increment the txq_expired_total counter.
     * Called from TxQ::processClosedLedger() for each queued transaction
     * removed because its LastLedgerSequence has passed — submitters who
     * under-bid the escalating fee and were never included.
     */
    void
    incrementTxqExpired();

    /**
     * Increment the txq_dropped_total{reason} counter.
     * Called from TxQ::apply() when a transaction is refused admission to
     * the queue (e.g. the queue is full). Distinct from expiry (already
     * queued) and from jq_trans_overflow (job queue, not TxQ).
     * @param reason Admission-control rejection cause (e.g. "queue_full").
     */
    void
    incrementTxqDropped(std::string_view reason);

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Access the validation agreement tracker.
     * Used by consensus and ledger hooks to record our validations and
     * network validations so the tracker can compute agreement percentages.
     *
     * Guarded, along with the tracker itself, because only the observable-gauge
     * callbacks read it and those exist only in this configuration. Recording
     * into it is not free: each call takes its lock and inserts an entry.
     * @return Reference to the internal ValidationTracker instance.
     */
    [[nodiscard]] ValidationTracker&
    getValidationTracker()
    {
        return validationTracker_;
    }

    /**
     * Access the shared OTel Meter for call-site instrument creation.
     * Used by the XRPL_METRIC_* macros (MetricMacros.h) so new synchronous
     * counters/histograms can be declared at their call site instead of as
     * MetricsRegistry members. Returns an empty (falsy) shared_ptr before
     * start() has run or when disabled.
     * @return The shared Meter, or empty if not yet started.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const noexcept
    {
        return meter_;
    }

    /**
     * Sink handed to the nodestore_state gauge helpers below.
     *
     * Every value they publish multiplexes onto the single `nodestore_state`
     * gauge through its `metric` label, so the helpers need no access to the
     * OTel observer result -- just somewhere to put a name and a number.
     */
    using ObserveFn = std::function<void(char const* name, std::int64_t value)>;

    /**
     * Observe the NodeStore I/O totals and the means derived from them.
     *
     * @param db       NodeStore to read the counters from.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeNodeStoreTotals(node_store::Database& db, ObserveFn const& observe);

    /**
     * Observe the backend write-path detail, when the backend measures it.
     *
     * Publishes nothing for a backend whose getWriteStats() is std::nullopt,
     * which is every backend except NuDB. Absent labels let a reader tell
     * "not measured" from "measured, and idle"; zeros would read as a
     * perfectly idle write path.
     *
     * @param db       NodeStore whose writable backend is sampled.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeWritePathDetail(node_store::Database const& db, ObserveFn const& observe);

    /**
     * Observe the ledger-acquisition progress and stall counters.
     *
     * @param stats    Process-wide acquisition counters.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeAcquireStats(AcquireStats const& stats, ObserveFn const& observe);

    /**
     * Observe the read queue depth and the read thread-pool counts.
     *
     * These four have no accessor on Database, so its JSON counters object
     * is still the only way to reach them.
     *
     * @param db       NodeStore to read the JSON counters from.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeReadQueue(node_store::Database& db, ObserveFn const& observe);
#endif

private:
    /**
     * Master enable flag; when false all methods are no-ops.
     */
    bool const enabled_;

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Tracks validation agreement between this node and the network.
     *
     * Guarded because reconcile() -- which resolves and then prunes recorded
     * events -- runs only from the observable-gauge callbacks. Recording
     * without it accumulates one entry per validated ledger, so the tracker
     * exists only where something drains it.
     */
    ValidationTracker validationTracker_;

    /**
     * Reference to Application services for gauge callbacks.
     * Only needed when OTel is compiled in, since observable gauge
     * callbacks live entirely inside the XRPL_ENABLE_TELEMETRY guard.
     */
    ServiceRegistry& app_;

    /**
     * Journal for logging.
     */
    beast::Journal const journal_;

    /**
     * Set by detachCallbacks() during shutdown so every ObservableGauge
     * callback returns early before reading Application services that
     * may already be stopped. Checked with memory_order_acquire at the
     * top of each callback to pair with the memory_order_release store
     * in detachCallbacks().
     */
    std::atomic<bool> callbacksDetached_{false};

    /**
     * The SDK MeterProvider that owns the export pipeline.
     */
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider_;

    /**
     * The Meter used to create all instruments.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter_;

    // --- Synchronous instruments (RPC) ---
    /**
     * Counter: rpc_method_started_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcStartedCounter_;
    /**
     * Counter: rpc_method_finished_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcFinishedCounter_;
    /**
     * Counter: rpc_method_errored_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcErroredCounter_;
    /**
     * Histogram: rpc_method_us{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        rpcDurationHistogram_;

    // --- Synchronous instruments (Job Queue) ---
    // All five carry handler="<sanitised addJob name>" in addition to
    // job_type, so producers that share a job type stay distinguishable.
    /**
     * Counter: job_queued_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobQueuedCounter_;
    /**
     * Counter: job_started_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobStartedCounter_;
    /**
     * Counter: job_finished_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobFinishedCounter_;
    /**
     * Histogram: job_queued_us{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobQueuedDurationHistogram_;
    /**
     * Histogram: job_running_us{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobRunningDurationHistogram_;

    // --- Observable gauges (registered via callbacks) ---
    // Handles are stored so we can remove callbacks on shutdown.
    /**
     * Observable gauges for cache hit rates and sizes.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        cacheHitRateGauge_;
    /**
     * Observable gauges for TxQ metrics.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> txqGauge_;
    /**
     * Observable gauges for counted object instances.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        objectCountGauge_;
    /**
     * Observable gauges for load factor breakdown.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> loadFactorGauge_;
    /**
     * Observable gauge multiplexing every NodeStore value onto one
     * instrument via its `metric` label: I/O totals, the read and write
     * means derived from them, the NuDB write-queue detail, and the
     * ledger-acquisition stall counters.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> nodeStoreGauge_;
    /**
     * Observable gauge for server-level health metrics (state, uptime, peers, etc.).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> serverInfoGauge_;
    /**
     * Observable gauge for build version info (label-based, value=1).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> buildInfoGauge_;
    /**
     * Observable gauge for complete ledger range start/end pairs.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        completeLedgersGauge_;
    /**
     * Observable gauge for database sizes and historical fetch rate.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> dbMetricsGauge_;

    // --- External dashboard parity gauges ---
    /**
     * Observable gauge for validator health indicators (amendment blocked,
     * UNL blocked, quorum, UNL expiry).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validatorHealthGauge_;
    /**
     * Observable gauge for peer network quality metrics (P90 latency,
     * insane peer count, version spread, upgrade recommendation).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerQualityGauge_;
    /**
     * Observable gauge for transaction reduce-relay efficiency (selected vs
     * suppressed peers, feature-disabled peers, missing-tx frequency).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        reduceRelayGauge_;
    /**
     * Observable gauge for ledger economy metrics (base fee, reserve,
     * reserve increment, ledger age).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerEconomyGauge_;
    /**
     * Observable gauge for node state tracking (operating mode value,
     * time in current state).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        stateTrackingGauge_;
    /**
     * Observable gauge for storage detail metrics (NuDB on-disk size).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        storageDetailGauge_;
    /**
     * Observable gauge for validation agreement metrics (1h/24h percentages
     * and counts from ValidationTracker).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementGauge_;

    // --- External dashboard parity counters ---
    /**
     * Counter: ledgers_closed_total — incremented each consensus round.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        ledgersClosedCounter_;
    /**
     * Counter: validations_sent_total — incremented when this node sends a validation.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsSentCounter_;
    /**
     * Counter: validations_checked_total — incremented for each network validation
     * received.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsCheckedCounter_;
    /**
     * Counter: state_changes_total — incremented on operating mode transitions.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        stateChangesCounter_;
    /**
     * ObservableCounter: jq_trans_overflow_total — observed from
     * Overlay::getJqTransOverflow() (cumulative overflow tally owned by the overlay).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jqTransOverflowObservable_;
    /**
     * Counter: ledger_history_mismatch_total{reason} — incremented per classified
     * built-vs-validated ledger mismatch.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        ledgerHistoryMismatchCounter_;
    /**
     * Counter: txq_expired_total — incremented per transaction expired out of the
     * transaction queue.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> txqExpiredCounter_;
    /**
     * Counter: txq_dropped_total{reason} — incremented when a transaction is refused
     * admission to the queue.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> txqDroppedCounter_;
    /**
     * ObservableCounter: validation_agreements_total — observed from
     * ValidationTracker::totalAgreementsEver() (monotonic gross lifetime
     * tally, initial-classification semantics).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementsObservable_;
    /**
     * ObservableCounter: validation_missed_total — observed from
     * ValidationTracker::totalMissedEver() (monotonic gross lifetime tally,
     * initial-classification semantics).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationMissedObservable_;

    /**
     * Build the OTLP/HTTP exporter, periodic reader, resource attributes and
     * histogram views, then create the MeterProvider and meter. Extracted
     * from start() to keep each function under the 80-line limit.
     *
     * @param endpoint OTLP/HTTP metrics endpoint URL.
     * @param instanceId service.instance.id resource attribute (may be empty).
     * @param nodeId xrpl.node.id resource attribute (may be empty).
     */
    void
    initExporterAndProvider(
        std::string const& endpoint,
        std::string const& instanceId,
        std::string const& nodeId);

    /**
     * Create the synchronous instruments (RPC and job-queue counters and
     * histograms, plus the external dashboard parity counters). Extracted
     * from start() to keep each function under the 80-line limit.
     */
    void
    initSyncInstruments();

    /**
     * Register all observable gauge callbacks with the OTel SDK.
     * Dispatches to one helper per metric domain so that each helper
     * stays well under the 80-line-per-function limit.
     *
     * Called only from `startAsyncGauges()`, which owns the enabled_ and
     * meter_ guards and the Application-state precondition.
     */
    void
    registerAsyncGauges();

    // Per-domain registration helpers for the async (pull-model) phase.
    // Each creates its instrument -- an ObservableGauge, or an
    // ObservableCounter where the underlying value is cumulative -- and
    // attaches a single callback that reads current values from Application
    // services. The callbacks run on the OTel
    // PeriodicExportingMetricReader background thread (~10 s tick).
    void
    registerJqTransOverflowCounter();  // gap-fill: overlay overflow total
    void
    registerCacheHitRateGauge();
    void
    registerTxqGauge();
    void
    registerObjectCountGauge();
    void
    registerLoadFactorGauge();
    void
    registerNodeStoreGauge();

    // The four nodestore_state helpers and their ObserveFn sink are public
    // (above), so a test can drive each one with a recording sink and assert
    // the exact `metric` label values it publishes. They read only their
    // arguments, so exposing them widens no state.

    void
    registerServerInfoGauge();
    void
    registerBuildInfoGauge();
    void
    registerCompleteLedgersGauge();
    void
    registerDbMetricsGauge();
    void
    registerValidatorHealthGauge();
    void
    registerPeerQualityGauge();
    void
    registerReduceRelayGauge();  // Reduce-relay efficiency
    void
    registerLedgerEconomyGauge();
    void
    registerStateTrackingGauge();
    void
    registerStorageDetailGauge();
    void
    registerValidationAgreementGauge();
    void
    registerValidationTotalsCounters();  // gap-fill: lifetime agree/miss _total
#endif                                   // XRPL_ENABLE_TELEMETRY
};

}  // namespace telemetry
}  // namespace xrpl
