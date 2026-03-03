#include <xrpl/basics/RangeSet.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

using namespace xrpl;

TEST(RangeSet, prevMissing)
{
    // Set will include:
    // [ 0, 5]
    // [10,15]
    // [20,25]
    // etc...

    RangeSet<std::uint32_t> set;
    for (std::uint32_t i = 0; i < 10; ++i)
        set.insert(range(10 * i, 10 * i + 5));

    for (std::uint32_t i = 1; i < 100; ++i)
    {
        std::optional<std::uint32_t> expected;
        // no prev missing in domain for i <= 6
        if (i > 6)
        {
            std::uint32_t const oneBelowRange = (10 * (i / 10)) - 1;

            expected = ((i % 10) > 6) ? (i - 1) : oneBelowRange;
        }
        EXPECT_EQ(prevMissing(set, i), expected);
    }
}

TEST(RangeSet, toString)
{
    RangeSet<std::uint32_t> set;
    EXPECT_EQ(to_string(set), "empty");

    set.insert(1);
    EXPECT_EQ(to_string(set), "1");

    set.insert(range(4u, 6u));
    EXPECT_EQ(to_string(set), "1,4-6");

    set.insert(2);
    EXPECT_EQ(to_string(set), "1-2,4-6");

    set.erase(range(4u, 5u));
    EXPECT_EQ(to_string(set), "1-2,6");
}

TEST(RangeSet, fromString)
{
    RangeSet<std::uint32_t> set;

    EXPECT_FALSE(from_string(set, ""));
    EXPECT_EQ(boost::icl::length(set), 0);

    EXPECT_FALSE(from_string(set, "#"));
    EXPECT_EQ(boost::icl::length(set), 0);

    EXPECT_FALSE(from_string(set, ","));
    EXPECT_EQ(boost::icl::length(set), 0);

    EXPECT_FALSE(from_string(set, ",-"));
    EXPECT_EQ(boost::icl::length(set), 0);

    EXPECT_FALSE(from_string(set, "1,,2"));
    EXPECT_EQ(boost::icl::length(set), 0);

    EXPECT_TRUE(from_string(set, "1"));
    EXPECT_EQ(boost::icl::length(set), 1);
    EXPECT_EQ(boost::icl::first(set), 1);

    EXPECT_TRUE(from_string(set, "1,1"));
    EXPECT_EQ(boost::icl::length(set), 1);
    EXPECT_EQ(boost::icl::first(set), 1);

    EXPECT_TRUE(from_string(set, "1-1"));
    EXPECT_EQ(boost::icl::length(set), 1);
    EXPECT_EQ(boost::icl::first(set), 1);

    EXPECT_TRUE(from_string(set, "1,4-6"));
    EXPECT_EQ(boost::icl::length(set), 4);
    EXPECT_EQ(boost::icl::first(set), 1);
    EXPECT_FALSE(boost::icl::contains(set, 2));
    EXPECT_FALSE(boost::icl::contains(set, 3));
    EXPECT_TRUE(boost::icl::contains(set, 4));
    EXPECT_TRUE(boost::icl::contains(set, 5));
    EXPECT_EQ(boost::icl::last(set), 6);

    EXPECT_TRUE(from_string(set, "1-2,4-6"));
    EXPECT_EQ(boost::icl::length(set), 5);
    EXPECT_EQ(boost::icl::first(set), 1);
    EXPECT_TRUE(boost::icl::contains(set, 2));
    EXPECT_TRUE(boost::icl::contains(set, 4));
    EXPECT_EQ(boost::icl::last(set), 6);

    EXPECT_TRUE(from_string(set, "1-2,6"));
    EXPECT_EQ(boost::icl::length(set), 3);
    EXPECT_EQ(boost::icl::first(set), 1);
    EXPECT_TRUE(boost::icl::contains(set, 2));
    EXPECT_EQ(boost::icl::last(set), 6);
}
