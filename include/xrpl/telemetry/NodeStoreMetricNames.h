#pragma once

// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

/**
 * Metric name, description, label key and label values for the per-fetch
 * NodeStore read-latency histogram.
 *
 * The name is referenced from two translation units in two different
 * levelization modules, which is why these constants live in a header rather
 * than in either unit's unnamed namespace:
 *
 *   NodeStoreMetricNames.h
 *          |
 *          +--> NodeStoreScheduler.cpp  (xrpld.app -- the record site, via
 *          |                             XRPL_METRIC_HISTOGRAM_RECORD_LABELED)
 *          |
 *          +--> MetricsRegistry.cpp     (xrpld.telemetry -- registers the
 *                                        explicit sub-millisecond bucket
 *                                        boundaries for the same name)
 *
 * A copy-pasted literal would let the two drift, and a drifted name silently
 * drops the bucket override: the histogram would fall back to the SDK default
 * boundaries, whose lowest edge is far above the single-digit-microsecond
 * range a warm read occupies, so every warm read would land in bucket 0 and
 * the distribution would read as flat. This mirrors the reason
 * GetObjectMetricNames.h exists for the `getobject_*` family.
 *
 * Placed under `include/xrpl/telemetry/` for the same levelization reason as
 * GetObjectMetricNames.h: `xrpld.app > xrpl.telemetry` and
 * `xrpld.telemetry > xrpl.telemetry` are both existing edges (see
 * `.github/scripts/levelization/results/ordering.txt`), so `include/xrpl/` is
 * the one level both consumers can already reach. Both sites include this file
 * as `<xrpl/telemetry/NodeStoreMetricNames.h>`.
 *
 * Example usage -- registering the bucket view (MetricsRegistry.cpp):
 * @code
 * addHistogramView(
 *     *views,
 *     kNodeStoreReadUs,
 *     {kSubMillisecondBoundaries.begin(), kSubMillisecondBoundaries.end()});
 * @endcode
 *
 * Example usage -- edge case: the record site labels one instrument with two
 * independent dimensions, which is why the keys and all four values are
 * constants rather than literals (a misspelling on either side would create a
 * second, silently disjoint series):
 * @code
 * XRPL_METRIC_HISTOGRAM_RECORD_LABELED(
 *     app, kNodeStoreReadUs, kNodeStoreReadUsDesc, elapsed.count(),
 *     {{kFetchTypeLabel, std::string(kFetchTypeAsync)},
 *      {kFetchFoundLabel, std::string(kFetchFoundTrue)}});
 * @endcode
 *
 * @note These are `constexpr char[]`, not `constexpr std::string_view`. The
 * OTel C++ API takes `nostd::string_view`, which on this build is OTel's own
 * type; it converts from `char const*` and from `std::string` but has no
 * converting constructor from `std::string_view`, so a `string_view` constant
 * would not compile at the call sites. Same reasoning as
 * GetObjectMetricNames.h.
 *
 * @note Header-only constants with no runtime state, so there is nothing to
 * synchronize -- safe to include from any thread context.
 */

namespace xrpl::telemetry {

// ===== Metric name and description ==========================================

/**
 * Per-fetch NodeStore backend read latency, in microseconds.
 *
 * Referenced twice: at the record site in NodeStoreScheduler.cpp, and by the
 * sub-millisecond `addHistogramView()` call in MetricsRegistry.cpp. Both must
 * use this one constant.
 */
inline constexpr char kNodeStoreReadUs[] = "nodestore_read_us";

/**
 * Description for kNodeStoreReadUs.
 */
inline constexpr char kNodeStoreReadUsDesc[] = "NodeStore backend fetch latency in microseconds";

// ===== Label keys ===========================================================

/**
 * Label key separating an async (read-ahead) fetch from a synchronous one.
 *
 * The two mean different things: a slow async read delays prefetch, while a
 * slow synchronous read blocks a caller outright. Merged into one series they
 * cannot be told apart.
 */
inline constexpr char kFetchTypeLabel[] = "fetch_type";

/**
 * Label key recording whether the fetch found the object.
 *
 * A miss and a hit have different cost profiles -- a miss can require reading
 * every backend -- so mixing them would blur the distribution that matters.
 */
inline constexpr char kFetchFoundLabel[] = "found";

// ===== Label values =========================================================

/** @{ */
/**
 * kFetchTypeLabel values, one per node_store::FetchType enumerator.
 */
inline constexpr char kFetchTypeAsync[] = "async";
inline constexpr char kFetchTypeSync[] = "sync";
/** @} */

/** @{ */
/**
 * kFetchFoundLabel values. Spelled out rather than emitted as a bool
 * AttributeValue so the exported label text is stable and matches the
 * string-valued convention every other label in this codebase follows.
 */
inline constexpr char kFetchFoundTrue[] = "true";
inline constexpr char kFetchFoundFalse[] = "false";
/** @} */

// ===== Record-site helpers ==================================================

/**
 * Map a fetch's async-ness to its kFetchTypeLabel value.
 *
 * Takes a bool rather than a node_store::FetchType because this header sits in
 * `xrpl.telemetry`, which has no levelization edge to `xrpl.nodestore`. The
 * caller does the one-line enum comparison; this function owns the mapping so
 * the two label spellings live in exactly one place and are unit-testable.
 *
 * @param isAsync True for node_store::FetchType::Async.
 * @return kFetchTypeAsync when @p isAsync, else kFetchTypeSync.
 *
 * @note Pure and reentrant: holds no state and performs no I/O.
 *
 * Example:
 * @code
 * fetchTypeLabelValue(true);   // "async"
 * fetchTypeLabelValue(false);  // "sync"
 * @endcode
 */
[[nodiscard]] constexpr char const*
fetchTypeLabelValue(bool isAsync) noexcept
{
    return isAsync ? kFetchTypeAsync : kFetchTypeSync;
}

/**
 * Map a fetch's hit/miss outcome to its kFetchFoundLabel value.
 *
 * @param wasFound node_store::FetchReport::wasFound.
 * @return kFetchFoundTrue when @p wasFound, else kFetchFoundFalse.
 *
 * @note Pure and reentrant: holds no state and performs no I/O.
 *
 * Example:
 * @code
 * fetchFoundLabelValue(true);   // "true"
 * fetchFoundLabelValue(false);  // "false"
 * @endcode
 */
[[nodiscard]] constexpr char const*
fetchFoundLabelValue(bool wasFound) noexcept
{
    return wasFound ? kFetchFoundTrue : kFetchFoundFalse;
}

/**
 * Whether an elapsed microsecond count may be handed to the histogram.
 *
 * The OTel SDK rejects a negative histogram value and logs a warning on every
 * such call, so a clock anomaly on a per-fetch path would turn into a log
 * flood. Filtering here drops the bad sample instead.
 *
 * Zero is recordable: a fetch served from a warm page cache can genuinely
 * round to 0 us, and suppressing that would make the fastest reads invisible.
 *
 * @param elapsedUs Measured fetch duration in microseconds.
 * @return True when @p elapsedUs is zero or positive.
 *
 * @note Pure and reentrant: holds no state and performs no I/O.
 *
 * Example:
 * @code
 * shouldRecordFetchLatency(0);   // true  -- genuinely instant read
 * shouldRecordFetchLatency(-1);  // false -- clock anomaly, skip
 * @endcode
 */
[[nodiscard]] constexpr bool
shouldRecordFetchLatency(long long elapsedUs) noexcept
{
    return elapsedUs >= 0;
}

}  // namespace xrpl::telemetry
