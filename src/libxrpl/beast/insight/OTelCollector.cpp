/**
 * @file OTelCollector.cpp
 * @brief OpenTelemetry Metrics SDK implementation of beast::insight::Collector.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined (via CMake
 * telemetry=ON). Maps beast::insight instruments to OTel SDK instruments
 * and exports them via OTLP/HTTP using a PeriodicMetricReader.
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
 *   PeriodicMetricReader (1s interval)
 *       |
 *       v
 *   OtlpHttpMetricExporter -> OTel Collector -> Prometheus
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/beast/insight/OTelCollector.h>

#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/MeterImpl.h>
#include <xrpl/beast/utility/Journal.h>

#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace beast {
namespace insight {

namespace detail {

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace otlp_http = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

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
    OTelHookImpl(HandlerType const& handler, std::shared_ptr<OTelCollectorImp> const& impl);

    ~OTelHookImpl() override;

    /**
     * @brief Invoke the stored handler.
     *
     * Called by the collector during observable gauge callbacks to give
     * metric producers a chance to update gauge values before export.
     */
    void
    callHandler();

private:
    OTelHookImpl&
    operator=(OTelHookImpl const&);

    /** Owning collector. Prevents collector destruction while hook alive. */
    std::shared_ptr<OTelCollectorImp> m_impl;

    /** User-supplied handler called at each collection interval. */
    HandlerType m_handler;
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
     * @param name   Fully-qualified metric name (prefix.group.name).
     * @param meter  OTel Meter used to create the counter instrument.
     */
    OTelCounterImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelCounterImpl() override = default;

    /**
     * @brief Add amount to the counter.
     * @param amount  Value to add (must be non-negative for OTel counters).
     */
    void
    increment(value_type amount) override;

private:
    OTelCounterImpl&
    operator=(OTelCounterImpl const&);

    /** OTel synchronous counter instrument. */
    opentelemetry::nostd::unique_ptr<metrics_api::Counter<uint64_t>> m_counter;
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
     * @param name   Fully-qualified metric name (prefix.group.name).
     * @param meter  OTel Meter used to create the histogram instrument.
     */
    OTelEventImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelEventImpl() override = default;

    /**
     * @brief Record a duration measurement.
     * @param value  Duration in milliseconds.
     */
    void
    notify(value_type const& value) override;

private:
    OTelEventImpl&
    operator=(OTelEventImpl const&);

    /** OTel histogram instrument for recording durations. */
    opentelemetry::nostd::unique_ptr<metrics_api::Histogram<double>> m_histogram;
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
     * @param name       Fully-qualified metric name (prefix.group.name).
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

    /** Static callback registered with the OTel SDK observable gauge. */
    static void
    gaugeCallback(opentelemetry::metrics::ObserverResult result, void* state);

private:
    OTelGaugeImpl&
    operator=(OTelGaugeImpl const&);

    /** Current gauge value, updated atomically by set()/increment(). */
    std::atomic<int64_t> m_value{0};

    /** OTel observable gauge handle (prevents deregistration). */
    opentelemetry::nostd::shared_ptr<metrics_api::ObservableInstrument> m_gauge;

    /** Owning collector, used to invoke hooks before reading gauge values. */
    std::shared_ptr<OTelCollectorImp> m_collector;
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
     * @param name   Fully-qualified metric name (prefix.group.name).
     * @param meter  OTel Meter used to create the counter instrument.
     */
    OTelMeterImpl(
        std::string const& name,
        opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter);

    ~OTelMeterImpl() override = default;

    /**
     * @brief Add amount to the meter.
     * @param amount  Value to add (unsigned).
     */
    void
    increment(value_type amount) override;

private:
    OTelMeterImpl&
    operator=(OTelMeterImpl const&);

    /** OTel synchronous counter instrument (unsigned). */
    opentelemetry::nostd::unique_ptr<metrics_api::Counter<uint64_t>> m_counter;
};

//------------------------------------------------------------------------------

/**
 * @brief Main OTel Collector implementation.
 *
 * Creates an OTel MeterProvider with a PeriodicMetricReader that
 * exports metrics via OTLP/HTTP at 1-second intervals. Implements
 * all Collector::make_*() factory methods to create OTel-backed
 * instrument wrappers.
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
 *   | - m_journal      |
 *   | - m_prefix       |
 *   | - m_provider     |     +---------------------+
 *   | - m_otelMeter    |---->| OTel MeterProvider   |
 *   | - m_hooks[]      |     | + PeriodicReader     |
 *   | - m_gauges[]     |     | + OtlpHttpExporter   |
 *   +------------------+     +---------------------+
 *
 * Lifecycle:
 *   1. Constructor creates MeterProvider + exporter pipeline.
 *   2. make_*() methods create instruments registered with the provider.
 *   3. PeriodicMetricReader collects every 1s, calling observable callbacks.
 *   4. Observable callbacks invoke hooks, read gauge atomics.
 *   5. Destructor shuts down MeterProvider (flushes pending exports).
 *
 * Caveats:
 *   - Observable gauge callbacks run on the SDK's internal thread. Hook
 *     handlers must be thread-safe.
 *   - Metric names are formed as "prefix_name" with dots replaced by
 *     underscores to match StatsD->Prometheus naming conventions.
 *   - The OTel Prometheus exporter appends "_total" to counters. The
 *     metric names we register do NOT include this suffix — Prometheus
 *     adds it automatically.
 *
 * Example usage:
 * @code
 *   auto collector = OTelCollector::New(
 *       "http://localhost:4318/v1/metrics", "xrpld", journal);
 *   auto counter = collector->make_counter("rpc.requests");
 *   counter.increment(1);
 *   // Metric "xrpld_rpc_requests" exported via OTLP every 1s.
 * @endcode
 */
class OTelCollectorImp : public OTelCollector, public std::enable_shared_from_this<OTelCollectorImp>
{
public:
    /**
     * @brief Construct the OTel collector and initialize the export pipeline.
     *
     * @param endpoint    OTLP/HTTP metrics endpoint URL.
     * @param prefix      Prefix for all metric names.
     * @param instanceId  Value for the service.instance.id resource attribute.
     *                    When empty, the attribute is omitted.
     * @param journal     Journal for logging.
     */
    OTelCollectorImp(
        std::string const& endpoint,
        std::string const& prefix,
        std::string const& instanceId,
        Journal journal);

    /**
     * @brief Shut down the MeterProvider, flushing any pending exports.
     */
    ~OTelCollectorImp() override;

    /** @name Collector interface implementation */
    /** @{ */
    Hook
    make_hook(HookImpl::HandlerType const& handler) override;

    Counter
    make_counter(std::string const& name) override;

    Event
    make_event(std::string const& name) override;

    Gauge
    make_gauge(std::string const& name) override;

    Meter
    make_meter(std::string const& name) override;
    /** @} */

    /** @name Hook management for observable callbacks */
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

    /** @name Gauge registration for observable callbacks */
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
     * @brief Get the OTel Meter instance for creating instruments.
     * @return Shared pointer to the OTel Meter.
     */
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const&
    otelMeter() const;

    /**
     * @brief Format a metric name with the configured prefix.
     *
     * Replaces dots with underscores to match StatsD->Prometheus naming.
     * Example: prefix="xrpld", name="LedgerMaster.Validated_Ledger_Age"
     *   -> "xrpld_LedgerMaster_Validated_Ledger_Age"
     *
     * @param name  Raw metric name from beast::insight callers.
     * @return Fully-qualified metric name.
     */
    std::string
    formatName(std::string const& name) const;

private:
    /** Journal for log output. */
    Journal m_journal;

    /** Prefix for all metric names (e.g., "xrpld"). */
    std::string m_prefix;

    /** OTel SDK MeterProvider owning the export pipeline. RAII lifecycle. */
    std::shared_ptr<metrics_sdk::MeterProvider> m_provider;

    /** OTel Meter used to create all instruments. */
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> m_otelMeter;

    /** Mutex protecting hook and gauge registration lists. */
    std::mutex m_mutex;

    /** Registered hooks called during observable callbacks. */
    std::vector<OTelHookImpl*> m_hooks;

    /** Registered gauges read during observable callbacks. */
    std::vector<OTelGaugeImpl*> m_gauges;

    /**
     * @brief Debounce timestamp for callHooks().
     *
     * Multiple gauge callbacks fire during the same collection cycle.
     * This atomic tracks the last time hooks were invoked (ms since epoch).
     * Hooks are called at most once per 500ms window to avoid redundant
     * invocations while still ensuring fresh values each collection cycle.
     */
    std::atomic<int64_t> m_lastHookCallMs{0};
};

//==============================================================================
// Implementation
//==============================================================================

//------------------------------------------------------------------------------
// OTelHookImpl
//------------------------------------------------------------------------------

OTelHookImpl::OTelHookImpl(
    HandlerType const& handler,
    std::shared_ptr<OTelCollectorImp> const& impl)
    : m_impl(impl), m_handler(handler)
{
    m_impl->addHook(this);
}

OTelHookImpl::~OTelHookImpl()
{
    m_impl->removeHook(this);
}

void
OTelHookImpl::callHandler()
{
    m_handler();
}

//------------------------------------------------------------------------------
// OTelCounterImpl
//------------------------------------------------------------------------------

OTelCounterImpl::OTelCounterImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : m_counter(meter->CreateUInt64Counter(name))
{
}

void
OTelCounterImpl::increment(value_type amount)
{
    // OTel counters require non-negative values. beast::insight CounterImpl
    // uses int64_t, so clamp negative values to 0 and cast to uint64_t.
    if (amount > 0)
        m_counter->Add(static_cast<uint64_t>(amount));
}

//------------------------------------------------------------------------------
// OTelEventImpl
//------------------------------------------------------------------------------

OTelEventImpl::OTelEventImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : m_histogram(meter->CreateDoubleHistogram(name, "Duration in ms", "ms"))
{
}

void
OTelEventImpl::notify(value_type const& value)
{
    m_histogram->Record(static_cast<double>(value.count()), opentelemetry::context::Context{});
}

//------------------------------------------------------------------------------
// OTelGaugeImpl
//------------------------------------------------------------------------------

OTelGaugeImpl::OTelGaugeImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter,
    std::shared_ptr<OTelCollectorImp> const& collector)
    : m_gauge(meter->CreateInt64ObservableGauge(name)), m_collector(collector)
{
    m_collector->addGauge(this);
    m_gauge->AddCallback(gaugeCallback, this);
}

void
OTelGaugeImpl::gaugeCallback(opentelemetry::metrics::ObserverResult result, void* state)
{
    auto* self = static_cast<OTelGaugeImpl*>(state);
    self->m_collector->callHooks();
    if (auto intResult = opentelemetry::nostd::get_if<
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<int64_t>>>(
            &result))
    {
        (*intResult)->Observe(self->currentValue());
    }
}

OTelGaugeImpl::~OTelGaugeImpl()
{
    m_gauge->RemoveCallback(gaugeCallback, this);
    m_collector->removeGauge(this);
}

void
OTelGaugeImpl::set(value_type value)
{
    m_value.store(static_cast<int64_t>(value), std::memory_order_relaxed);
}

void
OTelGaugeImpl::increment(difference_type amount)
{
    // Use compare-exchange loop to safely clamp to [0, MAX].
    int64_t current = m_value.load(std::memory_order_relaxed);
    int64_t desired;
    do
    {
        desired = current + amount;
        // Clamp to 0 on underflow.
        if (desired < 0)
            desired = 0;
    } while (!m_value.compare_exchange_weak(current, desired, std::memory_order_relaxed));
}

int64_t
OTelGaugeImpl::currentValue() const
{
    return m_value.load(std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// OTelMeterImpl
//------------------------------------------------------------------------------

OTelMeterImpl::OTelMeterImpl(
    std::string const& name,
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> const& meter)
    : m_counter(meter->CreateUInt64Counter(name))
{
}

void
OTelMeterImpl::increment(value_type amount)
{
    m_counter->Add(amount);
}

//------------------------------------------------------------------------------
// OTelCollectorImp
//------------------------------------------------------------------------------

OTelCollectorImp::OTelCollectorImp(
    std::string const& endpoint,
    std::string const& prefix,
    std::string const& instanceId,
    Journal journal)
    : m_journal(journal), m_prefix(prefix)
{
    if (m_journal.info())
        m_journal.info() << "OTelCollector starting: endpoint=" << endpoint
                         << " prefix=" << m_prefix;

    // Configure OTLP HTTP metric exporter.
    otlp_http::OtlpHttpMetricExporterOptions exporterOpts;
    exporterOpts.url = endpoint;

    auto exporter = otlp_http::OtlpHttpMetricExporterFactory::Create(exporterOpts);

    // Configure periodic metric reader (1-second export interval).
    metrics_sdk::PeriodicExportingMetricReaderOptions readerOpts;
    readerOpts.export_interval_millis = std::chrono::milliseconds(1000);
    readerOpts.export_timeout_millis = std::chrono::milliseconds(500);

    auto reader =
        metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), readerOpts);

    // Configure resource attributes matching the trace exporter.
    // Include service.instance.id when provided so Prometheus
    // exported_instance labels distinguish multi-node deployments.
    resource::ResourceAttributes attrs;
    attrs[resource::SemanticConventions::kServiceName] = "xrpld";
    if (!instanceId.empty())
        attrs[resource::SemanticConventions::kServiceInstanceId] = instanceId;
    auto resourceAttrs = resource::Resource::Create(attrs);

    // Create MeterProvider with resource, then attach the metric reader.
    m_provider = metrics_sdk::MeterProviderFactory::Create(
        std::make_unique<metrics_sdk::ViewRegistry>(), resourceAttrs);
    m_provider->AddMetricReader(std::move(reader));

    // Configure histogram bucket boundaries for Event instruments.
    // These match the SpanMetrics connector buckets for consistency.
    auto histogramSelector = metrics_sdk::InstrumentSelectorFactory::Create(
        metrics_sdk::InstrumentType::kHistogram, "*", "ms");
    auto meterSelector = metrics_sdk::MeterSelectorFactory::Create("xrpld_metrics", "", "");
    auto histogramConfig = std::make_shared<metrics_sdk::HistogramAggregationConfig>();
    histogramConfig->boundaries_ =
        std::vector<double>{1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 5000.0};
    auto histogramView = metrics_sdk::ViewFactory::Create(
        "default_histogram",
        "Default histogram view with SpanMetrics-compatible buckets",
        "ms",
        metrics_sdk::AggregationType::kHistogram,
        std::move(histogramConfig));

    m_provider->AddView(
        std::move(histogramSelector), std::move(meterSelector), std::move(histogramView));

    // Create the OTel Meter for creating instruments.
    m_otelMeter = m_provider->GetMeter("xrpld_metrics", "1.0.0");

    if (m_journal.info())
        m_journal.info() << "OTelCollector started successfully";
}

OTelCollectorImp::~OTelCollectorImp()
{
    if (m_journal.info())
        m_journal.info() << "OTelCollector shutting down";
    if (m_provider)
    {
        m_provider->ForceFlush(std::chrono::milliseconds(2000));
        m_provider->Shutdown();
    }
    if (m_journal.info())
        m_journal.info() << "OTelCollector stopped";
}

Hook
OTelCollectorImp::make_hook(HookImpl::HandlerType const& handler)
{
    return Hook(std::make_shared<OTelHookImpl>(handler, shared_from_this()));
}

Counter
OTelCollectorImp::make_counter(std::string const& name)
{
    return Counter(std::make_shared<OTelCounterImpl>(formatName(name), m_otelMeter));
}

Event
OTelCollectorImp::make_event(std::string const& name)
{
    return Event(std::make_shared<OTelEventImpl>(formatName(name), m_otelMeter));
}

Gauge
OTelCollectorImp::make_gauge(std::string const& name)
{
    return Gauge(
        std::make_shared<OTelGaugeImpl>(formatName(name), m_otelMeter, shared_from_this()));
}

Meter
OTelCollectorImp::make_meter(std::string const& name)
{
    return Meter(std::make_shared<OTelMeterImpl>(formatName(name), m_otelMeter));
}

void
OTelCollectorImp::addHook(OTelHookImpl* hook)
{
    std::lock_guard lock(m_mutex);
    m_hooks.push_back(hook);
}

void
OTelCollectorImp::removeHook(OTelHookImpl* hook)
{
    std::lock_guard lock(m_mutex);
    m_hooks.erase(std::remove(m_hooks.begin(), m_hooks.end(), hook), m_hooks.end());
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
    auto last = m_lastHookCallMs.load(std::memory_order_acquire);
    if (now - last < 500)
        return;
    if (!m_lastHookCallMs.compare_exchange_strong(last, now, std::memory_order_acq_rel))
        return;  // Another thread won the race.

    std::lock_guard lock(m_mutex);
    for (auto* hook : m_hooks)
        hook->callHandler();
}

void
OTelCollectorImp::addGauge(OTelGaugeImpl* gauge)
{
    std::lock_guard lock(m_mutex);
    m_gauges.push_back(gauge);
}

void
OTelCollectorImp::removeGauge(OTelGaugeImpl* gauge)
{
    std::lock_guard lock(m_mutex);
    m_gauges.erase(std::remove(m_gauges.begin(), m_gauges.end(), gauge), m_gauges.end());
}

opentelemetry::nostd::shared_ptr<metrics_api::Meter> const&
OTelCollectorImp::otelMeter() const
{
    return m_otelMeter;
}

std::string
OTelCollectorImp::formatName(std::string const& name) const
{
    // StatsD uses "prefix.group.name" format. The OTel StatsD receiver
    // converts dots to underscores for Prometheus. We replicate this
    // to preserve metric name compatibility.
    //
    // Example: prefix="xrpld", name="LedgerMaster.Validated_Ledger_Age"
    //   -> "xrpld_LedgerMaster_Validated_Ledger_Age"
    std::string result;
    if (!m_prefix.empty())
    {
        result = m_prefix;
        result += '_';
    }
    for (char c : name)
    {
        result += (c == '.') ? '_' : c;
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
    Journal journal)
{
    return std::make_shared<detail::OTelCollectorImp>(endpoint, prefix, instanceId, journal);
}

}  // namespace insight
}  // namespace beast

#else  // !XRPL_ENABLE_TELEMETRY

// When telemetry is disabled at compile time, OTelCollector::New()
// returns a NullCollector so callers do not need conditional logic.

#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/insight/OTelCollector.h>

namespace beast {
namespace insight {

std::shared_ptr<Collector>
OTelCollector::New(
    std::string const& /* endpoint */,
    std::string const& /* prefix */,
    std::string const& /* instanceId */,
    Journal /* journal */)
{
    return NullCollector::New();
}

}  // namespace insight
}  // namespace beast

#endif  // XRPL_ENABLE_TELEMETRY
