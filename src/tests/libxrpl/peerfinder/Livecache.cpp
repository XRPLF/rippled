#include <xrpl/peerfinder/detail/Livecache.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/net/IPAddressV4.h>
#include <xrpl/beast/net/IPAddressV6.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
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

class LivecacheTest : public ::testing::Test
{
protected:
    static beast::Journal
    journal()
    {
        return beast::Journal{TestSink::instance()};
    }

    static beast::IP::Endpoint
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

    void
    addEndpoint(beast::IP::Endpoint const& ep, std::uint32_t hops = 0)
    {
        cache_.insert(Endpoint{ep, hops});
    }

    TestStopwatch clock_;
    Livecache<> cache_{clock_, journal()};
};

}  // namespace

TEST_F(LivecacheTest, basic_insert)
{
    EXPECT_TRUE(cache_.empty());

    for (auto i = 0; i < 10; ++i)
        addEndpoint(endpoint(i, true));

    EXPECT_FALSE(cache_.empty());
    EXPECT_EQ(cache_.size(), 10u);

    for (auto i = 10; i < 20; ++i)
        addEndpoint(endpoint(i, false));

    EXPECT_FALSE(cache_.empty());
    EXPECT_EQ(cache_.size(), 20u);
}

TEST_F(LivecacheTest, insert_update_keeps_lowest_hop_count)
{
    auto const ep1 = Endpoint{endpoint(1), 2};
    cache_.insert(ep1);
    ASSERT_EQ(cache_.size(), 1u);
    EXPECT_EQ((cache_.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep2 = Endpoint{ep1.address, 4};
    cache_.insert(ep2);
    EXPECT_EQ(cache_.size(), 1u);
    EXPECT_EQ((cache_.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep3 = Endpoint{ep1.address, 2};
    cache_.insert(ep3);
    EXPECT_EQ(cache_.size(), 1u);
    EXPECT_EQ((cache_.hops.begin() + 2)->begin()->hops, 2u);

    auto const ep4 = Endpoint{ep1.address, 1};
    cache_.insert(ep4);
    EXPECT_EQ(cache_.size(), 1u);
    EXPECT_EQ((cache_.hops.begin() + 1)->begin()->hops, 1u);
}

TEST_F(LivecacheTest, hop_iterators_support_const_reverse_and_move_back)
{
    auto const ep1 = Endpoint{endpoint(1), 1};
    auto const ep2 = Endpoint{endpoint(2), 1};
    cache_.insert(ep1);
    cache_.insert(ep2);

    auto hop = *(cache_.hops.begin() + 1);
    ASSERT_NE(hop.begin(), hop.end());
    ASSERT_NE(hop.cbegin(), hop.cend());
    ASSERT_NE(hop.rbegin(), hop.rend());
    ASSERT_NE(hop.crbegin(), hop.crend());

    auto const firstAddress = hop.begin()->address;
    hop.moveBack(hop.begin());
    EXPECT_EQ(hop.rbegin()->address, firstAddress);

    auto const& constHops = cache_.hops;
    EXPECT_NE(constHops.begin(), constHops.end());
    EXPECT_NE(constHops.cbegin(), constHops.cend());
    EXPECT_NE(constHops.rbegin(), constHops.rend());
    EXPECT_NE(constHops.crbegin(), constHops.crend());

    auto const constHop = *(constHops.cbegin() + 1);
    EXPECT_EQ(std::distance(constHop.begin(), constHop.end()), 2);
    EXPECT_EQ(std::distance(constHop.cbegin(), constHop.cend()), 2);
    EXPECT_EQ(std::distance(constHop.rbegin(), constHop.rend()), 2);
    EXPECT_EQ(std::distance(constHop.crbegin(), constHop.crend()), 2);
}

TEST_F(LivecacheTest, on_write_reports_entries_and_expiration)
{
    cache_.insert(Endpoint{endpoint(1), 1});
    cache_.insert(Endpoint{endpoint(2), Tuning::kMaxHops + 1});

    JsonPropertyStream stream;
    {
        beast::PropertyStream::Map map(stream);
        cache_.onWrite(map);
    }

    auto const& top = stream.top();
    EXPECT_EQ(top["size"].asUInt(), 2u);
    EXPECT_FALSE(top["hist"].asString().empty());
    ASSERT_TRUE(top.isMember("entries"));
    ASSERT_EQ(top["entries"].size(), 2u);
    auto const& entry = top["entries"][json::UInt{0}];
    EXPECT_TRUE(entry.isMember("hops"));
    EXPECT_TRUE(entry.isMember("address"));
    EXPECT_TRUE(entry.isMember("expires"));
}

TEST_F(LivecacheTest, expire_removes_entries_after_ttl)
{
    using namespace std::chrono_literals;

    cache_.insert(Endpoint{endpoint(1), 1});
    ASSERT_EQ(cache_.size(), 1u);

    cache_.expire();
    EXPECT_EQ(cache_.size(), 1u);

    clock_.advance(Tuning::kLiveCacheSecondsToLive - 1s);
    cache_.expire();
    EXPECT_EQ(cache_.size(), 1u);

    clock_.advance(1s);
    cache_.expire();
    EXPECT_TRUE(cache_.empty());
}

TEST_F(LivecacheTest, expire_removes_multiple_entries_after_ttl)
{
    using namespace std::chrono_literals;

    cache_.insert(Endpoint{endpoint(1), 1});
    cache_.insert(Endpoint{endpoint(2), 2});

    clock_.advance(Tuning::kLiveCacheSecondsToLive);
    cache_.expire();
    EXPECT_TRUE(cache_.empty());
}

TEST_F(LivecacheTest, histogram_counts_all_entries)
{
    constexpr auto kNumEndpoints = 40;

    for (auto i = 0; i < kNumEndpoints; ++i)
    {
        addEndpoint(endpoint(static_cast<std::uint16_t>(i)), xrpl::randInt<std::uint32_t>());
    }

    auto const histogram = cache_.hops.histogram();
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
    EXPECT_EQ(sum, kNumEndpoints);
}

TEST_F(LivecacheTest, shuffle_preserves_bucket_contents)
{
    for (auto i = 0; i < 100; ++i)
    {
        addEndpoint(endpoint(static_cast<std::uint16_t>(i)), xrpl::randInt(Tuning::kMaxHops + 1));
    }

    using AtHop = std::vector<Endpoint>;
    using AllHops = std::array<AtHop, 1 + Tuning::kMaxHops + 1>;

    auto const compareEndpoint = [](Endpoint const& lhs, Endpoint const& rhs) {
        return rhs.hops < lhs.hops || (rhs.hops == lhs.hops && rhs.address < lhs.address);
    };
    auto const sameEndpoint = [](Endpoint const& lhs, Endpoint const& rhs) {
        return lhs.hops == rhs.hops && lhs.address == rhs.address;
    };
    auto const sameEndpoints =
        [&sameEndpoint](std::vector<Endpoint> const& lhs, std::vector<Endpoint> const& rhs) {
            return lhs.size() == rhs.size() &&
                std::equal(lhs.begin(), lhs.end(), rhs.begin(), sameEndpoint);
        };

    AllHops before;
    AllHops beforeSorted;
    for (auto i = std::make_pair(0, cache_.hops.begin()); i.second != cache_.hops.end();
         ++i.first, ++i.second)
    {
        std::ranges::copy(*i.second, std::back_inserter(before[i.first]));
        std::ranges::copy(*i.second, std::back_inserter(beforeSorted[i.first]));
        std::ranges::sort(beforeSorted[i.first], compareEndpoint);
    }

    cache_.hops.shuffle();

    AllHops after;
    AllHops afterSorted;
    for (auto i = std::make_pair(0, cache_.hops.begin()); i.second != cache_.hops.end();
         ++i.first, ++i.second)
    {
        std::ranges::copy(*i.second, std::back_inserter(after[i.first]));
        std::ranges::copy(*i.second, std::back_inserter(afterSorted[i.first]));
        std::ranges::sort(afterSorted[i.first], compareEndpoint);
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
