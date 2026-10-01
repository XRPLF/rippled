#pragma once

/**
 * Startup pre-registration of getobject_rejected_total.
 *
 * An XRPL_METRIC_COUNTER_* macro creates each series on its first Add, so the
 * first event of a rare counter reads as 0 under increase() and rate(). The
 * function below creates such a counter at startup and records 0 on each
 * label set. An event after the first export then has an earlier sample to be
 * counted against; one before that export shares the first sample with the
 * zero and still reads as 0.
 *
 * @code
 *   ApplicationImp::setup()
 *     |  before any peer can connect
 *     v
 *   preRegisterGetObjectCounters(app)
 *     |
 *     +--> XRPL_METRIC_COUNTER_PREREGISTER_LABELED         MetricMacros.h
 *            kGetObjectRejectedTotal, its description,
 *            kLabelReason, kGetObjectRejectedReasons  GetObjectMetricNames.h
 * @endcode
 *
 * A template on the app, because the macros are duck-typed on it, so a unit
 * test can pass a fake app and run the exact call setup() makes.
 *
 * @code
 * // In ApplicationImp::setup(), before overlay_ is built:
 * telemetry::preRegisterGetObjectCounters(*this);
 *
 * // Edge case: telemetry disabled, or the registry stopped. The macro skips,
 * // so no instrument is created and meter() is never read.
 * telemetry::preRegisterGetObjectCounters(app);
 * @endcode
 *
 * @note Safe from any thread while the registry records, like the recording
 *       macros. Call it once, at startup; a second call adds no series.
 */

#include <xrpl/telemetry/MetricMacros.h>

#ifdef XRPL_ENABLE_TELEMETRY
// Named only as macro arguments, which the macros drop when telemetry is
// compiled out.
#include <xrpl/telemetry/GetObjectMetricNames.h>
#endif

namespace xrpl::telemetry {

/**
 * Create getobject_rejected_total at 0 for each refusal reason.
 *
 * PeerImp records a refusal only for a peer's request, so a call made before
 * any peer can connect precedes every refusal.
 *
 * @param app The application, or a test fake that has the same
 *            getMetricsRegistry().
 */
template <class App>
void
preRegisterGetObjectCounters([[maybe_unused]] App& app)
{
    XRPL_METRIC_COUNTER_PREREGISTER_LABELED(
        app,
        kGetObjectRejectedTotal,
        kGetObjectRejectedTotalDesc,
        labelSetsFor(kLabelReason, kGetObjectRejectedReasons));
}

}  // namespace xrpl::telemetry
