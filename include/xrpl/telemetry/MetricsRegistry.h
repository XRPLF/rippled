#pragma once

/**
 * Central OTel metrics registry: the export pipeline and the instruments that
 * app code pushes values into.
 *
 * Owns the OpenTelemetry MeterProvider, the OTLP/HTTP exporter, the periodic
 * reader and every SYNCHRONOUS instrument (counters and histograms) that is
 * not already covered by the beast::insight StatsD pipeline. The instruments
 * are created once at startup and drained by the OTel
 * PeriodicExportingMetricReader at a fixed interval (10 s).
 *
 * When XRPL_ENABLE_TELEMETRY is **not** defined, this class compiles to a
 * lightweight no-op: every public method is an empty inline.
 *
 * Every caller reaches it through ServiceRegistry::getMetricsRegistry(), and
 * the XRPL_METRIC_* macros then create their own instruments from meter().
 *
 * Dependency / ownership diagram (ASCII):
 *
 * MetricsRegistry
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
 * +-- ValidationTracker  (rolling validation-agreement windows)
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
 * // In ApplicationImp's member-init list, right after telemetry_ and before
 * // every subsystem. The constructor builds the pipeline and every
 * // synchronous instrument, so no producer can exist before they do. The
 * // endpoint, the TLS settings and the resource identity come from
 * // [telemetry] and [network_id], read by Application.cpp rather than
 * // through Telemetry::Setup.
 * metricsRegistry_(std::make_unique<telemetry::MetricsRegistry>(
 *     telemetry_->isEnabled(), journal, options))
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
 * // Shutdown, before any observer of live server state is torn down.
 * // Idempotent, so run() and ~ApplicationImp both call it:
 * metricsRegistry_->stop();
 * @endcode
 *
 * Caveats:
 * - The MetricsRegistry must be created AFTER the Telemetry object because
 * it reads isEnabled() to decide whether to initialize the OTel SDK, and
 * BEFORE every subsystem that records a metric. Declaration order in
 * ApplicationImp is the guarantee; keep the member where it is.
 * - Adding a new synchronous instrument requires updating both the header
 * and the .cpp, then calling the new record*() method from the
 * instrumentation site. Prefer the XRPL_METRIC_* macros, which need
 * neither.
 */

#ifdef XRPL_ENABLE_TELEMETRY
// The tracker is held and exposed only in this configuration, where the
// observable-gauge callbacks that drain it exist.
#include <xrpl/telemetry/ValidationTracker.h>
#endif

#include <xrpl/beast/utility/Journal.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/unique_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>

// These two serve only the telemetry-only members below, so they are guarded
// like their uses: std::atomic by phase_, std::shared_ptr by provider_.
#include <atomic>
#include <memory>
#endif

namespace xrpl::telemetry {

/**
 * Central OpenTelemetry metric registry.
 *
 * Owns the metrics export pipeline and every push-model instrument that the
 * beast::insight StatsD pipeline does not already cover. See the file-level
 * header comment above for the instrument inventory and usage examples.
 *
 * Class / collaborator diagram (ASCII):
 *
 * MetricsRegistry
 * |
 * +-- creates/owns --> MeterProvider (SDK)
 * |                        |
 * |                        v
 * |                    reader thread (~10 s) -> OTLP/HTTP export
 * |
 * +-- creates/owns --> Counter and Histogram instruments
 * |
 * +-- holds ----------> ValidationTracker (rolling windows)
 *
 * @note Thread safety:
 * - The recordRpc, recordJob, and increment methods are invoked
 * from hot paths. OTel Counter::Add() and Histogram::Record()
 * are documented thread-safe, and null-guard checks protect
 * uninitialized instruments.
 * - recording() is a single acquire load and is read on every
 * XRPL_METRIC_* call site, from any thread.
 * - meter() may be called from any thread. The constructor is the
 * last writer of the handle it returns; stop() leaves it alone.
 * - ValidationTracker protects its rolling windows internally.
 * - The constructor, hasPipeline() and stop() are NOT thread-safe
 * with each other. All three read or write provider_, a plain
 * shared_ptr that stop() resets, so all three belong on the
 * single server lifecycle thread, in that order.
 *
 * @note Lifetime, in two phases (see Phase):
 * - Ready: the constructor built the pipeline and the synchronous
 * instruments. Runs in ApplicationImp's member-init list, so it precedes
 * every subsystem that could record.
 * - Stopped: stop() joined the reader thread. Runs before any observed
 * service stops, from run() and again from ~ApplicationImp for the
 * paths that never reach run().
 *
 * @note Extending:
 * - Adding a new SYNCHRONOUS instrument (counter/histogram): prefer the
 * XRPL_METRIC_* call-site macros in MetricMacros.h -- no header/cpp
 * edit needed. Fall back to a dedicated member + init line + record
 * method (the pattern below) only when the metric needs to be read
 * back by other code (e.g. ValidationTracker-style accumulation) or
 * needs a custom histogram bucket View (see the histogram note in
 * MetricMacros.h).
 * - An OBSERVABLE instrument does not belong here. Its callback reads live
 * server state, so it must be registered only once that state exists,
 * which is later than this object is built. Register it from the layer
 * that owns those callbacks.
 */
class MetricsRegistry
{
public:
    /**
     * Everything the constructor needs from config: where to export, how to
     * secure the connection, and the process identity stamped on the OTel
     * resource.
     *
     * The values come from the `[telemetry]` section plus `[network_id]`, read
     * by `makeMetricsRegistryOptions()` in `Application.cpp`. They must match
     * what `makeTelemetrySetup()` gives the trace pipeline, or one node reports
     * two identities and a dashboard filter shows half its series.
     *
     * A struct rather than ten positional parameters: seven of them are
     * strings, so a swapped pair would compile and silently stamp the wrong
     * label. Designated initializers name every value at the call site.
     *
     * @code
     * MetricsRegistry::Options opts{
     *     .endpoint = "http://localhost:4318/v1/metrics",
     *     .serviceName = "xrpld",
     *     .serviceVersion = build_info::getVersionString(),
     *     .serviceInstanceId = nodePublicKey,
     *     .nodeId = nodePublicKey,
     *     .networkId = 2};
     * MetricsRegistry registry(enabled, journal, opts);
     *
     * // Edge case: mutual TLS to a collector that requires it.
     * opts.useTls = true;
     * opts.tlsCaCertPath = "/etc/xrpld/otel-ca.pem";
     * opts.tlsClientCertPath = "/etc/xrpld/node.pem";
     * opts.tlsClientKeyPath = "/etc/xrpld/node.key";
     * MetricsRegistry secure(enabled, journal, opts);
     * @endcode
     *
     * @note Plain aggregate, no invariants enforced. `networkType` is not a
     * field: it is derived from @ref networkId inside the constructor so
     * the two can never disagree.
     */
    struct Options
    {
        /**
         * OTLP/HTTP endpoint URL for metric export, from
         * `[telemetry] metrics_endpoint`.
         */
        std::string endpoint;

        /**
         * service.name resource attribute, from `[telemetry] service_name`.
         * Stamped unconditionally, so an empty value here yields an empty
         * label rather than the SDK's `unknown_service` default. The caller
         * seeds it with `systemName()`.
         */
        std::string serviceName;

        /**
         * service.version resource attribute — the build's version string.
         * Left off the resource when empty.
         */
        std::string serviceVersion;

        /**
         * service.instance.id resource attribute, from
         * `[telemetry] service_instance_id` or the node's base58 public key.
         * Left off the resource when empty.
         */
        std::string serviceInstanceId;

        /**
         * xrpl.node.id resource attribute — the node's base58 public key,
         * which config cannot override. Left off the resource when empty.
         */
        std::string nodeId;

        /**
         * Network identifier from `[network_id]`. Stamped as xrpl.network.id,
         * and mapped to the xrpl.network.type label by `networkTypeFromId()`.
         */
        std::uint32_t networkId{0};

        /**
         * Whether the exporter connects to the collector over TLS. The three
         * paths below apply only when this is true.
         */
        bool useTls{false};

        /**
         * CA bundle used to verify the collector. Empty selects the system
         * CA store.
         */
        std::string tlsCaCertPath;

        /**
         * This node's client certificate, presented for mutual TLS. Empty
         * means one-way TLS.
         */
        std::string tlsClientCertPath;

        /**
         * Private key for @ref tlsClientCertPath.
         */
        std::string tlsClientKeyPath;
    };

    /**
     * Construct the registry and, when enabled, build the whole metrics
     * pipeline: OTLP exporter, periodic reader, MeterProvider and every
     * SYNCHRONOUS instrument (counters and histograms).
     *
     * Doing this in the constructor is what fixes the init order. The
     * Application declares its registry before every subsystem, so no
     * producer can exist before the instruments do. A failure to build the
     * pipeline is logged and leaves the registry a no-op; it never stops the
     * node.
     *
     * @note Invariant for future changes: the constructor may create only
     * instruments with NO callback of their own. Push-model counters
     * and histograms qualify; app code records into them when it is
     * ready. An instrument registered here is live immediately, and
     * the reader thread may invoke its callback before the rest of
     * the server is built, so any observable whose callback reads
     * live server state must be registered later, by the layer that
     * owns those callbacks. This applies to observable COUNTERS as
     * well as gauges.
     *
     * @param enabled  False makes every method a no-op (telemetry disabled).
     * @param journal  Log output.
     * @param options  Endpoint, TLS settings and resource identity, all read
     * from config by the caller. See @ref Options.
     */
    MetricsRegistry(bool enabled, beast::Journal journal, Options const& options);

    /**
     * Stops the pipeline if run() or ~ApplicationImp did not already.
     */
    ~MetricsRegistry();

    /**
     * Non-copyable, non-movable.
     */
    MetricsRegistry(MetricsRegistry const&) = delete;
    MetricsRegistry&
    operator=(MetricsRegistry const&) = delete;

    /**
     * Flush pending metrics and shut down the pipeline.
     *
     * Stores `Phase::Stopped` first so `recording()` reads false on every
     * later record call, then destroys the SDK provider. meter_ is not
     * touched: record threads may still be running, and the gate is what
     * keeps them off the dying pipeline. Idempotent.
     *
     * @pre Anything that observes live server state on the reader thread has
     * already been disarmed. Shutting the provider down joins that
     * thread, so a caller that has not disarmed its observers leaves a
     * narrow race between the final tick and the teardown of what those
     * observers read.
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

    /**
     * @return true when a record call is safe to run.
     *
     * False when the registry is disabled, or after stop() has torn down the
     * export pipeline. After stop() the SDK's SyncMetricStorage still holds a
     * raw pointer to an AggregationConfig owned by a destroyed View, so a
     * record with a first-seen attribute set would fire the factory lambda
     * and deref that dangling pointer. Every XRPL_METRIC_* macro reads this
     * once before touching an instrument.
     *
     * One acquire atomic load in the hot path.
     */
    [[nodiscard]] bool
    recording() const noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        return enabled_ && phase_.load(std::memory_order_acquire) != Phase::Stopped;
#else
        return enabled_;
#endif
    }

    /**
     * @return true when a real exporting pipeline exists, as opposed to the
     * no-op meter installed when the pipeline is disabled.
     *
     * A meter() check cannot answer this. The registry always hands out a
     * meter, so registering instruments on a no-op one would report success
     * and export nothing. Ask this before registering an observable
     * instrument.
     *
     * @note Not thread-safe against stop(), which drops the provider this
     * reads. Call it from the server lifecycle thread, like the constructor
     * and stop().
     */
    [[nodiscard]] bool
    hasPipeline() const noexcept;

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
     * Defined inline so it is available in a build without telemetry and
     * usable in a constant expression.
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
     * Defined inline for the same reason as sanitiseHandler(); constexpr so
     * the cases below are checked at compile time.
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
     * Defined inline for the same reason as sanitiseHandler().
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
     * MetricsRegistry members.
     *
     * Invariant: never empty while recording() is true. The constructor sets
     * it to the real meter, or to a no-op meter when the pipeline failed to
     * build, and never writes it again, so reads need no lock. After stop()
     * the meter's SDK context is gone; the macros gate on recording() first,
     * so no caller reaches it then.
     *
     * @return The shared Meter.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const noexcept
    {
        return meter_;
    }
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
     * Journal for logging.
     */
    beast::Journal const journal_;

    /**
     * Where the registry is in its life. Construction ends in `Ready`;
     * stop() moves to `Stopped`.
     *
     * After `Stopped` the SDK pipeline is gone. recording() reads false, so
     * no macro touches meter_ or a cached instrument.
     */
    enum class Phase { Ready, Stopped };

    /**
     * Current phase. Written from the server lifecycle thread with release
     * ordering; read from record threads via `recording()` with acquire
     * ordering, so no record starts once stop() has stored `Stopped`.
     */
    std::atomic<Phase> phase_{Phase::Ready};

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
     * Build the OTLP/HTTP exporter, periodic reader, resource attributes and
     * histogram views, then create the MeterProvider and meter. Extracted
     * from the constructor to keep each function under the 80-line limit.
     *
     * @param options Endpoint, TLS settings and resource identity, forwarded
     * unchanged from the constructor. See @ref Options.
     */
    void
    initExporterAndProvider(Options const& options);

    /**
     * Create the synchronous instruments (RPC and job-queue counters and
     * histograms, plus the external dashboard parity counters). Extracted
     * from the constructor to keep each function under the 80-line limit.
     */
    void
    initSyncInstruments();

    /**
     * Give up the pipeline after a build failure: drop the provider, hand
     * out a no-op meter so every call site still gets an instrument, and log
     * why. The registry stays enabled and inert for the process.
     *
     * @param reason What failed, for the log line.
     */
    void
    disablePipeline(std::string_view reason);
#endif  // XRPL_ENABLE_TELEMETRY
};

}  // namespace xrpl::telemetry
