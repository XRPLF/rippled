#pragma once

/**
 * Call-site OTel metric macros.
 *
 * Adds a new OTel metric instrument entirely at the call site -- no member
 * field, no init line, no wrapper method in MetricsRegistry. Covers every
 * instrument kind the OTel Metrics API defines:
 *
 *   Synchronous (created once on first use, then record on every call):
 *     Counter            XRPL_METRIC_COUNTER_INC / _ADD [+ _LABELED]
 *     UpDownCounter      XRPL_METRIC_UPDOWN_ADD [+ _LABELED]
 *     Histogram          XRPL_METRIC_HISTOGRAM_RECORD [+ _LABELED]
 *     Gauge              XRPL_METRIC_GAUGE_RECORD [+ _LABELED]
 *                         (requires OPENTELEMETRY_ABI_VERSION_NO >= 2;
 *                         this repo currently builds ABI v1 -- see the
 *                         static_assert branch below)
 *
 *   Asynchronous/observable (register a callback ONCE, eagerly, during
 *   construction/init -- see the "Observable" note below):
 *     Observable Counter        XRPL_METRIC_OBSERVABLE_COUNTER_REGISTER
 *     Observable UpDownCounter  XRPL_METRIC_OBSERVABLE_UPDOWN_REGISTER
 *     Observable Gauge          XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER
 *
 * When XRPL_ENABLE_TELEMETRY is not defined, every macro expands to a
 * no-op statement, so call sites never need their own #ifdef.
 *
 * Example usage -- plain counter:
 * @code
 * void RCLConsensus::Adaptor::doAccept()
 * {
 *     // ... existing consensus-accept logic ...
 *     XRPL_METRIC_COUNTER_INC(app_, "ledgers_closed_total",
 *         "Total ledgers closed by consensus");
 * }
 * @endcode
 *
 * Example usage -- labeled counter (edge case: per-reason tally):
 * @code
 * void TxQ::apply(...)
 * {
 *     if (queueIsFull)
 *         XRPL_METRIC_COUNTER_INC_LABELED(app, "txq_dropped_total",
 *             "Transactions refused admission to the queue",
 *             {{"reason", std::string("queue_full")}});
 * }
 * @endcode
 *
 * Pass the label set as a bare brace-enclosed list, as above. Do not wrap
 * it in an extra pair of parentheses: the list is forwarded verbatim into
 * the OTel `Add()`/`Record()` call, which takes an initializer_list, and
 * the extra parentheses do not compile.
 *
 * Wrap each label *value* in `std::string`. `AttributeValue` is a variant
 * in which a bare `const char*` selects the boolean alternative, so an
 * unwrapped literal is recorded as `true`.
 *
 * Example usage -- UpDownCounter (edge case: value that can decrease):
 * @code
 * void ServerHandler::onRpcStart()
 * {
 *     XRPL_METRIC_UPDOWN_ADD(app_, "rpc_in_flight_requests",
 *         "RPC requests currently executing", 1);
 * }
 * void ServerHandler::onRpcFinish()
 * {
 *     XRPL_METRIC_UPDOWN_ADD(app_, "rpc_in_flight_requests",
 *         "RPC requests currently executing", -1);
 * }
 * @endcode
 *
 * Example usage -- observable gauge registered from a non-MetricsRegistry
 * class (edge case: a subsystem exposing its own live state):
 * @code
 * SomeSubsystem::SomeSubsystem(ServiceRegistry& app) : app_(app)
 * {
 *     XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER(
 *         app_, "some_subsystem_queue_depth", "Current queue depth",
 *         [this] { return static_cast<int64_t>(queue_.size()); });
 * }
 * @endcode
 *
 * @note A histogram whose values can exceed ~10,000 units (e.g. a
 * microsecond duration beyond 10ms) needs an explicit-bucket View, which
 * OTel can only register at MeterProvider construction time -- this
 * cannot be done from a call site. Register such a view in
 * MetricsRegistry::initExporterAndProvider() as today; the
 * histogram-record call itself can still use the macro.
 *
 * @note The SYNCHRONOUS macros (Counter/UpDownCounter/Histogram/Gauge)
 * create their instrument once, on first use, from
 * MetricsRegistry::meter(). The registry builds that meter in its
 * constructor, before any subsystem exists, and guarantees it is never
 * empty while the registry is enabled (a no-op meter stands in if the
 * pipeline failed to build, and again after stop()). So a call site holds
 * a valid instrument from its first call and needs no check of its own.
 * The only branch on the hot path is the recording() gate, which is false
 * once stop() has torn the pipeline down; without that gate a Record on a
 * stale SDK instrument would deref a dangling AggregationConfig.
 *
 * @note Static-init safety: Meter::CreateXxx is declared noexcept in the
 * OTel API (opentelemetry/metrics/meter.h), so the function-local static
 * that caches the instrument cannot throw during first-call construction.
 * A throw there would call std::terminate.
 *
 * @note The OBSERVABLE registration macros are the opposite: call them
 * EAGERLY, exactly once, from constructor/init code -- never from a hot
 * path. Repeated calls at the same call site register a NEW callback
 * each time (no create-once caching, unlike the synchronous macros),
 * which leaks callbacks.
 *
 * @note There is no way to read back a synchronous instrument's current
 * accumulated value from application code -- the OTel API is
 * write-only/push-based by design. If your logic needs both to record a
 * metric AND read its running value, keep your own state (std::atomic or
 * similar) and separately feed OTel via these macros.
 */

// On Windows, OTel's spin_lock_mutex.h (transitively included from
// MetricsRegistry.h) defines _WINSOCKAPI_ and includes <windows.h>, which
// pulls in WinSock 1. ServiceRegistry.h below then includes <boost/asio.hpp>,
// whose socket_types.hpp requires winsock2.h first and errors out if WinSock 1
// arrived earlier. Pre-including boost's socket types header here gets
// winsock2.h in before the OTel headers, so any translation unit that includes
// MetricMacros.h first (e.g. the telemetry unit tests) still compiles. The
// production MetricsRegistry.cpp carries the same guard.
#ifdef _MSC_VER
#include <boost/asio/detail/socket_types.hpp>
#endif

#include <xrpld/telemetry/MetricsRegistry.h>  // IWYU pragma: keep

#include <xrpl/core/ServiceRegistry.h>  // IWYU pragma: keep

#ifdef XRPL_ENABLE_TELEMETRY

#include <functional>  // IWYU pragma: keep

#define XRPL_METRIC_COUNTER_INC(app, name, description)                                     \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_counter_ =                                               \
                xrpl_mr_->meter()->CreateUInt64Counter((name), (description));              \
            xrpl_counter_->Add(1);                                                          \
        }                                                                                   \
    } while (false)

// The label set is passed as trailing variadic arguments so a
// brace-enclosed initializer list (e.g. {{"reason", std::string("x")}}),
// which contains a top-level comma, survives preprocessing as a single
// logical argument. __VA_ARGS__ re-joins it verbatim into the Add() call.
#define XRPL_METRIC_COUNTER_INC_LABELED(app, name, description, ...)                        \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_counter_ =                                               \
                xrpl_mr_->meter()->CreateUInt64Counter((name), (description));              \
            xrpl_counter_->Add(1, __VA_ARGS__);                                             \
        }                                                                                   \
    } while (false)

// Same as XRPL_METRIC_COUNTER_INC, but increments by a caller-supplied amount
// instead of a fixed 1 (e.g. bytes transferred, batch sizes).
#define XRPL_METRIC_COUNTER_ADD(app, name, description, amount)                             \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_counter_ =                                               \
                xrpl_mr_->meter()->CreateUInt64Counter((name), (description));              \
            xrpl_counter_->Add(amount);                                                     \
        }                                                                                   \
    } while (false)

// amount is fixed; the trailing variadic args carry the label set (see the
// note on XRPL_METRIC_COUNTER_INC_LABELED for why labels are variadic).
#define XRPL_METRIC_COUNTER_ADD_LABELED(app, name, description, amount, ...)                \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_counter_ =                                               \
                xrpl_mr_->meter()->CreateUInt64Counter((name), (description));              \
            xrpl_counter_->Add(amount, __VA_ARGS__);                                        \
        }                                                                                   \
    } while (false)

// UpDownCounter: like COUNTER_ADD, but the underlying instrument permits a
// negative amount (e.g. in-flight request count, +1 on start / -1 on
// finish from two different points in the same or different call sites).
// A plain Counter's Add() must never see a negative value per the OTel
// API contract; use this macro, not COUNTER_ADD, whenever the value can
// decrease.
#define XRPL_METRIC_UPDOWN_ADD(app, name, description, amount)                              \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_updown_ =                                                \
                xrpl_mr_->meter()->CreateInt64UpDownCounter((name), (description));         \
            xrpl_updown_->Add(amount);                                                      \
        }                                                                                   \
    } while (false)

// amount may be negative; the trailing variadic args carry the label set
// (see the note on XRPL_METRIC_COUNTER_INC_LABELED for why labels are variadic).
#define XRPL_METRIC_UPDOWN_ADD_LABELED(app, name, description, amount, ...)                 \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            static auto const xrpl_updown_ =                                                \
                xrpl_mr_->meter()->CreateInt64UpDownCounter((name), (description));         \
            xrpl_updown_->Add(amount, __VA_ARGS__);                                         \
        }                                                                                   \
    } while (false)

#define XRPL_METRIC_HISTOGRAM_RECORD(app, name, description, value)                            \
    do                                                                                         \
    {                                                                                          \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording())    \
        {                                                                                      \
            static auto const xrpl_hist_ =                                                     \
                xrpl_mr_->meter()->CreateDoubleHistogram((name), (description));               \
            xrpl_hist_->Record(static_cast<double>(value), opentelemetry::context::Context{}); \
        }                                                                                      \
    } while (false)

// value is fixed; the trailing variadic args carry the label set (see the
// note on XRPL_METRIC_COUNTER_INC_LABELED for why labels are variadic).
#define XRPL_METRIC_HISTOGRAM_RECORD_LABELED(app, name, description, value, ...)             \
    do                                                                                       \
    {                                                                                        \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording())  \
        {                                                                                    \
            static auto const xrpl_hist_ =                                                   \
                xrpl_mr_->meter()->CreateDoubleHistogram((name), (description));             \
            xrpl_hist_->Record(                                                              \
                static_cast<double>(value), __VA_ARGS__, opentelemetry::context::Context{}); \
        }                                                                                    \
    } while (false)

// Synchronous Gauge: last-value snapshot, not a distribution (contrast
// Histogram) and not a running total (contrast Counter/UpDownCounter).
// ABI-gated: opentelemetry-cpp only exposes CreateInt64Gauge/
// CreateDoubleGauge when OPENTELEMETRY_ABI_VERSION_NO >= 2. This project's
// Conan build currently pins ABI v1 (verified:
// .build/build/generators/opentelemetry-cpp-release-x86_64-data.cmake sets
// OPENTELEMETRY_ABI_VERSION_NO=1), so the real path below is presently
// dead code on this codebase's build -- shipped anyway so it activates
// automatically the day the ABI version is bumped, and so a developer who
// reaches for "just the current value, not a distribution" sees an
// actionable compile error now instead of silently reaching for the wrong
// instrument kind (misusing Histogram or UpDownCounter as a gauge
// substitute is explicitly discouraged -- see Design/taxonomy section).
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
#define XRPL_METRIC_GAUGE_RECORD(app, name, description, value)                                 \
    do                                                                                          \
    {                                                                                           \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording())     \
        {                                                                                       \
            static auto const xrpl_gauge_ =                                                     \
                xrpl_mr_->meter()->CreateDoubleGauge((name), (description));                    \
            xrpl_gauge_->Record(static_cast<double>(value), opentelemetry::context::Context{}); \
        }                                                                                       \
    } while (false)
// value is fixed; the trailing variadic args carry the label set (see the
// note on XRPL_METRIC_COUNTER_INC_LABELED for why labels are variadic).
#define XRPL_METRIC_GAUGE_RECORD_LABELED(app, name, description, value, ...)                 \
    do                                                                                       \
    {                                                                                        \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording())  \
        {                                                                                    \
            static auto const xrpl_gauge_ =                                                  \
                xrpl_mr_->meter()->CreateDoubleGauge((name), (description));                 \
            xrpl_gauge_->Record(                                                             \
                static_cast<double>(value), __VA_ARGS__, opentelemetry::context::Context{}); \
        }                                                                                    \
    } while (false)
#else
#define XRPL_METRIC_GAUGE_RECORD(app, name, description, value)                            \
    static_assert(                                                                         \
        false,                                                                             \
        "XRPL_METRIC_GAUGE_RECORD requires OPENTELEMETRY_ABI_VERSION_NO >= 2 (this build " \
        "uses ABI v1). Use XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER with your own state, or " \
        "bump OPENTELEMETRY_ABI_VERSION_NO as a separate, reviewed change.")
#define XRPL_METRIC_GAUGE_RECORD_LABELED(app, name, description, value, ...)             \
    static_assert(                                                                       \
        false,                                                                           \
        "XRPL_METRIC_GAUGE_RECORD_LABELED requires OPENTELEMETRY_ABI_VERSION_NO >= 2 "   \
        "(this build uses ABI v1). Use XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER with your " \
        "own state, or bump OPENTELEMETRY_ABI_VERSION_NO as a separate, reviewed change.")
#endif  // OPENTELEMETRY_ABI_VERSION_NO >= 2

// -----------------------------------------------------------------
// Observable/async instrument registration. Unlike the synchronous
// macros above, these do NOT lazily create-on-first-call -- they
// register a callback with the SDK immediately, at the call site, the
// moment the macro executes. Callers MUST invoke this during
// construction/init, before the server is fully live (same timing rule
// MetricsRegistry::registerAsyncGauges() already follows for its own
// gauges). Calling it from a hot-path function instead of an init path
// re-registers a new callback on every call, which leaks callbacks and
// is NOT what this macro is for.
//
// The callable is captured in a heap-allocated std::function, and its
// address is passed as the `void* state` to AddCallback (whose signature,
// `void (*)(ObserverResult, void*)`, is a raw C function pointer -- it
// cannot bind a capturing lambda directly). A static trampoline
// function reinterprets `state` back to the std::function and invokes
// it inside the callback. The heap allocation is intentionally leaked
// for the process lifetime (matches every existing ObservableGauge in
// MetricsRegistry, which are member fields with the same lifetime as
// the registry itself) -- do not "fix" this with a smart pointer that
// frees before the reader thread's last collection tick.
// -----------------------------------------------------------------
#define XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER(app, name, description, valueFn)              \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            auto xrpl_m_ = xrpl_mr_->meter();                                               \
            auto* xrpl_fn_ = new std::function<int64_t()>(valueFn);                         \
            auto xrpl_inst_ = xrpl_m_->CreateInt64ObservableGauge((name), (description));   \
            xrpl_inst_->AddCallback(                                                        \
                [](opentelemetry::metrics::ObserverResult result, void* state) {            \
                    auto* fn = static_cast<std::function<int64_t()>*>(state);               \
                    try                                                                     \
                    {                                                                       \
                        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<         \
                            opentelemetry::metrics::ObserverResultT<int64_t>>>(result)      \
                            ->Observe((*fn)());                                             \
                    }                                                                       \
                    catch (...)                                                             \
                    {                                                                       \
                    }                                                                       \
                },                                                                          \
                xrpl_fn_);                                                                  \
        }                                                                                   \
    } while (false)

#define XRPL_METRIC_OBSERVABLE_COUNTER_REGISTER(app, name, description, valueFn)            \
    do                                                                                      \
    {                                                                                       \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording()) \
        {                                                                                   \
            auto xrpl_m_ = xrpl_mr_->meter();                                               \
            auto* xrpl_fn_ = new std::function<int64_t()>(valueFn);                         \
            auto xrpl_inst_ = xrpl_m_->CreateInt64ObservableCounter((name), (description)); \
            xrpl_inst_->AddCallback(                                                        \
                [](opentelemetry::metrics::ObserverResult result, void* state) {            \
                    auto* fn = static_cast<std::function<int64_t()>*>(state);               \
                    try                                                                     \
                    {                                                                       \
                        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<         \
                            opentelemetry::metrics::ObserverResultT<int64_t>>>(result)      \
                            ->Observe((*fn)());                                             \
                    }                                                                       \
                    catch (...)                                                             \
                    {                                                                       \
                    }                                                                       \
                },                                                                          \
                xrpl_fn_);                                                                  \
        }                                                                                   \
    } while (false)

#define XRPL_METRIC_OBSERVABLE_UPDOWN_REGISTER(app, name, description, valueFn)                   \
    do                                                                                            \
    {                                                                                             \
        if (auto* xrpl_mr_ = (app).getMetricsRegistry(); xrpl_mr_ && xrpl_mr_->recording())       \
        {                                                                                         \
            auto xrpl_m_ = xrpl_mr_->meter();                                                     \
            auto* xrpl_fn_ = new std::function<int64_t()>(valueFn);                               \
            auto xrpl_inst_ = xrpl_m_->CreateInt64ObservableUpDownCounter((name), (description)); \
            xrpl_inst_->AddCallback(                                                              \
                [](opentelemetry::metrics::ObserverResult result, void* state) {                  \
                    auto* fn = static_cast<std::function<int64_t()>*>(state);                     \
                    try                                                                           \
                    {                                                                             \
                        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<               \
                            opentelemetry::metrics::ObserverResultT<int64_t>>>(result)            \
                            ->Observe((*fn)());                                                   \
                    }                                                                             \
                    catch (...)                                                                   \
                    {                                                                             \
                    }                                                                             \
                },                                                                                \
                xrpl_fn_);                                                                        \
        }                                                                                         \
    } while (false)

#else  // !XRPL_ENABLE_TELEMETRY

#define XRPL_METRIC_COUNTER_INC(app, name, description) \
    do                                                  \
    {                                                   \
    } while (false)
#define XRPL_METRIC_COUNTER_INC_LABELED(app, name, description, ...) \
    do                                                               \
    {                                                                \
    } while (false)
#define XRPL_METRIC_COUNTER_ADD(app, name, description, amount) \
    do                                                          \
    {                                                           \
    } while (false)
#define XRPL_METRIC_COUNTER_ADD_LABELED(app, name, description, amount, ...) \
    do                                                                       \
    {                                                                        \
    } while (false)
#define XRPL_METRIC_UPDOWN_ADD(app, name, description, amount) \
    do                                                         \
    {                                                          \
    } while (false)
#define XRPL_METRIC_UPDOWN_ADD_LABELED(app, name, description, amount, ...) \
    do                                                                      \
    {                                                                       \
    } while (false)
#define XRPL_METRIC_HISTOGRAM_RECORD(app, name, description, value) \
    do                                                              \
    {                                                               \
    } while (false)
#define XRPL_METRIC_HISTOGRAM_RECORD_LABELED(app, name, description, value, ...) \
    do                                                                           \
    {                                                                            \
    } while (false)
#define XRPL_METRIC_GAUGE_RECORD(app, name, description, value) \
    do                                                          \
    {                                                           \
    } while (false)
#define XRPL_METRIC_GAUGE_RECORD_LABELED(app, name, description, value, ...) \
    do                                                                       \
    {                                                                        \
    } while (false)
#define XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER(app, name, description, valueFn) \
    do                                                                         \
    {                                                                          \
    } while (false)
#define XRPL_METRIC_OBSERVABLE_COUNTER_REGISTER(app, name, description, valueFn) \
    do                                                                           \
    {                                                                            \
    } while (false)
#define XRPL_METRIC_OBSERVABLE_UPDOWN_REGISTER(app, name, description, valueFn) \
    do                                                                          \
    {                                                                           \
    } while (false)

#endif  // XRPL_ENABLE_TELEMETRY
