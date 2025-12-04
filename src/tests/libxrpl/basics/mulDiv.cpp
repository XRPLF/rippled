#include <xrpl/basics/mulDiv.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using namespace xrpl;

TEST(mulDiv, mulDiv)
{
    auto const max = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t const max32 = std::numeric_limits<std::uint32_t>::max();

    auto result = mulDiv(85, 20, 5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 340);
    result = mulDiv(20, 85, 5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 340);

    result = mulDiv(0, max - 1, max - 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    result = mulDiv(max - 1, 0, max - 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    result = mulDiv(max, 2, max / 2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 4);
    result = mulDiv(max, 1000, max / 1000);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1000000);
    result = mulDiv(max, 1000, max / 1001);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1001000);
    result = mulDiv(max32 + 1, max32 + 1, 5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 3689348814741910323);

    // Overflow
    result = mulDiv(max - 1, max - 2, 5);
    EXPECT_FALSE(result.has_value());
}
