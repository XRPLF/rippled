#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace xrpl::telemetry::buckets {

/**
 * @file HistogramBuckets.h
 * @brief Explicit histogram bucket edges for xrpld's OTel instruments.
 *
 * One header owns every ladder, so a reviewer sees all of them together and
 * a test can assert their invariants.
 *
 * Why a ladder is worth this much care: when a quantile falls in the `+Inf`
 * bucket, Prometheus returns the *second-highest* edge, not `+Inf`. A
 * saturated histogram therefore reports a believable constant instead of an
 * obvious error. The same trap exists at the bottom -- if nearly every
 * sample lands in bucket 0, `histogram_quantile` interpolates inside it and
 * invents a value. A ladder is correct only when its floor sits below the
 * mass of the distribution and its ceiling above the tail.
 *
 *   sample --> [ SDK lower_bound over edges ] --> per-bucket counter
 *                          |                              |
 *                  edges come from                        v
 *                    THIS header                     OTLP export
 *                                                         |
 *                                                         v
 *                                        histogram_quantile() in Grafana
 *
 * Ladders are `std::array<double, N>` so they are constant-initialised and
 * usable in a `static_assert`. The OTel SDK wants `std::vector<double>` in
 * its aggregation config, so call toVector() at the registration site
 * rather than storing vectors here.
 *
 * Example -- register a view with the millisecond ladder:
 * @code
 * auto config = std::make_shared<HistogramAggregationConfig>();
 * config->boundaries_ = buckets::toVector(buckets::kMillisecondBuckets);
 * @endcode
 *
 * Example -- the edge case that motivated a second ladder. An Event whose
 * samples are sizes rather than durations must not borrow a latency ladder,
 * or a quarter of its samples land in `+Inf` and every quantile reads back
 * as the top edge:
 * @code
 * config->boundaries_ = buckets::toVector(buckets::kByteBuckets);
 * @endcode
 *
 * @note Thread safety: every member is `constexpr` and immutable, so
 *       reading them from any thread is safe. toVector() allocates and is
 *       meant for start-up registration paths, never for a record path.
 * @note Limitation: changing a ladder changes the exported series count and
 *       ends bucket comparability across the change -- existing series keep
 *       their old `le` values, so panels show a break at restart. Grafana
 *       Cloud bills per series, so re-measure the series count after any
 *       edit here.
 */

/**
 * Bucket edges, in milliseconds, for whole-millisecond `beast::insight`
 * Events: job queue wait and run times, io latency, RPC time, pathfinding.
 *
 * **This list must contain every representable edge of the collector's
 * spanmetrics ladder, and may extend above it.** Agreement over the shared
 * range is deliberate: it lets a span-derived latency panel and a native
 * histogram panel be read on the same scale. Drop an edge the collector
 * carries and every quantile above it reads back as the top edge instead of
 * failing. `check_bucket_parity.py` enforces the containment -- add a
 * collector edge, add it here too.
 *
 * The sub-millisecond edges the collector carries (0.01 to 0.5 ms) are
 * deliberately absent. `beast::insight::Event` rounds every duration up to
 * a whole millisecond before it reaches the histogram, so those edges would
 * collect nothing. Metrics that genuinely need finer resolution belong on
 * the microsecond ladder, on the OTel-native path.
 *
 * The 60 s and 120 s edges exceed the collector's 30 s top on purpose,
 * because jobs outlive spans: the updatepaths job type was measured
 * averaging about 60 s, so a 30 s ceiling would censor its quantiles just
 * as 5 s censors them today. All these Events share one ladder, so its
 * ceiling has to cover the slowest member rather than the typical one.
 *
 * The 2, 3 and 4 s edges resolve second-scale work, which a single
 * four-second-wide bucket can only interpolate across.
 */
inline constexpr std::array kMillisecondBuckets{
    1.0,
    5.0,
    10.0,
    25.0,
    50.0,
    100.0,
    250.0,
    500.0,
    1'000.0,
    2'000.0,
    3'000.0,
    4'000.0,
    5'000.0,
    10'000.0,
    30'000.0,
    60'000.0,
    120'000.0};

/**
 * Bucket edges, in bytes, for `beast::insight` Events whose samples are
 * sizes rather than durations. Currently only the RPC response size.
 *
 * Placed from the measured distribution rather than from a guess about how
 * large a response could theoretically be. Measured over 24 h: mean 2131 B,
 * half of all responses under 1 kB, three quarters under 5 kB. The tail
 * above 5 kB has a mean of at most 7538 B, which bounds p99 near 80 kB and
 * p99.75 below 256 kB.
 *
 * So the resolution belongs between 512 B and 64 kB, where the
 * distribution actually turns, and two further edges are ample headroom.
 * Spending edges at the megabyte scale would cost cardinality on a range
 * nothing measured occupies. If a genuinely multi-megabyte response ever
 * shows up in the top bucket, extend this -- but extend it on evidence.
 */
inline constexpr std::array kByteBuckets{
    512.0,
    1'024.0,
    2'048.0,
    4'096.0,
    8'192.0,
    16'384.0,
    32'768.0,
    65'536.0,
    262'144.0,
    1'048'576.0};

/**
 * @brief Check that a ladder is strictly ascending and non-negative.
 *
 * The SDK places a sample with `std::lower_bound` over the edges, which
 * silently misbuckets when edges repeat or descend. Checking at compile
 * time makes that class of typo impossible to ship.
 *
 * @param ladder Bucket upper bounds to check.
 * @return true when the ladder is non-empty, starts at or above zero, and
 *         every later edge is strictly greater than its predecessor.
 */
constexpr bool
isAscendingNonNegative(std::span<double const> ladder) noexcept
{
    if (ladder.empty() || ladder.front() < 0.0)
        return false;

    for (std::size_t i = 1; i < ladder.size(); ++i)
    {
        if (!(ladder[i] > ladder[i - 1]))
            return false;
    }
    return true;
}

static_assert(isAscendingNonNegative(kMillisecondBuckets));
static_assert(isAscendingNonNegative(kByteBuckets));

/**
 * @brief Copy a ladder into the `std::vector<double>` the OTel SDK wants.
 *
 * @param ladder Bucket upper bounds.
 * @return A vector holding the same edges in the same order.
 */
inline std::vector<double>
toVector(std::span<double const> ladder)
{
    return std::vector<double>(ladder.begin(), ladder.end());
}

}  // namespace xrpl::telemetry::buckets
