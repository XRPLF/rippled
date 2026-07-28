#include <xrpl/beast/core/LexicalCast.h>

#include <xrpl/beast/xor_shift_engine.h>

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstddef>
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
constexpr T kMax = std::numeric_limits<T>::max();

template <class T>
constexpr T kMin = std::numeric_limits<T>::min();

// Comfortably inside the range, not boundary values.
constexpr auto kNearMax32 = kMax<uint32_t> - 5;
constexpr auto kNearMin32 = kMin<int32_t> + 4;
constexpr auto kUnderInt64Max = uint64_t{kMax<int64_t>} - 1;

// No wider integer type can hold these, so ToString cannot produce them.
constexpr std::string_view kAboveUint64Max = "18446744073709551616";
constexpr std::string_view kBelowInt64Min = "-9223372036854775809";

// The decimal text of a value, usable in a constant expression.
template <class T>
struct ToString
{
    std::array<char, 24> buffer{};
    std::size_t length{};

    constexpr explicit ToString(T value)
    {
        auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        length = static_cast<std::size_t>(result.ptr - buffer.data());
    }

    constexpr
    operator std::string_view() const
    {
        return {buffer.data(), length};
    }
};

// lexicalCastThrow deduces its input type, so the text has to be an explicit
// string_view rather than a ToString.
template <class T, class Value>
[[nodiscard]] T
castThrow(Value value)
{
    return lexicalCastThrow<T>(std::string_view{ToString{value}});
}

template <class T>
[[nodiscard]] bool
roundTrips(std::string_view text)
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
static_assert(std::is_same_v<int, int32_t>);
static_assert(std::is_same_v<unsigned int, uint32_t>);
static_assert(std::is_same_v<short, int16_t>);
static_assert(std::is_same_v<unsigned short, uint16_t>);

using IntegerTypes = ::testing::Types<  //
    int16_t,
    uint16_t,
    int32_t,
    uint32_t,
    int64_t,
    uint64_t>;

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
    for (int32_t i = kMin<int16_t>; i <= kMax<int16_t>; ++i)
    {
        auto const value = static_cast<int16_t>(i);

        // ASSERT, or a broken cast reports all 65536 iterations.
        auto const text = lexicalCast<std::string>(value);
        ASSERT_EQ(text, std::to_string(value));
        ASSERT_EQ(lexicalCast<int16_t>(text), value);
    }
}

TEST(LexicalCast, rejects_overflow)
{
    static_assert(not parses<uint32_t>(ToString{uint64_t{kMax<uint32_t>} + 5}));

    // Well past the maximum rather than just over it.
    static_assert(not parses<uint64_t>("99999999999999999999"));
    static_assert(not parses<uint16_t>("75821"));
}

TEST(LexicalCast, rejects_underflow)
{
    static_assert(not parses<uint32_t>("-1"));
    static_assert(not parses<int32_t>(ToString{-(int64_t{kMax<uint32_t>} + 5)}));

    static_assert(not parses<int64_t>("-99999999999999999999"));
    static_assert(not parses<int16_t>("-75821"));
}

TEST(LexicalCast, accepts_up_to_the_maximum)
{
    static_assert(parsed<uint16_t>(ToString{kMax<uint16_t> - 1}) == kMax < uint16_t > -1);
    static_assert(parsed<uint16_t>(ToString{kMax<uint16_t>}) == kMax<uint16_t>);
    static_assert(not parses<uint16_t>(ToString{uint32_t{kMax<uint16_t>} + 1}));

    static_assert(parsed<int16_t>(ToString{kMax<int16_t> - 1}) == kMax < int16_t > -1);
    static_assert(parsed<int16_t>(ToString{kMax<int16_t>}) == kMax<int16_t>);
    static_assert(not parses<int16_t>(ToString{int32_t{kMax<int16_t>} + 1}));

    static_assert(parsed<uint32_t>(ToString{kMax<uint32_t> - 1}) == kMax < uint32_t > -1);
    static_assert(parsed<uint32_t>(ToString{kMax<uint32_t>}) == kMax<uint32_t>);
    static_assert(not parses<uint32_t>(ToString{uint64_t{kMax<uint32_t>} + 1}));

    static_assert(parsed<int32_t>(ToString{kMax<int32_t> - 1}) == kMax < int32_t > -1);
    static_assert(parsed<int32_t>(ToString{kMax<int32_t>}) == kMax<int32_t>);
    static_assert(not parses<int32_t>(ToString{int64_t{kMax<int32_t>} + 1}));

    static_assert(parsed<int64_t>(ToString{kMax<int64_t> - 1}) == kMax < int64_t > -1);
    static_assert(parsed<int64_t>(ToString{kMax<int64_t>}) == kMax<int64_t>);
    static_assert(not parses<int64_t>(ToString{uint64_t{kMax<int64_t>} + 1}));

    static_assert(parsed<uint64_t>(ToString{kMax<uint64_t> - 1}) == kMax < uint64_t > -1);
    static_assert(parsed<uint64_t>(ToString{kMax<uint64_t>}) == kMax<uint64_t>);
    static_assert(not parses<uint64_t>(kAboveUint64Max));
}

TEST(LexicalCast, accepts_down_to_the_minimum)
{
    static_assert(parsed<int16_t>(ToString{kMin<int16_t> + 1}) == kMin<int16_t> + 1);
    static_assert(parsed<int16_t>(ToString{kMin<int16_t>}) == kMin<int16_t>);
    static_assert(not parses<int16_t>(ToString{int32_t{kMin<int16_t>} - 1}));

    static_assert(parsed<int32_t>(ToString{kMin<int32_t> + 1}) == kMin<int32_t> + 1);
    static_assert(parsed<int32_t>(ToString{kMin<int32_t>}) == kMin<int32_t>);
    static_assert(not parses<int32_t>(ToString{int64_t{kMin<int32_t>} - 1}));

    static_assert(parsed<int64_t>(ToString{kMin<int64_t> + 1}) == kMin<int64_t> + 1);
    static_assert(parsed<int64_t>(ToString{kMin<int64_t>}) == kMin<int64_t>);
    static_assert(not parses<int64_t>(kBelowInt64Min));
}

TEST(LexicalCast, limits_round_trip_through_to_string)
{
    EXPECT_TRUE(roundTrips<uint64_t>(ToString{kMax<uint64_t>}));
    EXPECT_TRUE(roundTrips<int64_t>(ToString{kMax<int64_t>}));
    EXPECT_TRUE(roundTrips<int64_t>(ToString{kMin<int64_t>}));
    EXPECT_TRUE(roundTrips<uint32_t>(ToString{kMax<uint32_t>}));
    EXPECT_TRUE(roundTrips<int32_t>(ToString{kMin<int32_t>}));
    EXPECT_TRUE(roundTrips<uint16_t>(ToString{kMax<uint16_t>}));
    EXPECT_TRUE(roundTrips<int16_t>(ToString{kMin<int16_t>}));
}

TEST(LexicalCast, accepts_signed_zero_in_every_form)
{
    static_assert(parsed<int32_t>("-0") == 0);
    static_assert(parsed<int32_t>("0") == 0);
    static_assert(parsed<int32_t>("+0") == 0);
}

TEST(LexicalCast, rejects_negative_zero_when_unsigned)
{
    static_assert(not parses<uint32_t>("-0"));
    static_assert(parsed<uint32_t>("0") == 0);
    static_assert(parsed<uint32_t>("+0") == 0);
}

TEST(LexicalCast, accepts_char_pointer_and_std_string_input)
{
    int32_t fromLiteral = 0;
    EXPECT_TRUE(lexicalCastChecked(fromLiteral, "+42"));
    EXPECT_EQ(fromLiteral, 42);

    int32_t fromString = 0;
    EXPECT_TRUE(lexicalCastChecked(fromString, std::string{"-42"}));
    EXPECT_EQ(fromString, -42);
}

TEST(LexicalCast, throwing_cast_returns_in_range_values)
{
    EXPECT_EQ(castThrow<uint64_t>(kUnderInt64Max), kUnderInt64Max);
    EXPECT_EQ(castThrow<uint32_t>(kNearMax32), kNearMax32);
    EXPECT_EQ(castThrow<int32_t>(kNearMin32), kNearMin32);
    EXPECT_EQ(lexicalCastThrow<int16_t>("-5711"), -5711);
}

TEST(LexicalCast, throwing_cast_throws_on_out_of_range)
{
    EXPECT_THROW(lexicalCastThrow<uint64_t>("99999999999999999999"), BadLexicalCast);

    // kNearMax32 with digits appended, so each is further past uint32_t's range.
    for (auto const scale : {10, 100, 1000})
    {
        auto const tooBig = ToString{uint64_t{kNearMax32} * scale};
        EXPECT_THROW(lexicalCastThrow<uint32_t>(std::string_view{tooBig}), BadLexicalCast);
    }

    EXPECT_THROW(lexicalCastThrow<int32_t>("5294967295"), BadLexicalCast);
    EXPECT_THROW(lexicalCastThrow<int16_t>("66666"), BadLexicalCast);
}

// Full-width digits, not ASCII ones.
TEST(LexicalCast, throwing_cast_throws_on_utf8_digits)
{
    EXPECT_THROW(lexicalCastThrow<int>("\xef\xbc\x91\xef\xbc\x90"), BadLexicalCast);
}

}  // namespace beast
