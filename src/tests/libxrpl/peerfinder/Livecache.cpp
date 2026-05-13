#include <xrpl/peerfinder/detail/Livecache.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/net/IPAddressV4.h>
#include <xrpl/beast/net/IPAddressV6.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {
namespace {

beast::Journal
journal()
{
    return beast::Journal{TestSink::instance()};
}

beast::IP::Endpoint
endpoint(std::uint16_t index, bool v4 = true)
{
    auto const port = static_cast<std::uint16_t>(10000 + index);

    if (v4)
    {
        auto bytes = beast::IP::AddressV4::bytes_type{
            {54,
             static_cast<std::uint8_t>((index / 256) % 256),
             static_cast<std::uint8_t>(index % 256),
             1}};
        return beast::IP::Endpoint{beast::IP::Address{beast::IP::AddressV4{bytes}}, port};
    }

    auto bytes = beast::IP::AddressV6::bytes_type{
        {0x20,
         0x01,
         0x0d,
         0xb8,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         static_cast<std::uint8_t>((index / 256) % 256),
         static_cast<std::uint8_t>(index % 256),
         1}};
    return beast::IP::Endpoint{beast::IP::Address{beast::IP::AddressV6{bytes}}, port};
}

template <class Cache>
void
addEndpoint(beast::IP::Endpoint const& ep, Cache& cache, std::uint32_t hops = 0)
{
    cache.insert(Endpoint{ep, hops});
}

bool
sameEndpoint(Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.hops == rhs.hops && lhs.address == rhs.address;
}

bool
sameEndpoints(std::vector<Endpoint> const& lhs, std::vector<Endpoint> const& rhs)
{
    return lhs.size() == rhs.size() &&
        std::equal(lhs.begin(), lhs.end(), rhs.begin(), sameEndpoint);
}

}  // namespace

TEST(Livecache, basic_insert)
{
    TestStopwatch clock;
    Livecache<> cache(clock, journal());
    EXPECT_TRUE(cache.empty());

    for (auto i = 0; i < 10; ++i)
        addEndpoint(endpoint(i, true), cache);

    EXPECT_FALSE(cache.empty());
    EXPECT_EQ(cache.size(), 10u);

    for (auto i = 10; i < 20; ++i)
        addEndpoint(endpoint(i, false), cache);

    EXPECT_FALSE(cache.empty());
    EXPECT_EQ(cache.size(), 20u);
}

TEST(Livecache, insert_update_keeps_lowest_hop_count)
{
    TestStopwatch clock;
    Livecache<> cache(clock, journal());

    auto const ep1 = Endpoint{endpoint(1), 2};
    cache.insert(ep1);
    ASSERT_EQ(cache.size(), 1u);
    EXPECT_EQ((cache.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep2 = Endpoint{ep1.address, 4};
    cache.insert(ep2);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ((cache.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep3 = Endpoint{ep1.address, 2};
    cache.insert(ep3);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ((cache.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep4 = Endpoint{ep1.address, 1};
    cache.insert(ep4);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ((cache.hops.begin() + 1)->begin()->hops, 1u);
}

TEST(Livecache, expire_removes_entries_after_ttl)
{
    using namespace std::chrono_literals;

    TestStopwatch clock;
    Livecache<> cache(clock, journal());

    cache.insert(Endpoint{endpoint(1), 1});
    ASSERT_EQ(cache.size(), 1u);

    cache.expire();
    EXPECT_EQ(cache.size(), 1u);

    clock.advance(Tuning::kLiveCacheSecondsToLive - 1s);
    cache.expire();
    EXPECT_EQ(cache.size(), 1u);

    clock.advance(1s);
    cache.expire();
    EXPECT_TRUE(cache.empty());
}

TEST(Livecache, histogram_counts_all_entries)
{
    constexpr auto kNUM_ENDPOINTS = 40;

    TestStopwatch clock;
    Livecache<> cache(clock, journal());
    for (auto i = 0; i < kNUM_ENDPOINTS; ++i)
    {
        addEndpoint(endpoint(static_cast<std::uint16_t>(i)), cache, xrpl::randInt<std::uint32_t>());
    }

    auto const histogram = cache.hops.histogram();
    ASSERT_FALSE(histogram.empty());

    std::vector<std::string> values;
    boost::split(values, histogram, boost::algorithm::is_any_of(","));

    auto sum = 0;
    for (auto const& value : values)
    {
        auto const count = boost::lexical_cast<int>(boost::trim_copy(value));
        sum += count;
        EXPECT_GE(count, 0);
    }
    EXPECT_EQ(sum, kNUM_ENDPOINTS);
}

TEST(Livecache, shuffle_preserves_bucket_contents)
{
    TestStopwatch clock;
    Livecache<> cache(clock, journal());
    for (auto i = 0; i < 100; ++i)
    {
        addEndpoint(
            endpoint(static_cast<std::uint16_t>(i)), cache, xrpl::randInt(Tuning::kMaxHops + 1));
    }

    using AtHop = std::vector<Endpoint>;
    using AllHops = std::array<AtHop, 1 + Tuning::kMaxHops + 1>;

    auto const compareEndpoint = [](Endpoint const& lhs, Endpoint const& rhs) {
        return rhs.hops < lhs.hops || (rhs.hops == lhs.hops && rhs.address < lhs.address);
    };

    AllHops before;
    AllHops beforeSorted;
    for (auto i = std::make_pair(0, cache.hops.begin()); i.second != cache.hops.end();
         ++i.first, ++i.second)
    {
        std::copy((*i.second).begin(), (*i.second).end(), std::back_inserter(before[i.first]));
        std::copy(
            (*i.second).begin(), (*i.second).end(), std::back_inserter(beforeSorted[i.first]));
        std::sort(beforeSorted[i.first].begin(), beforeSorted[i.first].end(), compareEndpoint);
    }

    cache.hops.shuffle();

    AllHops after;
    AllHops afterSorted;
    for (auto i = std::make_pair(0, cache.hops.begin()); i.second != cache.hops.end();
         ++i.first, ++i.second)
    {
        std::copy((*i.second).begin(), (*i.second).end(), std::back_inserter(after[i.first]));
        std::copy((*i.second).begin(), (*i.second).end(), std::back_inserter(afterSorted[i.first]));
        std::sort(afterSorted[i.first].begin(), afterSorted[i.first].end(), compareEndpoint);
    }

    auto allBucketsKeptOriginalOrder = true;
    for (auto i = 0u; i < before.size(); ++i)
    {
        EXPECT_EQ(before[i].size(), after[i].size());
        allBucketsKeptOriginalOrder =
            allBucketsKeptOriginalOrder && sameEndpoints(before[i], after[i]);
        EXPECT_TRUE(sameEndpoints(beforeSorted[i], afterSorted[i]));
    }
    EXPECT_FALSE(allBucketsKeptOriginalOrder);
}

}  // namespace xrpl::PeerFinder
