/**
 * GTest unit tests for the histogram bucket ladders.
 *
 * These ladders decide whether a Grafana percentile panel reports a
 * measurement or an artefact, and neither failure mode is visible in the
 * panel itself: a quantile that falls in the `+Inf` bucket reads back as the
 * second-highest edge, and one that falls inside bucket 0 is interpolated.
 * Both look like plausible numbers. So the invariants are asserted here
 * rather than left to review.
 *
 * The ladders are `constexpr`, so most of this could be `static_assert`.
 * They are runtime tests as well so that a failure names which edge is
 * wrong instead of only failing the compile.
 */

#include <xrpl/telemetry/HistogramBuckets.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace xrpl::telemetry::buckets {

// Every ladder must be strictly ascending and non-negative. The SDK places a
// sample with std::lower_bound over the edges, so a duplicated or
// out-of-order edge silently sends samples to the wrong bucket.
class HistogramBucketsTest : public ::testing::TestWithParam<std::span<double const>>
{
};

TEST_P(HistogramBucketsTest, isStrictlyAscending)
{
    auto const ladder = GetParam();
    ASSERT_FALSE(ladder.empty());
    for (std::size_t i = 1; i < ladder.size(); ++i)
        EXPECT_LT(ladder[i - 1], ladder[i]) << "edge index " << i << " does not ascend";
}

TEST_P(HistogramBucketsTest, isNonNegativeAndFinite)
{
    for (double const edge : GetParam())
    {
        EXPECT_GE(edge, 0.0);
        EXPECT_TRUE(std::isfinite(edge)) << "edge " << edge << " is not finite";
    }
}

TEST_P(HistogramBucketsTest, passesTheCompileTimeValidator)
{
    EXPECT_TRUE(isAscendingNonNegative(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    AllLadders,
    HistogramBucketsTest,
    ::testing::Values(
        std::span<double const>{kMillisecondBuckets},
        std::span<double const>{kByteBuckets}));

// The validator must also REJECT. A predicate that only ever returns true
// would let every ladder above pass while proving nothing.
TEST(HistogramBucketsValidator, rejectsEmptyDescendingDuplicateAndNegative)
{
    EXPECT_FALSE(isAscendingNonNegative(std::span<double const>{}));

    constexpr std::array descending{5.0, 1.0};
    EXPECT_FALSE(isAscendingNonNegative(descending));

    constexpr std::array duplicated{1.0, 1.0, 2.0};
    EXPECT_FALSE(isAscendingNonNegative(duplicated));

    constexpr std::array negative{-1.0, 1.0};
    EXPECT_FALSE(isAscendingNonNegative(negative));
}

TEST(HistogramBucketsValidator, acceptsASingleEdgeAndALeadingZero)
{
    constexpr std::array single{1.0};
    EXPECT_TRUE(isAscendingNonNegative(single));

    // A leading zero is legal: the GetObject charge ladder starts at 0 to
    // separate the free tier from everything else.
    constexpr std::array leadingZero{0.0, 100.0};
    EXPECT_TRUE(isAscendingNonNegative(leadingZero));
}

TEST(HistogramBucketsRange, millisecondFloorIsOneAndCeilingCoversTheSlowestJob)
{
    // beast::insight::Event rounds durations up to whole milliseconds, so 1
    // is the smallest edge that can ever collect a sample.
    EXPECT_EQ(kMillisecondBuckets.front(), 1.0);

    // The updatepaths job type was measured averaging 59,956 ms. A 30 s
    // ceiling -- the collector's top edge -- would censor it just as the old
    // 5 s ceiling does, so this ladder has to reach further.
    EXPECT_GE(kMillisecondBuckets.back(), 120'000.0);
}

TEST(HistogramBucketsRange, millisecondLadderClearsTheMeasuredCensoringPoint)
{
    // rpc_size had 24.9% of samples above the old 5000 ceiling and
    // jobq_updatepaths had 100%. A ceiling at or below 5000 reintroduces the
    // exact defect this ladder exists to fix.
    EXPECT_GT(kMillisecondBuckets.back(), 5'000.0);
}

TEST(HistogramBucketsRange, millisecondLadderContainsEveryRepresentableCollectorEdge)
{
    // Agreement with the collector's spanmetrics ladder over the shared
    // range is the invariant; edges above its 30 s top are allowed because
    // jobs outlive spans. Sub-millisecond collector edges are excluded
    // because Event cannot represent them. check_bucket_parity.py enforces
    // this against the YAML; this test pins it for the C++ side alone so a
    // local edit fails fast.
    constexpr std::array collectorEdges{
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
        30'000.0};

    for (double const edge : collectorEdges)
    {
        EXPECT_NE(std::ranges::find(kMillisecondBuckets, edge), kMillisecondBuckets.end())
            << edge << " ms is a collector spanmetrics edge and must be present";
    }
}

TEST(HistogramBucketsRange, millisecondLadderResolvesTheOneToFiveSecondBand)
{
    // Without these the 1 s to 5 s span was one four-second-wide bucket, so
    // any quantile landing inside it was interpolated across four seconds.
    for (double const edge : {2'000.0, 3'000.0, 4'000.0})
    {
        EXPECT_NE(std::ranges::find(kMillisecondBuckets, edge), kMillisecondBuckets.end())
            << edge << " ms edge missing";
    }
}

TEST(HistogramBucketsRange, byteLadderBracketsTheMeasuredResponseDistribution)
{
    // Measured: mean 2131 B, half under 1 kB, three quarters under 5 kB, and
    // the tail above 5 kB has a mean of at most 7538 B -- which puts p99
    // near 80 kB. The floor must sit at or below the measured median region
    // and the ceiling well past the p99 bound.
    EXPECT_LE(kByteBuckets.front(), 512.0);
    EXPECT_GE(kByteBuckets.back(), 1'048'576.0);

    // Most of the resolution belongs where the distribution actually turns.
    auto const withinWorkingRange =
        std::ranges::count_if(kByteBuckets, [](double e) { return e >= 512.0 && e <= 65'536.0; });
    EXPECT_GE(withinWorkingRange, 6) << "too little resolution between 512 B and 64 kB";
}

TEST(HistogramBucketsRange, byteAndMillisecondLaddersAreDistinct)
{
    // A single shared ladder is what put a byte count on a latency scale and
    // censored a quarter of its samples.
    EXPECT_NE(kByteBuckets.size(), kMillisecondBuckets.size());
    EXPECT_GT(kByteBuckets.back(), kMillisecondBuckets.back());
}

TEST(HistogramBucketsConvert, toVectorPreservesOrderAndSize)
{
    auto const converted = toVector(kByteBuckets);
    ASSERT_EQ(converted.size(), kByteBuckets.size());
    EXPECT_TRUE(std::ranges::equal(converted, kByteBuckets));
}

TEST(HistogramBucketsConvert, toVectorHandlesAnEmptyLadder)
{
    EXPECT_TRUE(toVector(std::span<double const>{}).empty());
}

}  // namespace xrpl::telemetry::buckets
