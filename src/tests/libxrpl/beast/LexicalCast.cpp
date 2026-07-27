#include <xrpl/beast/core/LexicalCast.h>

#include <xrpl/beast/xor_shift_engine.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace beast {
namespace {

template <class T>
[[nodiscard]] bool
parses(std::string const& text)
{
    T out{};
    return lexicalCastChecked(out, text);
}

template <class T>
[[nodiscard]] bool
roundTrips(std::string const& text)
{
    T out{};
    return lexicalCastChecked(out, text) && std::to_string(out) == text;
}

template <class T>
void
expectRoundTrip(T value)
{
    SCOPED_TRACE(::testing::Message() << "value: " << value);

    auto const text = lexicalCast<std::string>(value);
    EXPECT_EQ(text, std::to_string(value));

    auto decoded = static_cast<T>(~value);  // ensure decoded != value
    EXPECT_TRUE(lexicalCastChecked(decoded, text));
    EXPECT_EQ(decoded, value);
}

}  // namespace

// int/unsigned/short/unsigned short are covered by the list below — they are
// these exact types everywhere we build.
static_assert(std::is_same_v<int, std::int32_t>);
static_assert(std::is_same_v<unsigned int, std::uint32_t>);
static_assert(std::is_same_v<short, std::int16_t>);
static_assert(std::is_same_v<unsigned short, std::uint16_t>);

using IntegerTypes = ::testing::Types<  //
    std::int16_t,
    std::uint16_t,
    std::int32_t,
    std::uint32_t,
    std::int64_t,
    std::uint64_t>;

struct IntegerTypeNames
{
    template <class T>
    static std::string
    // NOLINTNEXTLINE(readability-identifier-naming) - required by gtest
    GetName(int)
    {
        return (std::is_signed_v<T> ? "int" : "uint") + std::to_string(sizeof(T) * 8);
    }
};

template <class T>
class LexicalCastIntegers : public ::testing::Test
{
};

TYPED_TEST_SUITE(LexicalCastIntegers, IntegerTypes, IntegerTypeNames);

TYPED_TEST(LexicalCastIntegers, round_trips_random_values)
{
    xor_shift_engine r{50};  // seeded per test so a failure reproduces on its own

    for (int i = 0; i < 1000; ++i)
        expectRoundTrip(static_cast<TypeParam>(r()));
}

TYPED_TEST(LexicalCastIntegers, round_trips_numeric_limits)
{
    expectRoundTrip(std::numeric_limits<TypeParam>::min());
    expectRoundTrip(std::numeric_limits<TypeParam>::max());
}

TEST(LexicalCast, round_trips_every_int16_value)
{
    for (std::int32_t i = std::numeric_limits<std::int16_t>::min();
         i <= std::numeric_limits<std::int16_t>::max();
         ++i)
    {
        auto const value = static_cast<std::int16_t>(i);

        // ASSERT, or a broken cast reports all 65536 iterations.
        auto const text = lexicalCast<std::string>(value);
        ASSERT_EQ(text, std::to_string(value));
        ASSERT_EQ(lexicalCast<std::int16_t>(text), value);
    }
}

TEST(LexicalCast, rejects_overflow)
{
    EXPECT_FALSE(parses<std::uint64_t>("99999999999999999999"));
    EXPECT_FALSE(parses<std::uint32_t>("4294967300"));
    EXPECT_FALSE(parses<std::uint16_t>("75821"));
}

TEST(LexicalCast, rejects_underflow)
{
    EXPECT_FALSE(parses<std::uint32_t>("-1"));

    EXPECT_FALSE(parses<std::int64_t>("-99999999999999999999"));
    EXPECT_FALSE(parses<std::int32_t>("-4294967300"));
    EXPECT_FALSE(parses<std::int16_t>("-75821"));
}

TEST(LexicalCast, accepts_up_to_the_maximum)
{
    EXPECT_TRUE(roundTrips<std::uint64_t>("18446744073709551614"));
    EXPECT_TRUE(roundTrips<std::uint64_t>("18446744073709551615"));
    EXPECT_FALSE(roundTrips<std::uint64_t>("18446744073709551616"));

    EXPECT_TRUE(roundTrips<std::int64_t>("9223372036854775806"));
    EXPECT_TRUE(roundTrips<std::int64_t>("9223372036854775807"));
    EXPECT_FALSE(roundTrips<std::int64_t>("9223372036854775808"));

    EXPECT_TRUE(roundTrips<std::uint32_t>("4294967294"));
    EXPECT_TRUE(roundTrips<std::uint32_t>("4294967295"));
    EXPECT_FALSE(roundTrips<std::uint32_t>("4294967296"));

    EXPECT_TRUE(roundTrips<std::int32_t>("2147483646"));
    EXPECT_TRUE(roundTrips<std::int32_t>("2147483647"));
    EXPECT_FALSE(roundTrips<std::int32_t>("2147483648"));

    EXPECT_TRUE(roundTrips<std::uint16_t>("65534"));
    EXPECT_TRUE(roundTrips<std::uint16_t>("65535"));
    EXPECT_FALSE(roundTrips<std::uint16_t>("65536"));

    EXPECT_TRUE(roundTrips<std::int16_t>("32766"));
    EXPECT_TRUE(roundTrips<std::int16_t>("32767"));
    EXPECT_FALSE(roundTrips<std::int16_t>("32768"));
}

TEST(LexicalCast, accepts_down_to_the_minimum)
{
    EXPECT_TRUE(roundTrips<std::int64_t>("-9223372036854775807"));
    EXPECT_TRUE(roundTrips<std::int64_t>("-9223372036854775808"));
    EXPECT_FALSE(roundTrips<std::int64_t>("-9223372036854775809"));

    EXPECT_TRUE(roundTrips<std::int32_t>("-2147483647"));
    EXPECT_TRUE(roundTrips<std::int32_t>("-2147483648"));
    EXPECT_FALSE(roundTrips<std::int32_t>("-2147483649"));

    EXPECT_TRUE(roundTrips<std::int16_t>("-32767"));
    EXPECT_TRUE(roundTrips<std::int16_t>("-32768"));
    EXPECT_FALSE(roundTrips<std::int16_t>("-32769"));
}

// These two use the `char const*` overload, not the `std::string` one `parses` hits.
TEST(LexicalCast, accepts_signed_zero_in_every_form)
{
    std::int32_t out = 0;

    EXPECT_TRUE(lexicalCastChecked(out, "-0"));
    EXPECT_TRUE(lexicalCastChecked(out, "0"));
    EXPECT_TRUE(lexicalCastChecked(out, "+0"));
}

TEST(LexicalCast, rejects_negative_zero_when_unsigned)
{
    std::uint32_t out = 0;

    EXPECT_FALSE(lexicalCastChecked(out, "-0"));
    EXPECT_TRUE(lexicalCastChecked(out, "0"));
    EXPECT_TRUE(lexicalCastChecked(out, "+0"));
}

TEST(LexicalCast, throwing_cast_returns_in_range_values)
{
    EXPECT_EQ(lexicalCastThrow<std::uint64_t>("9223372036854775806"), 9223372036854775806ULL);
    EXPECT_EQ(lexicalCastThrow<std::uint32_t>("4294967290"), 4294967290U);
    EXPECT_EQ(lexicalCastThrow<std::int32_t>("-2147483644"), -2147483644);
    EXPECT_EQ(lexicalCastThrow<std::int16_t>("-5711"), -5711);
}

TEST(LexicalCast, throwing_cast_throws_on_out_of_range)
{
    EXPECT_THROW(lexicalCastThrow<std::uint64_t>("99999999999999999999"), BadLexicalCast);

    EXPECT_THROW(lexicalCastThrow<std::uint32_t>("42949672900"), BadLexicalCast);
    EXPECT_THROW(lexicalCastThrow<std::uint32_t>("429496729000"), BadLexicalCast);
    EXPECT_THROW(lexicalCastThrow<std::uint32_t>("4294967290000"), BadLexicalCast);

    EXPECT_THROW(lexicalCastThrow<std::int32_t>("5294967295"), BadLexicalCast);
    EXPECT_THROW(lexicalCastThrow<std::int16_t>("66666"), BadLexicalCast);
}

// Full-width digits, not ASCII ones.
TEST(LexicalCast, throwing_cast_throws_on_utf8_digits)
{
    EXPECT_THROW(lexicalCastThrow<int>("\xef\xbc\x91\xef\xbc\x90"), BadLexicalCast);
}

}  // namespace beast
