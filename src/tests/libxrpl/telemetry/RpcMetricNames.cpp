/**
 * GTest unit tests for the RPC request-count metric names and their bucket fit.
 *
 * Two facts here have no other guard in CI. First, a metric NAME is the
 * Prometheus series every panel and alert selects on, and
 * `check_otel_naming.py` checks span attribute keys, not metric names -- so a
 * rename or a stray unit suffix would pass every gate and blank the panels.
 * Second, the reason these two histograms need an explicit-bucket view is the
 * ladder FLOOR, which is invisible in a dashboard: a quantile that falls inside
 * bucket 0 is interpolated from the bucket edge and reads back as a plausible
 * number. The bucket-index tests below pin that with the SDK's own placement
 * rule rather than leaving it to review.
 */

#include <xrpl/telemetry/RpcMetricNames.h>

#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/telemetry/HistogramBuckets.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace xrpl::telemetry {

namespace {

/**
 * Placement rule the OTel SDK uses: the bucket index of a sample is the count
 * of edges strictly below it, found with `std::lower_bound` over the ascending
 * edge list. Recomputed here rather than asserted from memory, so a ladder edit
 * moves the expected indices with it.
 *
 * @param ladder Bucket upper bounds, ascending.
 * @param sample Value to place.
 * @return Zero-based bucket index; `ladder.size()` means the `+Inf` bucket.
 */
[[nodiscard]] std::size_t
bucketIndex(std::span<double const> ladder, double sample)
{
    return static_cast<std::size_t>(std::ranges::lower_bound(ladder, sample) - ladder.begin());
}

/**
 * Last underscore-separated segment of a metric name.
 *
 * @param name Metric name.
 * @return The text after the final underscore, or the whole name when there is
 *         no underscore.
 */
[[nodiscard]] std::string_view
lastSegment(std::string_view name)
{
    auto const pos = name.rfind('_');
    return pos == std::string_view::npos ? name : name.substr(pos + 1);
}

/**
 * The opentelemetry-cpp default explicit-bucket boundaries, quoted from the
 * SDK because they are the ladder these two instruments would fall back to if
 * their view were dropped. Not a repo constant, so there is no symbol to read
 * them from.
 */
inline constexpr std::array kSdkDefaultBuckets{
    0.0,
    5.0,
    10.0,
    25.0,
    50.0,
    75.0,
    100.0,
    250.0,
    500.0,
    750.0,
    1'000.0,
    2'500.0,
    5'000.0,
    7'500.0,
    10'000.0};

/**
 * Smallest number of bytes one batch sub-request can occupy inside the
 * `params` array: the two braces of an empty object plus its separating comma.
 * An empty object still passes `isObject()`, so the handler iterates over it.
 */
inline constexpr int kMinBatchItemBytes = 3;

}  // namespace

TEST(RpcMetricNames, namesAreTheExactExportedSeriesNames)
{
    // These strings ARE the Prometheus series. Changing one is a
    // dashboard-breaking change, so it has to be a deliberate edit here too.
    EXPECT_EQ(std::string_view{kRpcBatchSize}, "rpc_batch_size");
    EXPECT_EQ(std::string_view{kPathfindDiscoveredPaths}, "pathfind_discovered_paths");
}

TEST(RpcMetricNames, descriptionsAreTheExactExportedHelpText)
{
    // The description becomes the Prometheus `# HELP` line, so it is part of
    // the exported surface, not a code comment.
    EXPECT_EQ(std::string_view{kRpcBatchSizeDesc}, "Sub-requests per batch JSON-RPC call");
    EXPECT_EQ(
        std::string_view{kPathfindDiscoveredPathsDesc},
        "Payment paths produced per pathfinding pass, across all source assets");
}

TEST(RpcMetricNames, namesAreLowerSnakeCase)
{
    for (std::string_view const name :
         {std::string_view{kRpcBatchSize}, std::string_view{kPathfindDiscoveredPaths}})
    {
        ASSERT_FALSE(name.empty());
        EXPECT_TRUE(name.front() >= 'a' && name.front() <= 'z')
            << name << " must start with a lowercase letter";
        EXPECT_NE(name.back(), '_') << name << " must not end with an underscore";
        for (char const c : name)
        {
            EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
                << name << " contains '" << c << "', which is not lower_snake_case";
        }
        EXPECT_EQ(name.find("__"), std::string_view::npos)
            << name << " must not contain a double underscore";
    }
}

TEST(RpcMetricNames, namesEndInTheCountedNounAndNotAUnitOrCounterSuffix)
{
    // Both instruments count things. `_total` is the Prometheus counter suffix
    // and `_count`/`_sum`/`_bucket` are the ones the exporter appends to a
    // histogram, so a base name ending in any of them collides with a series
    // the exporter generates. `_us`/`_ms`/`_bytes` would claim a unit these
    // values do not have.
    EXPECT_EQ(lastSegment(kRpcBatchSize), "size");
    EXPECT_EQ(lastSegment(kPathfindDiscoveredPaths), "paths");

    for (std::string_view const name :
         {std::string_view{kRpcBatchSize}, std::string_view{kPathfindDiscoveredPaths}})
    {
        for (std::string_view const reserved :
             {"total", "count", "sum", "bucket", "us", "ms", "s", "seconds", "bytes"})
        {
            EXPECT_NE(lastSegment(name), reserved)
                << name << " ends in the reserved suffix _" << reserved;
        }
    }
}

TEST(RpcMetricBucketFit, objectCountLadderSeparatesTheSmallestCounts)
{
    using buckets::kObjectCountBuckets;

    // The five edges that carry both distributions. Stated exactly: raising the
    // floor is the defect this test exists to catch.
    ASSERT_GE(kObjectCountBuckets.size(), 5u);
    EXPECT_EQ(kObjectCountBuckets[0], 1.0);
    EXPECT_EQ(kObjectCountBuckets[1], 2.0);
    EXPECT_EQ(kObjectCountBuckets[2], 4.0);
    EXPECT_EQ(kObjectCountBuckets[3], 8.0);
    EXPECT_EQ(kObjectCountBuckets[4], 16.0);

    std::span<double const> const ladder{kObjectCountBuckets};

    // A pathfinding pass that found nothing shares bucket 0 with a pass that
    // found exactly one path; every larger count is separated.
    EXPECT_EQ(bucketIndex(ladder, 0.0), 0u);
    EXPECT_EQ(bucketIndex(ladder, 1.0), 0u);
    EXPECT_EQ(bucketIndex(ladder, 2.0), 1u);
    EXPECT_EQ(bucketIndex(ladder, 3.0), 2u);
    EXPECT_EQ(bucketIndex(ladder, 4.0), 2u);
    EXPECT_EQ(bucketIndex(ladder, 8.0), 3u);
    EXPECT_EQ(bucketIndex(ladder, 16.0), 4u);
}

TEST(RpcMetricBucketFit, theSdkDefaultLadderWouldCollapseEverySmallBatch)
{
    // This is why both instruments get an explicit-bucket view. On the SDK
    // default ladder every batch from 1 to 5 sub-requests lands in one bucket,
    // so histogram_quantile interpolates inside it and returns the edge scaled
    // by the quantile -- a number that looks like a batch size and is not one.
    std::span<double const> const sdk{kSdkDefaultBuckets};
    EXPECT_EQ(bucketIndex(sdk, 1.0), 1u);
    EXPECT_EQ(bucketIndex(sdk, 2.0), 1u);
    EXPECT_EQ(bucketIndex(sdk, 4.0), 1u);
    EXPECT_EQ(bucketIndex(sdk, 5.0), 1u);

    // The chosen ladder spreads those same four values over three buckets.
    std::span<double const> const chosen{buckets::kObjectCountBuckets};
    EXPECT_EQ(bucketIndex(chosen, 1.0), 0u);
    EXPECT_EQ(bucketIndex(chosen, 2.0), 1u);
    EXPECT_EQ(bucketIndex(chosen, 4.0), 2u);
    EXPECT_EQ(bucketIndex(chosen, 5.0), 3u);
}

TEST(RpcMetricBucketFit, aMaximumSizedBatchStillOverflowsTheLadderCeiling)
{
    // The documented limitation of rpc_batch_size, as an executable fact.
    // Nothing caps the sub-request count except the request-size limit, so the
    // largest possible batch is far above the ladder's top edge and lands in
    // `+Inf`. RpcMetricNames.h documents the query that counts the overflow.
    // If the ladder is ever raised past this bound, this test goes red and that
    // note has to change with it.
    constexpr int kLargestPossibleBatch = rpc::tuning::kMaxRequestSize / kMinBatchItemBytes;
    EXPECT_EQ(kLargestPossibleBatch, 333'333);
    EXPECT_EQ(buckets::kObjectCountBuckets.back(), 12'288.0);
    EXPECT_GT(static_cast<double>(kLargestPossibleBatch), buckets::kObjectCountBuckets.back());

    // A pass-through check on the placement helper: the overflow really does
    // land in the +Inf bucket, whose index is one past the last edge.
    std::span<double const> const ladder{buckets::kObjectCountBuckets};
    EXPECT_EQ(bucketIndex(ladder, static_cast<double>(kLargestPossibleBatch)), ladder.size());
}

// The placement helper must also disagree with the ladder when it should. A
// helper that always returned 0 would let every index assertion above pass.
TEST(RpcMetricBucketFit, bucketIndexPlacesAboveAndBelowEveryEdge)
{
    constexpr std::array probe{10.0, 20.0};
    std::span<double const> const ladder{probe};
    EXPECT_EQ(bucketIndex(ladder, 9.9), 0u);
    EXPECT_EQ(bucketIndex(ladder, 10.0), 0u);
    EXPECT_EQ(bucketIndex(ladder, 10.1), 1u);
    EXPECT_EQ(bucketIndex(ladder, 20.0), 1u);
    EXPECT_EQ(bucketIndex(ladder, 20.1), 2u);
}

}  // namespace xrpl::telemetry
