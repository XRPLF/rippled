/**
 * @file OTelCollector.cpp
 * @brief OpenTelemetry Metrics SDK implementation of beast::insight::Collector.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined (via CMake
 * telemetry=ON). Maps beast::insight instruments to OTel SDK instruments
 * created on the GLOBAL Meter published by the telemetry module. This class
 * is an adapter only: it owns no export pipeline. The MeterProvider,
 * PeriodicExportingMetricReader, OTLP exporter and histogram view all live in
 * xrpl::telemetry::Telemetry.
 *
 * When XRPL_ENABLE_TELEMETRY is not defined, OTelCollector::New() returns
 * a NullCollector so the build succeeds without OTel dependencies.
 *
 * Data flow:
 *
 *   beast::insight callers
 *       |
 *       v
 *   OTelCounterImpl / OTelGaugeImpl / OTelEventImpl / OTelMeterImpl
 *       |                    |                |              |
 *       v                    v                v              v
 *   Counter<uint64_t>  ObservableGauge  Histogram<double>  Counter<uint64_t>
 *       |                    |                |              |
 *       +--------------------+----------------+--------------+
 *       |
 *       v
 *   GLOBAL Meter (from metrics::Provider::GetMeterProvider())
 *       |
 *       v
 *   telemetry-owned PeriodicMetricReader (1s) -> OTLP exporter -> Prometheus
 */

#ifdef XRPL_ENABLE_TELEMETRY
#include <xrpl/beast/insight/OTelCollector.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/MeterImpl.h>
#include <xrpl/beast/utility/Journal.h>

#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/unique_ptr.h>
#include <opentelemetry/nostd/variant.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace beast::insight {

namespace detail {

namespace metrics_api = opentelemetry::metrics;

class OTelCollectorImp;

//------------------------------------------------------------------------------

/**
 * @brief OTel-backed implementation of beast::insight::HookImpl.
 *
 * Stores a handler function that is invoked during each periodic
 * metric collection cycle. This mirrors the StatsDHookImpl pattern
 * where hooks are called at each 1-second timer tick, but here the
 * invocation is triggered by the OTel PeriodicMetricReader's
 * observable callback mechanism.
 */
class OTelHookImpl : public HookImpl
{
public:
    /**
     * @param handler  Callback invoked at each collection interval.
     * @param impl     Owning collector (prevents premature destruction).
     */
    OTelHookImpl(HandlerType handler, std::shared_ptr<OTelCollectorImp> impl);

    ~OTelHookImpl() override;

    OTelHookImpl&
    operator=(OTelHookImpl const&) = delete;

    /**
     * @brief Invoke the stored handler.
     *
     * Called by the collector during observable gauge callbacks to give
     * metric producers a chance to update gauge values before export.
     */
    void
    callHandler();

private:
    /**
     * Owning collector. Prevents collector destruction while hook alive.
     */
    std::shared_ptr<OTelCollectorImp> impl_;

    /**
     * User-supplied handler called at each collection interval.
     */
    HandlerType handler_;
};

//------------------------------------------------------------------------------

/**
 * @brief OTel-backed implementation of beast::insight::CounterImpl.
 *
 * Wraps an OTel Counter<uint64_t> instrument. Each increment() call
 * is forwarded directly to the OTel counter's Add() method. The
 * PeriodicMetricReader collects and exports the accumulated delta.
 *
 * Thread safety: OTel Counter::Add() is thread-safe by specification.
 */
class OTelCounterImpl : public CounterImpl
{
public:
    /**
     * @param name   Export-ready metric name, already run through
     *               formatName() by the collector: lowercase, with `.` and
     *               ` ` mapped to `_` (e.g. "rpc_size").
     * @param meter  OTel Meter used to create the counter instrument.
     */
    OTelCounterImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelCounterImpl() override = default;

    OTelCounterImpl&
    operator=(OTelCounterImpl const&) = delete;

    /**
     * @brief Add amount to the counter.
     * @param amount  Value to add (must be non-negative for OTel counters).
     */
    void
    increment(value_type amount) override;

private:
    /**
     * OTel synchronous counter instrument.
     */
    opentelemetry::nostd::unique_ptr<metrics_api::Counter<uint64_t>> counter_;
};

//------------------------------------------------------------------------------

/**
 * @brief OTel-backed implementation of beast::insight::EventImpl.
 *
 * Wraps an OTel Histogram<double> instrument. Each notify() call
 * records the duration in milliseconds. Uses explicit bucket boundaries
 * matching the SpanMetrics connector configuration:
 *   [1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000] ms
 *
 * Thread safety: OTel Histogram::Record() is thread-safe by specification.
 */
class OTelEventImpl : public EventImpl
{
public:
    /**
     * @param name   Export-ready metric name, already run through
     *               formatName() by the collector: lowercase, with `.` and
     *               ` ` mapped to `_` (e.g. "rpc_size").
     * @param meter  OTel Meter used to create the histogram instrument.
     */
    OTelEventImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelEventImpl() override = default;

    OTelEventImpl&
    operator=(OTelEventImpl const&) = delete;

    /**
     * @brief Record a duration measurement.
     * @param value  Duration in milliseconds.
     */
    void
    notify(value_type const& value) override;

private:
    /**
     * OTel histogram instrument for recording durations.
     */
    opentelemetry::nostd::unique_ptr<metrics_api::Histogram<double>> histogram_;
};

//------------------------------------------------------------------------------

/**
 * @brief OTel-backed implementation of beast::insight::GaugeImpl.
 *
 * Uses an atomic int64_t to store the current gauge value. The OTel SDK
 * reads this value via an ObservableGauge async callback during each
 * collection cycle. The set() and increment() methods update the
 * atomic value without blocking the collection thread.
 *
 * Design note: OTel gauges are asynchronous (observable) instruments.
 * The SDK calls a registered callback to read the value rather than
 * accepting push-style updates. We bridge the beast::insight push-style
 * API to OTel's pull-style API via the atomic variable.
 *
 * Thread safety: std::atomic operations are lock-free on all platforms.
 */
class OTelGaugeImpl : public GaugeImpl
{
public:
    /**
     * @param name       Export-ready metric name, already run through
     *                   formatName() by the collector: lowercase, with `.`
     *                   and ` ` mapped to `_`.
     * @param meter      OTel Meter used to create the observable gauge.
     * @param collector  Owning collector, used to invoke hooks before reads.
     */
    OTelGaugeImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter,
        std::shared_ptr<OTelCollectorImp> const& collector);

    ~OTelGaugeImpl() override;

    /**
     * @brief Set the gauge to an absolute value.
     * @param value  New gauge value.
     */
    void
    set(value_type value) override;

    /**
     * @brief Increment (or decrement) the gauge by a signed amount.
     *
     * Clamps the result to [0, INT64_MAX] to match StatsDGaugeImpl
     * behavior.
     *
     * @param amount  Signed amount to add to the current value.
     */
    void
    increment(difference_type amount) override;

    /**
     * @brief Return the current gauge value for the OTel callback.
     * @return The most recently set/incremented value.
     */
    int64_t
    currentValue() const;

    OTelGaugeImpl&
    operator=(OTelGaugeImpl const&) = delete;

    /**
     * Static callback registered with the OTel SDK observable gauge.
     */
    static void
    gaugeCallback(opentelemetry::metrics::ObserverResult result, void* state);

private:
    /**
     * Current gauge value, updated atomically by set()/increment().
     */
    std::atomic<int64_t> value_{0};

    /**
     * OTel observable gauge handle (prevents deregistration).
     */
    opentelemetry::nostd::shared_ptr<metrics_api::ObservableInstrument> gauge_;

    /**
     * Owning collector, used to invoke hooks before reading gauge values.
     */
    std::shared_ptr<OTelCollectorImp> collector_;
};

//------------------------------------------------------------------------------

/**
 * @brief OTel-backed implementation of beast::insight::MeterImpl.
 *
 * Wraps an OTel Counter<uint64_t> instrument. Semantically identical
 * to Counter but uses unsigned values. The OTel SDK accumulates deltas
 * and exports them via the PeriodicMetricReader.
 *
 * Note: In StatsD, Meter used the non-standard "|m" type which was
 * silently dropped by the OTel StatsD receiver. With native OTel,
 * Meter values are properly captured as counter deltas.
 *
 * Thread safety: OTel Counter::Add() is thread-safe by specification.
 */
class OTelMeterImpl : public MeterImpl
{
public:
    /**
     * @param name   Export-ready metric name, already run through
     *               formatName() by the collector: lowercase, with `.` and
     *               ` ` mapped to `_` (e.g. "rpc_size").
     * @param meter  OTel Meter used to create the counter instrument.
     */
    OTelMeterImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelMeterImpl() override = default;

    OTelMeterImpl&
    operator=(OTelMeterImpl const&) = delete;

    /**
     * @brief Add amount to the meter.
     * @param amount  Value to add (unsigned).
     */
    void
    increment(value_type amount) override;

private:
    /**
     * OTel synchronous counter instrument (unsigned).
     */
    opentelemetry::nostd::unique_ptr<metrics_api::Counter<uint64_t>> counter_;
};

//------------------------------------------------------------------------------

/**
 * @brief Main OTel Collector implementation (adapter over the global Meter).
 *
 * Obtains its Meter from the GLOBAL MeterProvider owned and published by the
 * telemetry module (xrpl::telemetry::Telemetry), rather than building its own
 * export pipeline. Implements all Collector::make_*() factory methods to
 * create OTel-backed instrument wrappers on that shared Meter.
 *
 * The metrics pipeline (MeterProvider + PeriodicExportingMetricReader + OTLP
 * HTTP exporter + histogram view) lives in the telemetry module. This class is
 * a thin adapter kept for beast::insight callers during deprecation.
 *
 * Class diagram:
 *
 *   +------------------+      +------------------+
 *   | Collector (ABC)  |<-----| OTelCollector    |
 *   +------------------+      | (public header)  |
 *          ^                  +------------------+
 *          |                          ^
 *   +------------------+             |
 *   | OTelCollectorImp |-------------+
 *   +------------------+
 *   | - journal_       |
 *   | - prefix_        |     +--------------------------+
 *   | - otelMeter_     |---->| GLOBAL MeterProvider     |
 *   | - hooks_[]       |     | (owned by Telemetry)     |
 *   | - gauges_[]      |     | + PeriodicReader         |
 *   +------------------+     | + OtlpHttpExporter       |
 *                            +--------------------------+
 *
 * Lifecycle:
 *   1. Constructor fetches the Meter from the global MeterProvider.
 *   2. make_*() methods create instruments on that shared Meter.
 *   3. The telemetry-owned PeriodicMetricReader collects every 1s, calling
 *      observable callbacks.
 *   4. Observable callbacks invoke hooks, read gauge atomics.
 *   5. Destructor only logs; the telemetry module owns pipeline teardown.
 *
 * Caveats:
 *   - Observable gauge callbacks run on the SDK's internal thread. Hook
 *     handlers must be thread-safe.
 *   - Metric names carry NO prefix. formatName() only lowercases the raw
 *     name and turns dots and spaces into underscores, to match
 *     StatsD->Prometheus naming conventions. The service is identified by
 *     the OTel resource (service.name), so prefix_ is kept for logging
 *     only and never affects an exported name.
 *   - The OTel Prometheus exporter appends "_total" to counters. The
 *     metric names we register do NOT include this suffix — Prometheus
 *     adds it automatically.
 *
 * Example usage:
 * @code
 *   auto collector = OTelCollector::New(
 *       "http://localhost:4318/v1/metrics", "xrpld",
 *       "node-1", "xrpld", "mainnet", journal);
 *   auto counter = collector->makeCounter("rpc.requests");
 *   counter.increment(1);
 *   // Metric "rpc_requests" exported via OTLP every 1s.
 * @endcode
 */
class OTelCollectorImp : public OTelCollector, public std::enable_shared_from_this<OTelCollectorImp>
{
public:
    /**
     * @brief Construct the OTel collector over the global MeterProvider.
     *
     * @param endpoint    OTLP/HTTP metrics endpoint URL, recorded in the
     *                    collector's startup log line. Export uses the
     *                    endpoint configured on the global telemetry
     *                    pipeline.
     * @param prefix      Label for the collector's startup log line
     *                    (e.g. "xrpld"). Exported metric names come from
     *                    formatName(); the service is identified by the
     *                    service.name resource attribute.
     * @param instanceId  Value for the service.instance.id resource attribute.
     *                    When empty, the attribute is omitted.
     * @param serviceName Value for the service.name resource attribute.
     *                    When empty, defaults to "xrpld".
     * @param networkType Value for the xrpl.network.type resource attribute.
     *                    When empty, the attribute is omitted.
     * @param journal     Journal for logging.
     */
    OTelCollectorImp(
        std::string const& endpoint,
        std::string prefix,
        std::string const& instanceId,
        std::string const& serviceName,
        std::string const& networkType,
        Journal journal);

    /**
     * @brief Shut down the MeterProvider, flushing any pending exports.
     */
    ~OTelCollectorImp() override;

    /**
     * @name Collector interface implementation
     */
    /** @{ */
    Hook
    makeHook(HookImpl::HandlerType const& handler) override;

    Counter
    makeCounter(std::string const& name) override;

    Event
    makeEvent(std::string const& name) override;

    Gauge
    makeGauge(std::string const& name) override;

    Meter
    makeMeter(std::string const& name) override;
    /** @} */

    /**
     * @name Hook management for observable callbacks
     */
    /** @{ */

    /**
     * @brief Register a hook for periodic invocation.
     * @param hook  Pointer to the hook to register.
     */
    void
    addHook(OTelHookImpl* hook);

    /**
     * @brief Unregister a hook.
     * @param hook  Pointer to the hook to unregister.
     */
    void
    removeHook(OTelHookImpl* hook);

    /**
     * @brief Invoke all registered hooks.
     *
     * Called from observable gauge callbacks before reading gauge values,
     * so that hook handlers have a chance to update metrics.
     */
    void
    callHooks();
    /** @} */

    /**
     * @name Gauge registration for observable callbacks
     */
    /** @{ */

    /**
     * @brief Register a gauge for observable callback reading.
     * @param gauge  Pointer to the gauge to register.
     */
    void
    addGauge(OTelGaugeImpl* gauge);

    /**
     * @brief Unregister a gauge.
     * @param gauge  Pointer to the gauge to unregister.
     */
    void
    removeGauge(OTelGaugeImpl* gauge);
    /** @} */

    /**
     * @brief Format a raw metric name for export.
     *
     * Lowercases the name and replaces dots and spaces with underscores to
     * match StatsD->Prometheus naming. Adds NO prefix: the service is
     * identified by the OTel resource (service.name).
     * Example: name="LedgerMaster.Validated_Ledger_Age"
     *   -> "ledgermaster_validated_ledger_age"
     *
     * @param name  Raw metric name from beast::insight callers.
     * @return Fully-qualified metric name.
     */
    static std::string
    formatName(std::string const& name);

private:
    /**
     * Journal for log output.
     */
    Journal journal_;

    /**
     * Configured metric-name prefix (e.g., "xrpld"). Log-only: it is
     * echoed in the startup log line and never applied to a metric name.
     */
    std::string prefix_;

    /**
     * OTel Meter used to create all instruments.
     */
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> otelMeter_;

    /**
     * Mutex protecting hook and gauge registration lists.
     */
    std::mutex mutex_;

    /**
     * Registered hooks called during observable callbacks.
     */
    std::vector<OTelHookImpl*> hooks_;

    /**
     * Registered gauges read during observable callbacks.
     */
    std::vector<OTelGaugeImpl*> gauges_;

    /**
     * @brief Debounce timestamp for callHooks().
     *
     * Multiple gauge callbacks fire during the same collection cycle.
     * This atomic tracks the last time hooks were invoked (ms since epoch).
     * Hooks are called at most once per 500ms window to avoid redundant
     * invocations while still ensuring fresh values each collection cycle.
     */
    std::atomic<int64_t> lastHookCallMs_{0};
};

//==============================================================================
// Implementation
//==============================================================================

//------------------------------------------------------------------------------
// OTelHookImpl
//------------------------------------------------------------------------------

OTelHookImpl::OTelHookImpl(HandlerType handler, std::shared_ptr<OTelCollectorImp> impl)
    : impl_(std::move(impl)), handler_(std::move(handler))
{
    impl_->addHook(this);
}

OTelHookImpl::~OTelHookImpl()
{
    impl_->removeHook(this);
}

void
OTelHookImpl::callHandler()
{
    handler_();
}

//------------------------------------------------------------------------------
// OTelCounterImpl
//------------------------------------------------------------------------------

OTelCounterImpl::OTelCounterImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : counter_(meter->CreateUInt64Counter(name))
{
}

void
OTelCounterImpl::increment(value_type amount)
{
    // OTel counters require non-negative values. beast::insight CounterImpl
    // uses int64_t, so clamp negative values to 0 and cast to uint64_t.
    if (amount > 0)
        counter_->Add(static_cast<uint64_t>(amount));
}

//------------------------------------------------------------------------------
// OTelEventImpl
//------------------------------------------------------------------------------

OTelEventImpl::OTelEventImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : histogram_(meter->CreateDoubleHistogram(name, "Duration in ms", "ms"))
{
}

void
OTelEventImpl::notify(value_type const& value)
{
    histogram_->Record(static_cast<double>(value.count()), opentelemetry::context::Context{});
}

//------------------------------------------------------------------------------
// OTelGaugeImpl
//------------------------------------------------------------------------------

OTelGaugeImpl::OTelGaugeImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter,
    std::shared_ptr<OTelCollectorImp> const& collector)
    : gauge_(meter->CreateInt64ObservableGauge(name)), collector_(collector)
{
    collector_->addGauge(this);
    gauge_->AddCallback(gaugeCallback, this);
}

void
OTelGaugeImpl::gaugeCallback(opentelemetry::metrics::ObserverResult result, void* state)
{
    auto* self = static_cast<OTelGaugeImpl*>(state);
    self->collector_->callHooks();
    if (auto intResult = opentelemetry::nostd::get_if<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
            &result))
    {
        (*intResult)->Observe(self->currentValue());
    }
}

OTelGaugeImpl::~OTelGaugeImpl()
{
    // RemoveCallback must run before this object is destroyed so the SDK
    // collection thread cannot invoke gaugeCallback on a dangling `this`.
    // The SDK's ObservableRegistry guards its callback list and the Observe()
    // pass with the same mutex, so RemoveCallback cannot return while a
    // callback for this instrument is in flight — removal is synchronous.
    gauge_->RemoveCallback(gaugeCallback, this);
    collector_->removeGauge(this);
}

void
OTelGaugeImpl::set(value_type value)
{
    value_.store(static_cast<int64_t>(value), std::memory_order_relaxed);
}

void
OTelGaugeImpl::increment(difference_type amount)
{
    // Use compare-exchange loop to safely clamp to [0, MAX].
    int64_t current = value_.load(std::memory_order_relaxed);
    int64_t desired = 0;
    do
    {
        desired = current + amount;
        // Clamp to 0 on underflow.
        desired = std::max(desired, int64_t{0});
    } while (!value_.compare_exchange_weak(current, desired, std::memory_order_relaxed));
}

int64_t
OTelGaugeImpl::currentValue() const
{
    return value_.load(std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// OTelMeterImpl
//------------------------------------------------------------------------------

OTelMeterImpl::OTelMeterImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : counter_(meter->CreateUInt64Counter(name))
{
}

void
OTelMeterImpl::increment(value_type amount)
{
    counter_->Add(amount);
}

//------------------------------------------------------------------------------
// OTelCollectorImp
//------------------------------------------------------------------------------

OTelCollectorImp::OTelCollectorImp(
    std::string const& endpoint,
    std::string prefix,
    std::string const& instanceId,
    std::string const& serviceName,
    std::string const& networkType,
    Journal journal)
    : journal_(journal), prefix_(std::move(prefix))
{
    // instanceId/serviceName/networkType are accepted but unused here: the
    // telemetry module owns the resource attributes for the shared metrics
    // pipeline, so setting them from this collector would have no effect.
    (void)instanceId;
    (void)serviceName;
    (void)networkType;

    if (journal_.info())
    {
        // endpoint is logged for diagnostics only: the global telemetry
        // pipeline owns the exporter that actually sends the metrics.
        journal_.info() << "OTelCollector starting: endpoint=" << endpoint << " prefix=" << prefix_;
    }

    // Fetch the Meter from the GLOBAL MeterProvider. The telemetry module
    // (xrpl::telemetry::Telemetry) builds the metrics pipeline (exporter,
    // periodic reader, histogram view, resource attributes) and registers it
    // via metrics::Provider::SetMeterProvider() during start(). beast metrics
    // ride that shared pipeline, so both direct-API and beast-sourced metrics
    // export under one resource identity.
    //
    // The name/version literals MUST match the telemetry module's kMeterName
    // ("xrpld") and kMeterVersion ("1.0.0"). They are written as literals (not
    // referenced from Telemetry.h) because beast/insight sits below the
    // telemetry module in the layering and cannot include its header.
    otelMeter_ = metrics_api::Provider::GetMeterProvider()->GetMeter(
        std::string{"xrpld"}, std::string{"1.0.0"});

    if (journal_.info())
    {
        journal_.info() << "OTelCollector started successfully";
    }
}

OTelCollectorImp::~OTelCollectorImp()
{
    if (journal_.info())
    {
        journal_.info() << "OTelCollector shutting down";
    }
    // No pipeline teardown here: the telemetry module owns the global
    // MeterProvider lifecycle (ForceFlush/Shutdown happen in Telemetry::stop()).
    if (journal_.info())
    {
        journal_.info() << "OTelCollector stopped";
    }
}

Hook
OTelCollectorImp::makeHook(HookImpl::HandlerType const& handler)
{
    return Hook(std::make_shared<OTelHookImpl>(handler, shared_from_this()));
}

Counter
OTelCollectorImp::makeCounter(std::string const& name)
{
    return Counter(std::make_shared<OTelCounterImpl>(formatName(name), otelMeter_));
}

Event
OTelCollectorImp::makeEvent(std::string const& name)
{
    return Event(std::make_shared<OTelEventImpl>(formatName(name), otelMeter_));
}

Gauge
OTelCollectorImp::makeGauge(std::string const& name)
{
    return Gauge(std::make_shared<OTelGaugeImpl>(formatName(name), otelMeter_, shared_from_this()));
}

Meter
OTelCollectorImp::makeMeter(std::string const& name)
{
    return Meter(std::make_shared<OTelMeterImpl>(formatName(name), otelMeter_));
}

void
OTelCollectorImp::addHook(OTelHookImpl* hook)
{
    std::scoped_lock const lock(mutex_);
    hooks_.push_back(hook);
}

void
OTelCollectorImp::removeHook(OTelHookImpl* hook)
{
    std::scoped_lock const lock(mutex_);
    std::erase(hooks_, hook);
}

void
OTelCollectorImp::callHooks()
{
    // Debounce: hooks run at most once per 500ms. Multiple gauge callbacks
    // fire during the same collection cycle — only the first one triggers
    // hooks. Subsequent callbacks within the window read already-updated
    // gauge values.
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    auto last = lastHookCallMs_.load(std::memory_order_acquire);
    if (now - last < 500)
        return;
    if (!lastHookCallMs_.compare_exchange_strong(last, now, std::memory_order_acq_rel))
        return;  // Another thread won the race.

    // Copy the hook list under the lock, then invoke handlers outside it.
    // A handler may drop the last reference to an OTelHookImpl, whose
    // destructor calls removeHook() and re-acquires mutex_; invoking
    // handlers while holding the (non-recursive) lock would deadlock.
    std::vector<OTelHookImpl*> hooks;
    {
        std::scoped_lock const lock(mutex_);
        hooks = hooks_;
    }
    for (auto* hook : hooks)
        hook->callHandler();
}

void
OTelCollectorImp::addGauge(OTelGaugeImpl* gauge)
{
    std::scoped_lock const lock(mutex_);
    gauges_.push_back(gauge);
}

void
OTelCollectorImp::removeGauge(OTelGaugeImpl* gauge)
{
    std::scoped_lock const lock(mutex_);
    std::erase(gauges_, gauge);
}

std::string
OTelCollectorImp::formatName(std::string const& name)
{
    // Produce a lowercase, Prometheus-compatible metric name: dots and
    // spaces become underscores. Service identity travels in the
    // service.name resource attribute, not in the metric name.
    std::string result;
    result.reserve(name.size());
    for (char const c : name)
    {
        if (c == '.' || c == ' ')
        {
            result += '_';
        }
        else
        {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

}  // namespace detail

//------------------------------------------------------------------------------

std::shared_ptr<Collector>
OTelCollector::New(
    std::string const& endpoint,
    std::string const& prefix,
    std::string const& instanceId,
    std::string const& serviceName,
    std::string const& networkType,
    Journal journal)
{
    return std::make_shared<detail::OTelCollectorImp>(
        endpoint, prefix, instanceId, serviceName, networkType, journal);
}

}  // namespace beast::insight

#else  // !XRPL_ENABLE_TELEMETRY

// When telemetry is disabled at compile time, OTelCollector::New()
// returns a NullCollector so callers do not need conditional logic.

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/insight/OTelCollector.h>
#include <xrpl/beast/utility/Journal.h>

namespace beast::insight {

std::shared_ptr<Collector>
OTelCollector::New(
    std::string const& /* endpoint */,
    std::string const& /* prefix */,
    std::string const& /* instanceId */,
    std::string const& /* serviceName */,
    std::string const& /* networkType */,
    Journal /* journal */)
{
    return NullCollector::make();
}

}  // namespace beast::insight

#endif  // XRPL_ENABLE_TELEMETRY
