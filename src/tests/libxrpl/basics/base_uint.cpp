#include <xrpl/basics/base_uint.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/endian/detail/order.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cassert>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::test {

// a non-hashing Hasher that just copies the bytes.
// Used to test hash_append in base_uint
template <std::size_t Bits>
struct Nonhash
{
    static constexpr auto const kEndian = boost::endian::order::big;
    static constexpr std::size_t kWidth = Bits / 8;

    std::array<std::uint8_t, kWidth> data;

    Nonhash() = default;

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        assert(len == kWidth);
        memcpy(data.data(), key, len);
    }

    explicit
    operator std::size_t() noexcept
    {
        return kWidth;
    }
};

struct BaseUintTest : public ::testing::Test
{
    using BaseUInt96 = BaseUInt<96>;
    static_assert(std::is_copy_constructible_v<BaseUInt96>);
    static_assert(std::is_copy_assignable_v<BaseUInt96>);
    static_assert(!std::is_constructible_v<BaseUInt96, std::complex<double>>);
    static_assert(!std::is_assignable_v<BaseUInt96&, std::complex<double>>);

    Blob const raw{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    BaseUInt96 const ascending{BaseUInt96::fromRaw(raw)};
    BaseUInt96 const zero{beast::kZero};

    static void
    testComparisons()
    {
        using HexPair = std::pair<std::string_view, std::string_view>;

        {
            static constexpr auto kTestArgs = std::to_array<HexPair>({
                {"0000000000000000", "0000000000000001"},
                {"0000000000000000", "ffffffffffffffff"},
                {"1234567812345678", "2345678923456789"},
                {"8000000000000000", "8000000000000001"},
                {"aaaaaaaaaaaaaaa9", "aaaaaaaaaaaaaaaa"},
                {"fffffffffffffffe", "ffffffffffffffff"},
            });

            for (auto const& [smallerText, largerText] : kTestArgs)
            {
                xrpl::BaseUInt<64> const smaller{smallerText}, larger{largerText};
                // For code readability, we want to use general boolean
                // expectations instead of specific EXPECT_LT etc.
                EXPECT_TRUE(smaller < larger);
                EXPECT_TRUE(smaller <= larger);
                EXPECT_TRUE(smaller != larger);
                EXPECT_FALSE(smaller == larger);
                EXPECT_FALSE(smaller > larger);
                EXPECT_FALSE(smaller >= larger);
                EXPECT_FALSE(larger < smaller);
                EXPECT_FALSE(larger <= smaller);
                EXPECT_TRUE(larger != smaller);
                EXPECT_FALSE(larger == smaller);
                EXPECT_TRUE(larger > smaller);
                EXPECT_TRUE(larger >= smaller);
                EXPECT_TRUE(smaller == smaller);
                EXPECT_TRUE(larger == larger);
            }
        }

        {
            static constexpr auto kTestArgs = std::to_array<HexPair>({
                {"000000000000000000000000", "000000000000000000000001"},
                {"000000000000000000000000", "ffffffffffffffffffffffff"},
                {"0123456789ab0123456789ab", "123456789abc123456789abc"},
                {"555555555555555555555555", "55555555555a555555555555"},
                {"aaaaaaaaaaaaaaa9aaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaa"},
                {"fffffffffffffffffffffffe", "ffffffffffffffffffffffff"},
            });

            for (auto const& [smallerText, largerText] : kTestArgs)
            {
                xrpl::BaseUInt<96> const smaller{smallerText}, larger{largerText};
                EXPECT_TRUE(smaller < larger);
                EXPECT_TRUE(smaller <= larger);
                EXPECT_TRUE(smaller != larger);
                EXPECT_FALSE(smaller == larger);
                EXPECT_FALSE(smaller > larger);
                EXPECT_FALSE(smaller >= larger);
                EXPECT_FALSE(larger < smaller);
                EXPECT_FALSE(larger <= smaller);
                EXPECT_TRUE(larger != smaller);
                EXPECT_FALSE(larger == smaller);
                EXPECT_TRUE(larger > smaller);
                EXPECT_TRUE(larger >= smaller);
                EXPECT_TRUE(smaller == smaller);
                EXPECT_TRUE(larger == larger);
            }
        }
    }
};

using BaseUintDeathTest = BaseUintTest;

TEST_F(BaseUintDeathTest, from_raw_size_mismatch)
{
    // ENABLE_VOIDSTAR is a debug build, but does not crash on failed asserts. Rather than twist
    // these tests into knots to make them work, just skip them.
#ifdef ENABLE_VOIDSTAR
    GTEST_SKIP() << "ENABLE_VOIDSTAR is a debug build, but does not crash on failed asserts.";
#else
    auto smallConstruct = [] {
        // Container smaller than the base_uint (8 bytes vs 12 bytes for
        // test96). Only the first 8 bytes are copied; the remaining 4 bytes
        // stay zero.
        Blob const tooSmall{1, 2, 3, 4, 5, 6, 7, 8};
        BaseUInt96 const result = BaseUInt96::fromRaw(tooSmall);
        auto const resultText = to_string(result);
        EXPECT_EQ(resultText, "010203040506070800000000") << resultText;
    };
    EXPECT_DEBUG_DEATH(smallConstruct(), "input size match");

    auto largeConstruct = [] {
        // Container larger than the base_uint (16 bytes vs 12 bytes for
        // test96). Only the first 12 bytes are copied; the extra bytes are
        // ignored.
        Blob const tooBig{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        BaseUInt96 const result = BaseUInt96::fromRaw(tooBig);
        auto const resultText = to_string(result);
        EXPECT_EQ(resultText, "0102030405060708090A0B0C") << resultText;
    };
    EXPECT_DEBUG_DEATH(largeConstruct(), "input size match");

    auto smallCopy = [] {
        // Container smaller than the base_uint (8 bytes vs 12 bytes for
        // test96). Only the first 8 bytes are copied; the remaining 4 bytes
        // stay zero.
        Blob const tooSmall{1, 2, 3, 4, 5, 6, 7, 8};
        BaseUInt96 result{};
        --result;
        {
            auto const originalText = to_string(result);
            EXPECT_EQ(originalText, "FFFFFFFFFFFFFFFFFFFFFFFF") << originalText;
        }
        result = tooSmall;
        auto const resultText = to_string(result);
        EXPECT_EQ(resultText, "010203040506070800000000") << resultText;
    };
    EXPECT_DEBUG_DEATH(smallCopy(), "input size match");

    auto const largeCopy = [] {
        // Container larger than the base_uint (16 bytes vs 12 bytes for
        // test96). Only the first 12 bytes are copied; the extra bytes are
        // ignored.
        Blob const tooBig{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        BaseUInt96 result{};
        --result;
        {
            auto const originalText = to_string(result);
            EXPECT_EQ(originalText, "FFFFFFFFFFFFFFFFFFFFFFFF") << originalText;
        }
        result = tooBig;
        auto const resultText = to_string(result);
        EXPECT_EQ(resultText, "0102030405060708090A0B0C") << resultText;
    };
    EXPECT_DEBUG_DEATH(largeCopy(), "input size match");
#endif
}

TEST_F(BaseUintTest, comparisons)
{
    testComparisons();
}

TEST_F(BaseUintTest, from_raw)
{
    EXPECT_EQ(BaseUInt96::kBytes, raw.size());
    EXPECT_EQ(raw.size(), ascending.size());
    EXPECT_EQ(to_string(ascending), "0102030405060708090A0B0C");
    EXPECT_EQ(toShortString(ascending), "01020304...");
    EXPECT_EQ(*ascending.data(), 1);
    EXPECT_EQ(ascending.signum(), 1);
    EXPECT_FALSE(!ascending);
    EXPECT_FALSE(ascending.isZero());
    EXPECT_TRUE(ascending.isNonZero());

    unsigned char expectedByte = 0;
    for (auto& byte : ascending)
        EXPECT_EQ(byte, ++expectedByte);
}

TEST_F(BaseUintTest, hash_append_writes_the_raw_bytes)
{
    // "Hash" with a no-op hasher and read the bytes it was handed back into
    // another base_uint, which must then equal the original.
    Nonhash<96> hasher{};
    hash_append(hasher, ascending);

    BaseUInt96 const rehashed =
        BaseUInt96::fromRaw(std::vector<std::uint8_t>(hasher.data.begin(), hasher.data.end()));
    EXPECT_EQ(rehashed, ascending);
}

TEST_F(BaseUintTest, complement)
{
    BaseUInt96 complement{~ascending};

    EXPECT_EQ(to_string(complement), "FEFDFCFBFAF9F8F7F6F5F4F3");
    EXPECT_EQ(toShortString(complement), "FEFDFCFB...");
    EXPECT_EQ(*complement.data(), 0xfe);
    EXPECT_EQ(complement.signum(), 1);
    EXPECT_FALSE(!complement);
    EXPECT_FALSE(complement.isZero());
    EXPECT_TRUE(complement.isNonZero());

    unsigned char expectedByte = 0xff;
    for (auto& byte : complement)
        EXPECT_EQ(byte, --expectedByte);

    EXPECT_LT(ascending, complement);
    EXPECT_GT(complement, ascending);

    complement = ascending;
    EXPECT_EQ(complement, ascending);
}

TEST_F(BaseUintTest, zero)
{
    EXPECT_EQ(to_string(zero), "000000000000000000000000");
    EXPECT_EQ(toShortString(zero), "00000000...");
    EXPECT_EQ(*zero.data(), 0);
    EXPECT_EQ(*zero.begin(), 0);
    EXPECT_EQ(*std::prev(zero.end(), 1), 0);
    EXPECT_EQ(zero.signum(), 0);
    EXPECT_TRUE(!zero);
    EXPECT_TRUE(zero.isZero());
    EXPECT_FALSE(zero.isNonZero());

    for (auto& byte : zero)
        EXPECT_EQ(byte, 0);
}

TEST_F(BaseUintTest, the_several_ways_of_spelling_zero_agree)
{
    BaseUInt96 const defaultZero;
    EXPECT_EQ(defaultZero, zero) << to_string(defaultZero);

    BaseUInt96 const bracedZero{};
    EXPECT_EQ(bracedZero, zero) << to_string(bracedZero);

    BaseUInt96 const zeroFromUInt{0u};
    EXPECT_EQ(zeroFromUInt, zero) << to_string(zeroFromUInt);
}

TEST_F(BaseUintTest, increment_and_decrement)
{
    BaseUInt96 counter{zero};

    counter++;
    EXPECT_EQ(counter, BaseUInt96(1));

    counter--;
    EXPECT_EQ(counter, beast::kZero);
    EXPECT_EQ(counter, zero);

    // Decrementing past zero wraps to all-ones.
    counter--;
    EXPECT_EQ(to_string(counter), "FFFFFFFFFFFFFFFFFFFFFFFF");
    EXPECT_EQ(toShortString(counter), "FFFFFFFF...");

    counter = beast::kZero;
    EXPECT_EQ(counter, zero);
}

TEST_F(BaseUintTest, exclusive_or)
{
    BaseUInt96 zeroPlusOne{zero};
    zeroPlusOne++;
    BaseUInt96 zeroMinusOne{zero};
    zeroMinusOne--;

    BaseUInt96 const xored{zeroMinusOne ^ zeroPlusOne};
    EXPECT_EQ(to_string(xored), "FFFFFFFFFFFFFFFFFFFFFFFE") << to_string(xored);
    EXPECT_EQ(toShortString(xored), "FFFFFFFF...") << toShortString(xored);
}

TEST_F(BaseUintTest, distinct_values_hash_into_a_set)
{
    // Requires hashing to work; four distinct values must stay four entries.
    std::unordered_set<BaseUInt96, HardenedHash<>> uset;

    BaseUInt96 zeroPlusOne{zero};
    zeroPlusOne++;
    BaseUInt96 zeroMinusOne{zero};
    zeroMinusOne--;

    uset.insert(ascending);
    uset.insert(BaseUInt96{~ascending});
    uset.insert(zero);
    uset.insert(BaseUInt96{zeroMinusOne ^ zeroPlusOne});

    EXPECT_EQ(uset.size(), 4);
}

TEST_F(BaseUintTest, parse_hex_round_trip)
{
    BaseUInt96 parsed;

    EXPECT_TRUE(parsed.parseHex(to_string(ascending)));
    EXPECT_EQ(parsed, ascending);
}

TEST_F(BaseUintTest, parse_hex_rejects_an_extra_leading_character)
{
    BaseUInt96 parsed{zero};

    EXPECT_FALSE(parsed.parseHex("A" + to_string(ascending)));
}

TEST_F(BaseUintTest, parse_hex_rejects_an_extra_trailing_character)
{
    BaseUInt96 parsed{zero};

    EXPECT_FALSE(parsed.parseHex(to_string(ascending) + "A"));
}

TEST_F(BaseUintTest, parse_hex_rejects_a_non_hex_character_at_any_position)
{
    BaseUInt96 parsed{zero};

    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string corrupted = to_string(zero);
        corrupted[i] = ('G' + (i % 10));
        EXPECT_FALSE(parsed.parseHex(corrupted));
    }
}

TEST_F(BaseUintTest, parse_hex_walking_ones)
{
    BaseUInt96 parsed;

    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string text = "000000000000000000000000";
        text[i] = '1';

        EXPECT_TRUE(parsed.parseHex(text));
        EXPECT_EQ(to_string(parsed), text);
    }
}

TEST_F(BaseUintTest, parse_hex_walking_zeroes)
{
    BaseUInt96 parsed;

    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string text = "111111111111111111111111";
        text[i] = '0';

        EXPECT_TRUE(parsed.parseHex(text));
        EXPECT_EQ(to_string(parsed), text);
    }
}

TEST_F(BaseUintTest, constexpr_construction)
{
    static_assert(BaseUInt96{}.signum() == 0);
    static_assert(BaseUInt96("0").signum() == 0);
    static_assert(BaseUInt96("000000000000000000000000").signum() == 0);
    static_assert(BaseUInt96("000000000000000000000001").signum() == 1);
    static_assert(BaseUInt96("800000000000000000000000").signum() == 1);
}

TEST_F(BaseUintTest, constexpr_constructor_throws_on_a_bad_length)
{
    // The vector keeps this out of a constant expression, so the constructor
    // throws instead of failing to compile.
    auto tooShort = [] {
        std::vector<char> const str(23, '7');
        std::string_view const sView(str.data(), str.size());
        [[maybe_unused]] BaseUInt96 const t96(sView);
    };

    EXPECT_THAT(
        tooShort, ::testing::ThrowsMessage<std::invalid_argument>("invalid length for hex string"));
}

TEST_F(BaseUintTest, constexpr_constructor_throws_on_a_bad_character)
{
    auto badCharacter = [] {
        std::vector<char> str(23, '7');
        str.push_back('G');
        std::string_view const sView(str.data(), str.size());
        [[maybe_unused]] BaseUInt96 const t96(sView);
    };

    EXPECT_THAT(badCharacter, ::testing::ThrowsMessage<std::range_error>("invalid hex character"));
}

TEST_F(BaseUintTest, constexpr_construction_agrees_with_parse_hex)
{
    struct StrBaseUInt
    {
        char const* const str;
        BaseUInt96 tst;

        constexpr StrBaseUInt(char const* s) : str(s), tst(s)
        {
        }
    };

    constexpr auto kTestCases = std::to_array<StrBaseUInt>({
        "000000000000000000000000",
        "000000000000000000000001",
        "fedcba9876543210ABCDEF91",
        "19FEDCBA0123456789abcdef",
        "800000000000000000000000",
        "fFfFfFfFfFfFfFfFfFfFfFfF",
    });

    for (StrBaseUInt const& testCase : kTestCases)
    {
        BaseUInt96 t96;
        EXPECT_TRUE(t96.parseHex(testCase.str));
        EXPECT_EQ(t96, testCase.tst);
    }
}

}  // namespace xrpl::test
