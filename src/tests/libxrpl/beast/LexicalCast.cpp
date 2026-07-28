#include <xrpl/beast/core/LexicalCast.h>

#include <xrpl/beast/xor_shift_engine.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace beast {
namespace {

template <class T>
[[nodiscard]] constexpr bool
parses(std::string_view text)
{
    T out{};
    return lexicalCastChecked(out, text);
}

template <class T>
[[nodiscard]] constexpr T
parsed(std::string_view text)
{
    T out{};
    return lexicalCastChecked(out, text) ? out : T{};
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
        return (std::is_signed_v<T> ? "int" : "uint") + std::to_string(sizeof(T) * 8) + "_t";
    }
};

template <class T>
class LexicalCastIntegers : public ::testing::Test
{
};

TYPED_TEST_SUITE(LexicalCastIntegers, IntegerTypes, IntegerTypeNames);

TYPED_TEST(LexicalCastIntegers, round_trips_random_values)
{
    static constexpr auto kSampleCount = 1000uz;

    xor_shift_engine r{50};  // seeded per test so a failure reproduces on its own

    for (auto i = 0uz; i < kSampleCount; ++i)
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
    static_assert(not parses<std::uint64_t>("99999999999999999999"));
    static_assert(not parses<std::uint32_t>("4294967300"));
    static_assert(not parses<std::uint16_t>("75821"));
}

TEST(LexicalCast, rejects_underflow)
{
    static_assert(not parses<std::uint32_t>("-1"));

    static_assert(not parses<std::int64_t>("-99999999999999999999"));
    static_assert(not parses<std::int32_t>("-4294967300"));
    static_assert(not parses<std::int16_t>("-75821"));
}

TEST(LexicalCast, accepts_up_to_the_maximum)
{
    static_assert(parsed<std::uint64_t>("18446744073709551614") == 18446744073709551614ULL);
    static_assert(parsed<std::uint64_t>("18446744073709551615") == 18446744073709551615ULL);
    static_assert(not parses<std::uint64_t>("18446744073709551616"));

    static_assert(parsed<std::int64_t>("9223372036854775806") == 9223372036854775806LL);
    static_assert(parsed<std::int64_t>("9223372036854775807") == 9223372036854775807LL);
    static_assert(not parses<std::int64_t>("9223372036854775808"));

    static_assert(parsed<std::uint32_t>("4294967294") == 4294967294U);
    static_assert(parsed<std::uint32_t>("4294967295") == 4294967295U);
    static_assert(not parses<std::uint32_t>("4294967296"));

    static_assert(parsed<std::int32_t>("2147483646") == 2147483646);
    static_assert(parsed<std::int32_t>("2147483647") == 2147483647);
    static_assert(not parses<std::int32_t>("2147483648"));

    static_assert(parsed<std::uint16_t>("65534") == 65534);
    static_assert(parsed<std::uint16_t>("65535") == 65535);
    static_assert(not parses<std::uint16_t>("65536"));

    static_assert(parsed<std::int16_t>("32766") == 32766);
    static_assert(parsed<std::int16_t>("32767") == 32767);
    static_assert(not parses<std::int16_t>("32768"));
}

TEST(LexicalCast, accepts_down_to_the_minimum)
{
    static_assert(parsed<std::int64_t>("-9223372036854775807") == -9223372036854775807LL);
    static_assert(
        parsed<std::int64_t>("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    static_assert(not parses<std::int64_t>("-9223372036854775809"));

    static_assert(parsed<std::int32_t>("-2147483647") == -2147483647);
    static_assert(parsed<std::int32_t>("-2147483648") == std::numeric_limits<std::int32_t>::min());
    static_assert(not parses<std::int32_t>("-2147483649"));

    static_assert(parsed<std::int16_t>("-32767") == -32767);
    static_assert(parsed<std::int16_t>("-32768") == std::numeric_limits<std::int16_t>::min());
    static_assert(not parses<std::int16_t>("-32769"));
}

TEST(LexicalCast, limits_round_trip_through_to_string)
{
    EXPECT_TRUE(roundTrips<std::uint64_t>("18446744073709551615"));
    EXPECT_TRUE(roundTrips<std::int64_t>("9223372036854775807"));
    EXPECT_TRUE(roundTrips<std::int64_t>("-9223372036854775808"));
    EXPECT_TRUE(roundTrips<std::uint32_t>("4294967295"));
    EXPECT_TRUE(roundTrips<std::int32_t>("-2147483648"));
    EXPECT_TRUE(roundTrips<std::uint16_t>("65535"));
    EXPECT_TRUE(roundTrips<std::int16_t>("-32768"));
}

TEST(LexicalCast, accepts_signed_zero_in_every_form)
{
    static_assert(parsed<std::int32_t>("-0") == 0);
    static_assert(parsed<std::int32_t>("0") == 0);
    static_assert(parsed<std::int32_t>("+0") == 0);
}

TEST(LexicalCast, rejects_negative_zero_when_unsigned)
{
    static_assert(not parses<std::uint32_t>("-0"));
    static_assert(parsed<std::uint32_t>("0") == 0);
    static_assert(parsed<std::uint32_t>("+0") == 0);
}

TEST(LexicalCast, accepts_char_pointer_and_std_string_input)
{
    std::int32_t fromLiteral = 0;
    EXPECT_TRUE(lexicalCastChecked(fromLiteral, "+42"));
    EXPECT_EQ(fromLiteral, 42);

    std::int32_t fromString = 0;
    EXPECT_TRUE(lexicalCastChecked(fromString, std::string{"-42"}));
    EXPECT_EQ(fromString, -42);
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
