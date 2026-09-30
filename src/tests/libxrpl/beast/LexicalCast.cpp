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

template <class T>
constexpr T kUnderMax = kMax<T> - 1;

template <class T>
constexpr T kOverMin = kMin<T> + 1;

// Comfortably inside the range, not boundary values.
constexpr auto kNearMax32 = kMax<uint32_t> - 5;
constexpr auto kNearMin32 = kMin<int32_t> + 4;
constexpr auto kUnderInt64Max = uint64_t{kMax<int64_t>} - 1;
constexpr auto kInRangeInt16 = int16_t{-5711};

// No wider integer type can hold these, so ToString cannot produce them.
constexpr auto kAboveUint64Max = "18446744073709551616";
constexpr auto kBelowInt64Min = "-9223372036854775809";

// Out of range for every integer type we test.
constexpr auto kTwentyNines = "99999999999999999999";
constexpr auto kNegativeTwentyNines = "-99999999999999999999";

// Arbitrary values chosen to sit well outside a type's range, not just over it.
constexpr auto kAboveUint16Max = "75821";
constexpr auto kBelowInt16Min = "-75821";
constexpr auto kAboveInt32Max = "5294967295";
constexpr auto kAboveInt16Max = "66666";

constexpr auto kPositiveInt32 = int32_t{42};
constexpr auto kNegativeInt32 = int32_t{-42};

constexpr auto kPositiveInt32Text = "+42";
constexpr auto kNegativeInt32Text = "-42";

constexpr auto kNegativeOne = "-1";
constexpr auto kNegativeZero = "-0";
constexpr auto kBareZero = "0";
constexpr auto kPositiveZero = "+0";

// Full-width digits one and zero, not ASCII ones.
constexpr std::string_view kFullWidthDigits = "\xef\xbc\x91\xef\xbc\x90";

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

template <class T>
constexpr auto kMaxText = ToString{kMax<T>};

template <class T>
constexpr auto kUnderMaxText = ToString{kUnderMax<T>};

template <class T, class Wider>
constexpr auto kOverMaxText = ToString{Wider{kMax<T>} + 1};

template <class T>
constexpr auto kMinText = ToString{kMin<T>};

template <class T>
constexpr auto kOverMinText = ToString{kOverMin<T>};

template <class T, class Wider>
constexpr auto kUnderMinText = ToString{Wider{kMin<T>} - 1};

constexpr auto kOverUint32MaxText = ToString{uint64_t{kMax<uint32_t>} + 5};
constexpr auto kNegatedOverUint32MaxText = ToString{-(int64_t{kMax<uint32_t>} + 5)};

// lexicalCastThrow deduces its input type, so the text has to be an explicit
// string_view rather than a ToString.
template <class T, class Value>
[[nodiscard]] constexpr T
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
    static_assert(not parses<uint32_t>(kOverUint32MaxText));
    static_assert(not parses<uint64_t>(kTwentyNines));
    static_assert(not parses<uint16_t>(kAboveUint16Max));
}

TEST(LexicalCast, rejects_underflow)
{
    static_assert(not parses<uint32_t>(kNegativeOne));
    static_assert(not parses<int32_t>(kNegatedOverUint32MaxText));
    static_assert(not parses<int64_t>(kNegativeTwentyNines));
    static_assert(not parses<int16_t>(kBelowInt16Min));
}

TEST(LexicalCast, accepts_up_to_the_maximum)
{
    static_assert(parsed<uint16_t>(kUnderMaxText<uint16_t>) == kUnderMax<uint16_t>);
    static_assert(parsed<uint16_t>(kMaxText<uint16_t>) == kMax<uint16_t>);
    static_assert(not parses<uint16_t>(kOverMaxText<uint16_t, uint32_t>));

    static_assert(parsed<int16_t>(kUnderMaxText<int16_t>) == kUnderMax<int16_t>);
    static_assert(parsed<int16_t>(kMaxText<int16_t>) == kMax<int16_t>);
    static_assert(not parses<int16_t>(kOverMaxText<int16_t, int32_t>));

    static_assert(parsed<uint32_t>(kUnderMaxText<uint32_t>) == kUnderMax<uint32_t>);
    static_assert(parsed<uint32_t>(kMaxText<uint32_t>) == kMax<uint32_t>);
    static_assert(not parses<uint32_t>(kOverMaxText<uint32_t, uint64_t>));

    static_assert(parsed<int32_t>(kUnderMaxText<int32_t>) == kUnderMax<int32_t>);
    static_assert(parsed<int32_t>(kMaxText<int32_t>) == kMax<int32_t>);
    static_assert(not parses<int32_t>(kOverMaxText<int32_t, int64_t>));

    static_assert(parsed<int64_t>(kUnderMaxText<int64_t>) == kUnderMax<int64_t>);
    static_assert(parsed<int64_t>(kMaxText<int64_t>) == kMax<int64_t>);
    static_assert(not parses<int64_t>(kOverMaxText<int64_t, uint64_t>));

    static_assert(parsed<uint64_t>(kUnderMaxText<uint64_t>) == kUnderMax<uint64_t>);
    static_assert(parsed<uint64_t>(kMaxText<uint64_t>) == kMax<uint64_t>);
    static_assert(not parses<uint64_t>(kAboveUint64Max));
}

TEST(LexicalCast, accepts_down_to_the_minimum)
{
    static_assert(parsed<int16_t>(kOverMinText<int16_t>) == kOverMin<int16_t>);
    static_assert(parsed<int16_t>(kMinText<int16_t>) == kMin<int16_t>);
    static_assert(not parses<int16_t>(kUnderMinText<int16_t, int32_t>));

    static_assert(parsed<int32_t>(kOverMinText<int32_t>) == kOverMin<int32_t>);
    static_assert(parsed<int32_t>(kMinText<int32_t>) == kMin<int32_t>);
    static_assert(not parses<int32_t>(kUnderMinText<int32_t, int64_t>));

    static_assert(parsed<int64_t>(kOverMinText<int64_t>) == kOverMin<int64_t>);
    static_assert(parsed<int64_t>(kMinText<int64_t>) == kMin<int64_t>);
    static_assert(not parses<int64_t>(kBelowInt64Min));
}

TEST(LexicalCast, limits_round_trip_through_to_string)
{
    EXPECT_TRUE(roundTrips<uint64_t>(kMaxText<uint64_t>));
    EXPECT_TRUE(roundTrips<int64_t>(kMaxText<int64_t>));
    EXPECT_TRUE(roundTrips<int64_t>(kMinText<int64_t>));
    EXPECT_TRUE(roundTrips<uint32_t>(kMaxText<uint32_t>));
    EXPECT_TRUE(roundTrips<int32_t>(kMinText<int32_t>));
    EXPECT_TRUE(roundTrips<uint16_t>(kMaxText<uint16_t>));
    EXPECT_TRUE(roundTrips<int16_t>(kMinText<int16_t>));
}

TEST(LexicalCast, accepts_signed_zero_in_every_form)
{
    static_assert(parsed<int32_t>(kNegativeZero) == 0);
    static_assert(parsed<int32_t>(kBareZero) == 0);
    static_assert(parsed<int32_t>(kPositiveZero) == 0);
}

TEST(LexicalCast, rejects_negative_zero_when_unsigned)
{
    static_assert(not parses<uint32_t>(kNegativeZero));
    static_assert(parsed<uint32_t>(kBareZero) == 0);
    static_assert(parsed<uint32_t>(kPositiveZero) == 0);
}

TEST(LexicalCast, accepts_char_pointer_and_std_string_input)
{
    int32_t fromLiteral = 0;
    EXPECT_TRUE(lexicalCastChecked(fromLiteral, kPositiveInt32Text));
    EXPECT_EQ(fromLiteral, kPositiveInt32);

    int32_t fromString = 0;
    EXPECT_TRUE(lexicalCastChecked(fromString, std::string{kNegativeInt32Text}));
    EXPECT_EQ(fromString, kNegativeInt32);
}

TEST(LexicalCast, throwing_cast_returns_in_range_values)
{
    static_assert(castThrow<uint64_t>(kUnderInt64Max) == kUnderInt64Max);
    static_assert(castThrow<uint32_t>(kNearMax32) == kNearMax32);
    static_assert(castThrow<int32_t>(kNearMin32) == kNearMin32);
    static_assert(castThrow<int16_t>(kInRangeInt16) == kInRangeInt16);
}

TEST(LexicalCast, throwing_cast_throws_on_out_of_range)
{
    EXPECT_THROW(lexicalCastThrow<uint64_t>(kTwentyNines), BadLexicalCast);

    // kNearMax32 with digits appended, so each is further past uint32_t's range.
    for (auto const scale : {10, 100, 1000})
    {
        auto const tooBig = ToString{uint64_t{kNearMax32} * scale};
        EXPECT_THROW(lexicalCastThrow<uint32_t>(std::string_view{tooBig}), BadLexicalCast);
    }

    EXPECT_THROW(lexicalCastThrow<int32_t>(kAboveInt32Max), BadLexicalCast);
    EXPECT_THROW(lexicalCastThrow<int16_t>(kAboveInt16Max), BadLexicalCast);
}

// Full-width digits, not ASCII ones.
TEST(LexicalCast, throwing_cast_throws_on_utf8_digits)
{
    EXPECT_THROW(lexicalCastThrow<int>(kFullWidthDigits), BadLexicalCast);
}

}  // namespace beast
