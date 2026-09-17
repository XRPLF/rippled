#include <xrpl/basics/partitioned_unordered_map.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <string>

namespace xrpl {

using TestMap = PartitionedUnorderedMap<std::size_t, std::string, std::hash<std::size_t>>;

TEST(PartitionedUnorderedMapTest, default_partition_count_is_two)
{
    // Callers that walk the whole map run one thread per partition, so this
    // count bounds that thread count. It must not follow the core count.
    EXPECT_EQ(TestMap::kDefaultPartitions, 2u);

    TestMap const map;
    EXPECT_EQ(map.partitions(), TestMap::kDefaultPartitions);
}

TEST(PartitionedUnorderedMapTest, zero_partitions_falls_back_to_the_default)
{
    TestMap const map{0};
    EXPECT_EQ(map.partitions(), TestMap::kDefaultPartitions);
}

TEST(PartitionedUnorderedMapTest, explicit_partition_count_is_honoured)
{
    TestMap const map{TestMap::kDefaultPartitions + 5};
    EXPECT_EQ(map.partitions(), TestMap::kDefaultPartitions + 5);
}

TEST(PartitionedUnorderedMapTest, every_key_is_reachable_across_partitions)
{
    // The partitioner is key % partitions, so with two partitions the even
    // keys land in one sub-map and the odd keys in the other.
    std::size_t const count = 10;
    TestMap map;

    for (std::size_t key = 0; key < count; ++key)
        map.emplace(key, std::to_string(key));

    EXPECT_EQ(map.size(), count);

    // Pin the split itself, not just the total: a partitioner that sent every
    // key to one sub-map would still satisfy size() and the lookups below.
    ASSERT_EQ(map.map().size(), 2u);
    EXPECT_EQ(map.map()[0].size(), count / 2);
    EXPECT_EQ(map.map()[1].size(), count / 2);

    for (std::size_t key = 0; key < count; ++key)
    {
        auto const it = map.find(key);
        ASSERT_NE(it, map.end());
        EXPECT_EQ(it->second, std::to_string(key));
    }

    std::size_t visited = 0;
    for (auto const& entry : map)
    {
        EXPECT_EQ(entry.second, std::to_string(entry.first));
        ++visited;
    }
    EXPECT_EQ(visited, count);
}

}  // namespace xrpl
