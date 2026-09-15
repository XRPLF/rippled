#pragma once

// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

/**
 * Metric names, label keys, label values, and descriptions for the
 * `TMGetObjectByHash` request path.
 *
 * These constants are shared across two modules, which is why they live in a
 * header rather than in either translation unit's unnamed namespace:
 *
 *   GetObjectMetricNames.h
 *          |
 *          +--> PeerImp.cpp          (XRPL_METRIC_* call sites: records the
 *          |                          instruments)
 *          |
 *          +--> MetricsRegistry.cpp  (addMicrosecondHistogramView: registers
 *                                     the explicit bucket boundaries for
 *                                     kGetObjectLookupUs)
 *
 * `kGetObjectLookupUs` in particular is referenced from both sites. A
 * copy-pasted literal would let the two drift, and a drifted name silently
 * drops the bucket override -- the histogram would fall back to the SDK
 * default boundaries, which top out at 10,000 (10 ms), so every quantile
 * would saturate. This mirrors the reason the existing
 * `kJobQueuedDurationUs` / `kJobRunningDurationUs` / `kRpcMethodDurationUs`
 * constants exist in MetricsRegistry.cpp; those three are referenced only
 * within that one file, so they stay file-local.
 *
 * Placed under `include/xrpl/telemetry/` because the two consumers sit in
 * different levelization modules: PeerImp.cpp is `xrpld.overlay` and
 * MetricsRegistry.cpp is `xrpld.telemetry`. Both are allowed to depend on
 * `xrpl.telemetry` (see `xrpld.overlay > xrpl.telemetry` and
 * `xrpld.telemetry > xrpl.telemetry` in the levelization ordering results), so
 * `include/xrpl/` is the one level both can reach. Keeping the constants in
 * `src/xrpld/telemetry/` would have required overlay to include a private
 * xrpld header from another module, flipping a levelization edge. Both sites
 * include this file as `<xrpl/telemetry/GetObjectMetricNames.h>`.
 *
 * Example usage -- recording a histogram:
 * @code
 * XRPL_METRIC_HISTOGRAM_RECORD(
 *     app_, kGetObjectRequestObjects, kGetObjectRequestObjectsDesc, requested);
 * @endcode
 *
 * Example usage -- edge case: the same instrument recorded under two
 * different label values, which is why the label key and both values are
 * constants rather than literals:
 * @code
 * XRPL_METRIC_COUNTER_ADD_LABELED(
 *     app_, kGetObjectLookupsTotal, kGetObjectLookupsTotalDesc, hits,
 *     {{kLabelResult, std::string(kResultHit)}});
 * XRPL_METRIC_COUNTER_ADD_LABELED(
 *     app_, kGetObjectLookupsTotal, kGetObjectLookupsTotalDesc, misses,
 *     {{kLabelResult, std::string(kResultMiss)}});
 * @endcode
 *
 * @note These are `constexpr char[]`, not `constexpr std::string_view`. The
 * OTel C++ API takes `nostd::string_view`, which on this build is OTel's own
 * type (the build defines only OPENTELEMETRY_ABI_VERSION_NO, not
 * OPENTELEMETRY_STL_VERSION, so `nostd::string_view` is not an alias for
 * `std::string_view`). It converts from `char const*` and from
 * `std::string`, but has no converting constructor from `std::string_view`,
 * so a `string_view` constant would not compile at the call sites. This also
 * matches the existing convention in MetricsRegistry.cpp.
 *
 * @note Header-only constants with no runtime state, so there is nothing to
 * synchronize -- safe to include from any thread context.
 */

namespace xrpl::telemetry {

// ===== Metric names ==========================================================

/**
 * Requests refused by `onMessage(TMGetObjectByHash)` before any NodeStore
 * access, split by the `reason` label.
 */
inline constexpr char kGetObjectRejectedTotal[] = "getobject_rejected_total";

/**
 * Distribution of the object count requested per message.
 */
inline constexpr char kGetObjectRequestObjects[] = "getobject_request_objects";

/**
 * Wall time spent in the NodeStore fetch loop, in microseconds.
 *
 * Referenced twice: here at the record site, and by
 * `addMicrosecondHistogramView()` in MetricsRegistry.cpp. Both must use this
 * one constant.
 */
inline constexpr char kGetObjectLookupUs[] = "getobject_lookup_us";

/**
 * NodeStore lookups performed, split by the `result` label.
 */
inline constexpr char kGetObjectLookupsTotal[] = "getobject_lookups_total";

/**
 * Distribution of the dynamic resource charge applied per request.
 */
inline constexpr char kGetObjectCharge[] = "getobject_charge";

// ===== Label keys ============================================================

/**
 * Label key distinguishing a hit from a miss on `kGetObjectLookupsTotal`.
 */
inline constexpr char kLabelResult[] = "result";

/**
 * Label key naming which gate refused the request.
 */
inline constexpr char kLabelReason[] = "reason";

// ===== Label values ==========================================================

/**
 * `kLabelResult` value: the object was found in the NodeStore.
 */
inline constexpr char kResultHit[] = "hit";

/**
 * `kLabelResult` value: the object was not found.
 */
inline constexpr char kResultMiss[] = "miss";

/**
 * `kLabelReason` value: more objects requested than the handler accepts.
 */
inline constexpr char kReasonOversize[] = "oversize";

/**
 * `kLabelReason` value: the ledger hash was not uint256-sized.
 */
inline constexpr char kReasonMalformedLedgerHash[] = "malformed_ledgerhash";

// ===== Instrument descriptions ===============================================

/** @{ */
inline constexpr char kGetObjectRejectedTotalDesc[] =
    "TMGetObjectByHash requests refused before any NodeStore access, by reason";

inline constexpr char kGetObjectRequestObjectsDesc[] =
    "Objects requested per TMGetObjectByHash message";

inline constexpr char kGetObjectLookupUsDesc[] =
    "Time spent in the TMGetObjectByHash NodeStore fetch loop, in microseconds";

inline constexpr char kGetObjectLookupsTotalDesc[] =
    "TMGetObjectByHash NodeStore lookups, by hit/miss result";

inline constexpr char kGetObjectChargeDesc[] =
    "Dynamic resource charge applied per TMGetObjectByHash request";
/** @} */

}  // namespace xrpl::telemetry
