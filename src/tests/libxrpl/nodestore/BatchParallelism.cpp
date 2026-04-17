#include <xrpl/nodestore/Backend.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace xrpl;
using namespace xrpl::NodeStore;

// Helper function to convert the pair result into ranges for testing.
std::vector<std::pair<unsigned int, unsigned int>>
calculateRanges(unsigned int batchSize, unsigned int maxThreadCount)
{
    auto const [numThreads, numItems] =
        Backend::calculateBatchParallelism(batchSize, maxThreadCount);

    std::vector<std::pair<unsigned int, unsigned int>> ranges;
    ranges.reserve(numThreads);

    for (unsigned int t = 0; t < numThreads; ++t)
    {
        auto const startIdx = t * numItems;
        auto const endIdx = std::min(startIdx + numItems, batchSize);
        ranges.emplace_back(startIdx, endIdx);
    }

    return ranges;
}

TEST(BatchParallelism, EmptyBatch)
{
    // Empty batch should return 0 threads.
    {
        auto const batchSize = 0u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 0u);
        EXPECT_EQ(numItems, 0u);

        // Verify ranges calculation.
        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        EXPECT_EQ(ranges.size(), numThreads);
    }
}

TEST(BatchParallelism, SmallBatches)
{
    // Batch size 1 should use 1 thread.
    {
        auto const batchSize = 1u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 1u);
        EXPECT_EQ(numItems, 1u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 1u);
    }

    // Batch size 2 should use 1 thread.
    {
        auto const batchSize = 2u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 1u);
        EXPECT_EQ(numItems, 2u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 2u);
    }

    // Batch size 3 should use 1 thread.
    {
        auto const batchSize = 3u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 1u);
        EXPECT_EQ(numItems, 3u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 3u);
    }

    // Batch size 4 should use 1 thread (exactly 4 items).
    {
        auto const batchSize = 4u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 1u);
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 4u);
    }
}

TEST(BatchParallelism, MediumBatches)
{
    // Batch size 5 should use 2 threads.
    {
        auto const batchSize = 5u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 2u);  // ceil(5/4) = 2
        EXPECT_EQ(numItems, 3u);    // ceil(5/2) = 3

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 3u);
        EXPECT_EQ(ranges[1].first, 3u);
        EXPECT_EQ(ranges[1].second, 5u);
    }

    // Batch size 8 should use 2 threads.
    {
        auto const batchSize = 8u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 2u);
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }

    // Batch size 15 should use 4 threads (ceil(15/4) = 4).
    {
        auto const batchSize = 15u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 4u);
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads - 1; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
        EXPECT_EQ(ranges[numThreads - 1].first, (numThreads - 1) * numItems);
        EXPECT_EQ(ranges[numThreads - 1].second, batchSize);  // Last range gets remaining items.
    }

    // Batch size 22 should use 6 threads.
    {
        auto const batchSize = 22u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 6u);  // ceil(22/4) = 6
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads - 1; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
        EXPECT_EQ(ranges[numThreads - 1].first, (numThreads - 1) * numItems);
        EXPECT_EQ(ranges[numThreads - 1].second, batchSize);
    }

    // Batch size 32 should use 8 threads.
    {
        auto const batchSize = 32u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 8u);
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }
}

TEST(BatchParallelism, LargeBatches)
{
    // Batch size 100 should use 8 threads (max limit).
    {
        auto const batchSize = 100u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 8u);
        EXPECT_EQ(numItems, 13u);  // ceil(100/8) = 13

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads - 1; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
        EXPECT_EQ(ranges[numThreads - 1].first, (numThreads - 1) * numItems);
        EXPECT_EQ(ranges[numThreads - 1].second, batchSize);
    }

    // Batch size 1000 with 8 hw threads.
    {
        auto const batchSize = 1000u;
        auto const maxThreadCount = 8u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 8u);
        EXPECT_EQ(numItems, 125u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }
}

TEST(BatchParallelism, HardwareThreadLimits)
{
    // With only 1 thread available.
    {
        auto const batchSize = 100u;
        auto const maxThreadCount = 1u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 1u);
        EXPECT_EQ(numItems, 100u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        EXPECT_EQ(ranges[0].first, 0u);
        EXPECT_EQ(ranges[0].second, 100u);
    }

    // With 2 threads.
    {
        auto const batchSize = 50u;
        auto const maxThreadCount = 2u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 2u);
        EXPECT_EQ(numItems, 25u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }

    // With 10 threads.
    {
        auto const batchSize = 50u;
        auto const maxThreadCount = 12u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 10u);  // ceil(50/4) = 13, but numThreads = 10.
        EXPECT_EQ(numItems, 5u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }

    // With many threads.
    {
        auto const batchSize = 20u;
        auto const maxThreadCount = 100u;

        auto const [numThreads, numItems] =
            Backend::calculateBatchParallelism(batchSize, maxThreadCount);
        EXPECT_EQ(numThreads, 5u);  // ceil(20/4) = 5, limited by batch size.
        EXPECT_EQ(numItems, 4u);

        auto const ranges = calculateRanges(batchSize, maxThreadCount);
        ASSERT_EQ(ranges.size(), numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            EXPECT_EQ(ranges[i].first, i * numItems);
            EXPECT_EQ(ranges[i].second, (i + 1) * numItems);
        }
    }
}
