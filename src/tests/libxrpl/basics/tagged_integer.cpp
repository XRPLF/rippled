#include <xrpl/basics/tagged_integer.h>

#include <doctest/doctest.h>

#include <type_traits>

using namespace ripple;

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

static_assert(
    std::is_assignable<TagUInt1, TagUInt1>::value,
    "TagUInt1 should be assignable with a TagUInt1");

static_assert(
    !std::is_assignable<TagUInt1, TagUInt2>::value,
    "TagUInt1 should not be assignable with a TagUInt2");

static_assert(
    std::is_assignable<TagUInt3, TagUInt3>::value,
    "TagUInt3 should be assignable with a TagUInt1");

static_assert(
    !std::is_assignable<TagUInt1, TagUInt3>::value,
    "TagUInt1 should not be assignable with a TagUInt3");

static_assert(
    !std::is_assignable<TagUInt3, TagUInt1>::value,
    "TagUInt3 should not be assignable with a TagUInt1");

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

static_assert(
    !std::is_convertible<TagUInt1, TagUInt2>::value,
    "TagUInt1 should not be convertible to TagUInt2");

static_assert(
    !std::is_convertible<TagUInt1, TagUInt3>::value,
    "TagUInt1 should not be convertible to TagUInt3");

static_assert(
    !std::is_convertible<TagUInt2, TagUInt3>::value,
    "TagUInt2 should not be convertible to a TagUInt3");

TEST_SUITE_BEGIN("tagged_integer");

using TagInt = tagged_integer<std::int32_t, Tag1>;

TEST_CASE("comparison operators")
{
    TagInt const zero(0);
    TagInt const one(1);

    CHECK(one == one);
    CHECK(!(one == zero));

    CHECK(one != zero);
    CHECK(!(one != one));

    CHECK(zero < one);
    CHECK(!(one < zero));

    CHECK(one > zero);
    CHECK(!(zero > one));

    CHECK(one >= one);
    CHECK(one >= zero);
    CHECK(!(zero >= one));

    CHECK(zero <= one);
    CHECK(zero <= zero);
    CHECK(!(one <= zero));
}

TEST_CASE("increment / decrement operators")
{
    TagInt const zero(0);
    TagInt const one(1);
    TagInt a{0};
    ++a;
    CHECK(a == one);
    --a;
    CHECK(a == zero);
    a++;
    CHECK(a == one);
    a--;
    CHECK(a == zero);
}

TEST_CASE("arithmetic operators")
{
    TagInt a{-2};
    CHECK(+a == TagInt{-2});
    CHECK(-a == TagInt{2});
    CHECK(TagInt{-3} + TagInt{4} == TagInt{1});
    CHECK(TagInt{-3} - TagInt{4} == TagInt{-7});
    CHECK(TagInt{-3} * TagInt{4} == TagInt{-12});
    CHECK(TagInt{8} / TagInt{4} == TagInt{2});
    CHECK(TagInt{7} % TagInt{4} == TagInt{3});

    CHECK(~TagInt{8} == TagInt{~TagInt::value_type{8}});
    CHECK((TagInt{6} & TagInt{3}) == TagInt{2});
    CHECK((TagInt{6} | TagInt{3}) == TagInt{7});
    CHECK((TagInt{6} ^ TagInt{3}) == TagInt{5});

    CHECK((TagInt{4} << TagInt{2}) == TagInt{16});
    CHECK((TagInt{16} >> TagInt{2}) == TagInt{4});
}

TEST_CASE("assignment operators")
{
    TagInt a{-2};
    TagInt b{0};
    b = a;
    CHECK(b == TagInt{-2});

    // -3 + 4 == 1
    a = TagInt{-3};
    a += TagInt{4};
    CHECK(a == TagInt{1});

    // -3 - 4 == -7
    a = TagInt{-3};
    a -= TagInt{4};
    CHECK(a == TagInt{-7});

    // -3 * 4 == -12
    a = TagInt{-3};
    a *= TagInt{4};
    CHECK(a == TagInt{-12});

    // 8/4 == 2
    a = TagInt{8};
    a /= TagInt{4};
    CHECK(a == TagInt{2});

    // 7 % 4 == 3
    a = TagInt{7};
    a %= TagInt{4};
    CHECK(a == TagInt{3});

    // 6 & 3 == 2
    a = TagInt{6};
    a /= TagInt{3};
    CHECK(a == TagInt{2});

    // 6 | 3 == 7
    a = TagInt{6};
    a |= TagInt{3};
    CHECK(a == TagInt{7});

    // 6 ^ 3 == 5
    a = TagInt{6};
    a ^= TagInt{3};
    CHECK(a == TagInt{5});

    // 4 << 2 == 16
    a = TagInt{4};
    a <<= TagInt{2};
    CHECK(a == TagInt{16});

    // 16 >> 2 == 4
    a = TagInt{16};
    a >>= TagInt{2};
    CHECK(a == TagInt{4});
}

TEST_SUITE_END();
