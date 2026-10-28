#include <xrpl/shamap/SHAMapItem.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace xrpl::tests {

namespace {

// An arbitrary well-formed key.
constexpr UInt256 kItemKey("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

}  // namespace

TEST(SHAMapItemTest, item_at_the_size_bound_is_built)
{
    std::vector<std::uint8_t> const data(kMaxSHAMapItemBytes, 1);
    auto const item = makeShamapitem(kItemKey, makeSlice(data));
    ASSERT_TRUE(item);
    EXPECT_EQ(item->size(), data.size());
    EXPECT_EQ(item->key(), kItemKey);
}

TEST(SHAMapItemTest, item_above_the_size_bound_is_refused)
{
    // One byte over the bound.
    std::vector<std::uint8_t> const data(kMaxSHAMapItemBytes + 1, 1);
    EXPECT_THROW(makeShamapitem(kItemKey, makeSlice(data)), std::length_error);
}

}  // namespace xrpl::tests
