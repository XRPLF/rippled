#include <xrpl/basics/base_uint.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/endian/detail/order.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
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

private:
    using HexPair = std::pair<std::string_view, std::string_view>;

    template <std::size_t Bits>
    static void
    testComparisons(std::span<HexPair const> cases)
    {
        auto checkedLoad = [](xrpl::BaseUInt<Bits>& value, std::string_view str) {
            auto const success = value.parseHex(str);
            EXPECT_TRUE(success) << str;
            return success;
        };

        for (auto const& [smallerText, largerText] : cases)
        {
            xrpl::BaseUInt<Bits> smaller, larger;

            if (!checkedLoad(smaller, smallerText) || !checkedLoad(larger, largerText))
                continue;

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

public:
    static void
    testComparisons()
    {
        testComparisons<64>(std::to_array<HexPair>({
            {"0000000000000000", "0000000000000001"},
            {"0000000000000000", "ffffffffffffffff"},
            {"1234567812345678", "2345678923456789"},
            {"8000000000000000", "8000000000000001"},
            {"aaaaaaaaaaaaaaa9", "aaaaaaaaaaaaaaaa"},
            {"fffffffffffffffe", "ffffffffffffffff"},
        }));

        testComparisons<96>(std::to_array<HexPair>({
            {"000000000000000000000000", "000000000000000000000001"},
            {"000000000000000000000000", "ffffffffffffffffffffffff"},
            {"0123456789ab0123456789ab", "123456789abc123456789abc"},
            {"555555555555555555555555", "55555555555a555555555555"},
            {"aaaaaaaaaaaaaaa9aaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaa"},
            {"fffffffffffffffffffffffe", "ffffffffffffffffffffffff"},
        }));
    }
};

using BaseUintDeathTest = BaseUintTest;

TEST_F(BaseUintDeathTest, from_raw_size_mismatch)
{
    // High-bit bytes throughout so that a sign-extension mistake on a
    // char-typed source would be visible.
    static constexpr std::array<std::uint8_t, 12> kBytes = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B};

    for (std::size_t i = 0; i <= kBytes.size(); ++i)
    {
        auto const input = std::span{kBytes}.first(i);
        auto const val = BaseUInt96::fromRaw(input);

        EXPECT_EQ(val.has_value(), i == BaseUInt96::size()) << i;

        if (val)
        {
            EXPECT_TRUE(std::ranges::equal(*val, input));
            EXPECT_EQ(BaseUInt96::fromRaw(std::string{input.begin(), input.end()}), val);
        }
    }
}

TEST_F(BaseUintTest, base_uint)
{
    static_assert(!std::is_constructible_v<BaseUInt96, std::complex<double>>);
    static_assert(!std::is_assignable_v<BaseUInt96&, std::complex<double>>);

    testComparisons();

    // used to verify set insertion (hashing required)
    std::unordered_set<BaseUInt96, HardenedHash<>> uset;

    BaseUInt96 const ascending{
        std::to_array<std::uint8_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12})};

    uset.insert(ascending);
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

    // Test hash_append by "hashing" with a no-op hasher (hasher)
    // and then extracting the bytes that were written during hashing
    // back into another base_uint (rehashed) for comparison with the original
    Nonhash<96> hasher{};
    hash_append(hasher, ascending);

    // Exercises the fixed-extent constructor from a std::array.
    EXPECT_EQ(BaseUInt96{hasher.data}, ascending);

    BaseUInt96 complement{~ascending};
    uset.insert(complement);
    EXPECT_EQ(to_string(complement), "FEFDFCFBFAF9F8F7F6F5F4F3");
    EXPECT_EQ(toShortString(complement), "FEFDFCFB...");
    EXPECT_EQ(*complement.data(), 0xfe);
    EXPECT_EQ(complement.signum(), 1);
    EXPECT_FALSE(!complement);
    EXPECT_FALSE(complement.isZero());
    EXPECT_TRUE(complement.isNonZero());

    expectedByte = 0xff;
    for (auto& byte : complement)
        EXPECT_EQ(byte, --expectedByte);

    EXPECT_LT(ascending, complement);
    EXPECT_GT(complement, ascending);

    complement = ascending;
    EXPECT_EQ(complement, ascending);

    BaseUInt96 zero{beast::kZero};
    uset.insert(zero);
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

    {
        // There are several ways to create a zero. beast::kZero is tested above. Test some
        // others.
        BaseUInt96 const defaultZero;
        EXPECT_EQ(defaultZero, zero) << to_string(defaultZero);

        BaseUInt96 const bracedZero{};
        EXPECT_EQ(bracedZero, zero) << to_string(bracedZero);
    }

    BaseUInt96 counter{zero};
    counter++;
    EXPECT_EQ(counter, zero.next());
    counter--;
    EXPECT_EQ(counter, beast::kZero);
    EXPECT_EQ(counter, zero);
    counter--;
    EXPECT_EQ(to_string(counter), "FFFFFFFFFFFFFFFFFFFFFFFF");
    EXPECT_EQ(toShortString(counter), "FFFFFFFF...");
    counter = beast::kZero;
    EXPECT_EQ(counter, zero);

    BaseUInt96 zeroPlusOne{zero};
    zeroPlusOne++;
    BaseUInt96 zeroMinusOne{zero};
    zeroMinusOne--;
    BaseUInt96 const xored{zeroMinusOne ^ zeroPlusOne};
    uset.insert(xored);
    EXPECT_EQ(to_string(xored), "FFFFFFFFFFFFFFFFFFFFFFFE") << to_string(xored);
    EXPECT_EQ(toShortString(xored), "FFFFFFFF...") << toShortString(xored);

    EXPECT_EQ(uset.size(), 4);

    BaseUInt96 parsed;
    EXPECT_TRUE(parsed.parseHex(to_string(ascending)));
    EXPECT_EQ(parsed, ascending);
    parsed = zero;

    // fails with extra char
    EXPECT_FALSE(parsed.parseHex("A" + to_string(ascending)));
    parsed = zero;

    // fails with extra char at end
    EXPECT_FALSE(parsed.parseHex(to_string(ascending) + "A"));

    // fails with a non-hex character at some point in the string:
    parsed = zero;

    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string xored = to_string(zero);
        xored[i] = ('G' + (i % 10));
        EXPECT_FALSE(parsed.parseHex(xored));
    }

    // Walking 1s:
    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string s1 = "000000000000000000000000";
        s1[i] = '1';

        EXPECT_TRUE(parsed.parseHex(s1));
        EXPECT_EQ(to_string(parsed), s1);
    }

    // Walking 0s:
    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string s1 = "111111111111111111111111";
        s1[i] = '0';

        EXPECT_TRUE(parsed.parseHex(s1));
        EXPECT_EQ(to_string(parsed), s1);
    }

    // Compile-time construction
    static_assert(BaseUInt96{}.signum() == 0);
    static_assert(BaseUInt96{"0"}.signum() == 0);
    static_assert(BaseUInt96{"000000000000000000000000"}.signum() == 0);
    static_assert(BaseUInt96{"000000000000000000000001"}.signum() == 1);
    static_assert(BaseUInt96{"800000000000000000000000"}.signum() == 1);

    // The string_view constructor is consteval, so malformed input is a
    // compile-time error rather than an exception. The runtime parser
    // rejects the same inputs and leaves the object untouched.
    {
        std::string const tooShort(23, '7');
        BaseUInt96 t96;
        EXPECT_FALSE(t96.parseHex(tooShort));
        EXPECT_EQ(t96, BaseUInt96{});
    }
    {
        std::string badCharacter(23, '7');
        badCharacter.push_back('G');
        BaseUInt96 t96;
        EXPECT_FALSE(t96.parseHex(badCharacter));
        EXPECT_EQ(t96, BaseUInt96{});
    }

    {
        // Verify that consteval construction interprets a string the same
        // way parseHex() does.
        struct StrBaseUInt
        {
            std::string_view str;
            BaseUInt96 value;

            consteval StrBaseUInt(char const* s) : str(s), value(str)
            {
            }
        };

        constexpr StrBaseUInt kTestCases[] = {
            "000000000000000000000000",
            "000000000000000000000001",
            "fedcba9876543210ABCDEF91",
            "19FEDCBA0123456789abcdef",
            "800000000000000000000000",
            "fFfFfFfFfFfFfFfFfFfFfFfF",
        };

        for (StrBaseUInt const& testCase : kTestCases)
        {
            BaseUInt96 t96;
            EXPECT_TRUE(t96.parseHex(testCase.str)) << testCase.str;
            EXPECT_EQ(t96, testCase.value);
        }
    }
}

}  // namespace xrpl::test
