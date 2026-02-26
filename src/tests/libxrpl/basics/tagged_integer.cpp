#include <xrpl/basics/tagged_integer.h>

#include <gtest/gtest.h>

#include <type_traits>

using namespace xrpl;

struct Tag1
{
};
struct Tag2
{
};

// Static checks that types are not interoperable

using TagUInt1 = tagged_integer<std::uint32_t, Tag1>;
using TagUInt2 = tagged_integer<std::uint32_t, Tag2>;
using TagUInt3 = tagged_integer<std::uint64_t, Tag1>;

// Check construction of tagged_integers
static_assert(
    std::is_constructible<TagUInt1, std::uint32_t>::value,
    "TagUInt1 should be constructible using a std::uint32_t");

static_assert(
    !std::is_constructible<TagUInt1, std::uint64_t>::value,
    "TagUInt1 should not be constructible using a std::uint64_t");

static_assert(
    std::is_constructible<TagUInt3, std::uint32_t>::value,
    "TagUInt3 should be constructible using a std::uint32_t");

static_assert(
    std::is_constructible<TagUInt3, std::uint64_t>::value,
    "TagUInt3 should be constructible using a std::uint64_t");

// Check assignment of tagged_integers
static_assert(
    !std::is_assignable<TagUInt1, std::uint32_t>::value,
    "TagUInt1 should not be assignable with a std::uint32_t");

static_assert(
    !std::is_assignable<TagUInt1, std::uint64_t>::value,
    "TagUInt1 should not be assignable with a std::uint64_t");

static_assert(
    !std::is_assignable<TagUInt3, std::uint32_t>::value,
    "TagUInt3 should not be assignable with a std::uint32_t");

static_assert(
    !std::is_assignable<TagUInt3, std::uint64_t>::value,
    "TagUInt3 should not be assignable with a std::uint64_t");

static_assert(std::is_assignable<TagUInt1, TagUInt1>::value, "TagUInt1 should be assignable with a TagUInt1");

static_assert(!std::is_assignable<TagUInt1, TagUInt2>::value, "TagUInt1 should not be assignable with a TagUInt2");

static_assert(std::is_assignable<TagUInt3, TagUInt3>::value, "TagUInt3 should be assignable with a TagUInt1");

static_assert(!std::is_assignable<TagUInt1, TagUInt3>::value, "TagUInt1 should not be assignable with a TagUInt3");

static_assert(!std::is_assignable<TagUInt3, TagUInt1>::value, "TagUInt3 should not be assignable with a TagUInt1");

// Check convertibility of tagged_integers
static_assert(
    !std::is_convertible<std::uint32_t, TagUInt1>::value,
    "std::uint32_t should not be convertible to a TagUInt1");

static_assert(
    !std::is_convertible<std::uint32_t, TagUInt3>::value,
    "std::uint32_t should not be convertible to a TagUInt3");

static_assert(
    !std::is_convertible<std::uint64_t, TagUInt3>::value,
    "std::uint64_t should not be convertible to a TagUInt3");

static_assert(
    !std::is_convertible<std::uint64_t, TagUInt2>::value,
    "std::uint64_t should not be convertible to a TagUInt2");

static_assert(!std::is_convertible<TagUInt1, TagUInt2>::value, "TagUInt1 should not be convertible to TagUInt2");

static_assert(!std::is_convertible<TagUInt1, TagUInt3>::value, "TagUInt1 should not be convertible to TagUInt3");

static_assert(!std::is_convertible<TagUInt2, TagUInt3>::value, "TagUInt2 should not be convertible to a TagUInt3");

using TagInt = tagged_integer<std::int32_t, Tag1>;

TEST(tagged_integer, comparison_operators)
{
    TagInt const zero(0);
    TagInt const one(1);

    EXPECT_TRUE(one == one);
    EXPECT_FALSE(one == zero);

    EXPECT_TRUE(one != zero);
    EXPECT_FALSE(one != one);

    EXPECT_TRUE(zero < one);
    EXPECT_FALSE(one < zero);

    EXPECT_TRUE(one > zero);
    EXPECT_FALSE(zero > one);

    EXPECT_TRUE(one >= one);
    EXPECT_TRUE(one >= zero);
    EXPECT_FALSE(zero >= one);

    EXPECT_TRUE(zero <= one);
    EXPECT_TRUE(zero <= zero);
    EXPECT_FALSE(one <= zero);
}

TEST(tagged_integer, increment_decrement_operators)
{
    TagInt const zero(0);
    TagInt const one(1);
    TagInt a{0};
    ++a;
    EXPECT_EQ(a, one);
    --a;
    EXPECT_EQ(a, zero);
    a++;
    EXPECT_EQ(a, one);
    a--;
    EXPECT_EQ(a, zero);
}

TEST(tagged_integer, arithmetic_operators)
{
    TagInt a{-2};
    EXPECT_EQ(+a, TagInt{-2});
    EXPECT_EQ(-a, TagInt{2});
    EXPECT_EQ(TagInt{-3} + TagInt{4}, TagInt{1});
    EXPECT_EQ(TagInt{-3} - TagInt{4}, TagInt{-7});
    EXPECT_EQ(TagInt{-3} * TagInt{4}, TagInt{-12});
    EXPECT_EQ(TagInt{8} / TagInt{4}, TagInt{2});
    EXPECT_EQ(TagInt{7} % TagInt{4}, TagInt{3});

    EXPECT_EQ(~TagInt{8}, TagInt{~TagInt::value_type{8}});
    EXPECT_EQ((TagInt{6} & TagInt{3}), TagInt{2});
    EXPECT_EQ((TagInt{6} | TagInt{3}), TagInt{7});
    EXPECT_EQ((TagInt{6} ^ TagInt{3}), TagInt{5});

    EXPECT_EQ((TagInt{4} << TagInt{2}), TagInt{16});
    EXPECT_EQ((TagInt{16} >> TagInt{2}), TagInt{4});
}

TEST(tagged_integer, assignment_operators)
{
    TagInt a{-2};
    TagInt b{0};
    b = a;
    EXPECT_EQ(b, TagInt{-2});

    // -3 + 4 == 1
    a = TagInt{-3};
    a += TagInt{4};
    EXPECT_EQ(a, TagInt{1});

    // -3 - 4 == -7
    a = TagInt{-3};
    a -= TagInt{4};
    EXPECT_EQ(a, TagInt{-7});

    // -3 * 4 == -12
    a = TagInt{-3};
    a *= TagInt{4};
    EXPECT_EQ(a, TagInt{-12});

    // 8/4 == 2
    a = TagInt{8};
    a /= TagInt{4};
    EXPECT_EQ(a, TagInt{2});

    // 7 % 4 == 3
    a = TagInt{7};
    a %= TagInt{4};
    EXPECT_EQ(a, TagInt{3});

    // 6 & 3 == 2
    a = TagInt{6};
    a /= TagInt{3};
    EXPECT_EQ(a, TagInt{2});

    // 6 | 3 == 7
    a = TagInt{6};
    a |= TagInt{3};
    EXPECT_EQ(a, TagInt{7});

    // 6 ^ 3 == 5
    a = TagInt{6};
    a ^= TagInt{3};
    EXPECT_EQ(a, TagInt{5});

    // 4 << 2 == 16
    a = TagInt{4};
    a <<= TagInt{2};
    EXPECT_EQ(a, TagInt{16});

    // 16 >> 2 == 4
    a = TagInt{16};
    a >>= TagInt{2};
    EXPECT_EQ(a, TagInt{4});
}
