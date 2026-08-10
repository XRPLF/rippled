#include <xrpl/basics/base_uint.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/endian/detail/order.hpp>

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

TEST_F(BaseUintDeathTest, fromRaw_size_mismatch)
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

TEST_F(BaseUintTest, base_uint)
{
    static_assert(!std::is_constructible_v<BaseUInt96, std::complex<double>>);
    static_assert(!std::is_assignable_v<BaseUInt96&, std::complex<double>>);

    testComparisons();

    // used to verify set insertion (hashing required)
    std::unordered_set<BaseUInt96, HardenedHash<>> uset;

    Blob const raw{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    EXPECT_EQ(BaseUInt96::kBytes, raw.size());

    BaseUInt96 u = BaseUInt96::fromRaw(raw);
    uset.insert(u);
    EXPECT_EQ(raw.size(), u.size());
    EXPECT_EQ(to_string(u), "0102030405060708090A0B0C");
    EXPECT_EQ(toShortString(u), "01020304...");
    EXPECT_EQ(*u.data(), 1);
    EXPECT_EQ(u.signum(), 1);
    EXPECT_FALSE(!u);
    EXPECT_FALSE(u.isZero());
    EXPECT_TRUE(u.isNonZero());
    unsigned char t = 0;
    for (auto& d : u)
    {
        EXPECT_EQ(d, ++t);
    }

    // Test hash_append by "hashing" with a no-op hasher (h)
    // and then extracting the bytes that were written during hashing
    // back into another base_uint (w) for comparison with the original
    Nonhash<96> h{};
    hash_append(h, u);
    BaseUInt96 const w =
        BaseUInt96::fromRaw(std::vector<std::uint8_t>(h.data.begin(), h.data.end()));
    EXPECT_EQ(w, u);

    BaseUInt96 v{~u};
    uset.insert(v);
    EXPECT_EQ(to_string(v), "FEFDFCFBFAF9F8F7F6F5F4F3");
    EXPECT_EQ(toShortString(v), "FEFDFCFB...");
    EXPECT_EQ(*v.data(), 0xfe);
    EXPECT_EQ(v.signum(), 1);
    EXPECT_FALSE(!v);
    EXPECT_FALSE(v.isZero());
    EXPECT_TRUE(v.isNonZero());

    t = 0xff;
    for (auto& d : v)
    {
        EXPECT_EQ(d, --t);
    }

    EXPECT_LT(u, v);
    EXPECT_GT(v, u);

    v = u;
    EXPECT_EQ(v, u);

    BaseUInt96 z{beast::kZero};
    uset.insert(z);
    EXPECT_EQ(to_string(z), "000000000000000000000000");
    EXPECT_EQ(toShortString(z), "00000000...");
    EXPECT_EQ(*z.data(), 0);
    EXPECT_EQ(*z.begin(), 0);
    EXPECT_EQ(*std::prev(z.end(), 1), 0);
    EXPECT_EQ(z.signum(), 0);
    EXPECT_TRUE(!z);
    EXPECT_TRUE(z.isZero());
    EXPECT_FALSE(z.isNonZero());
    for (auto& d : z)
    {
        EXPECT_EQ(d, 0);
    }

    {
        // There are several ways to create a zero. beast::kZero is tested above. Test some
        // others.
        BaseUInt96 const z1;
        EXPECT_EQ(z1, z) << to_string(z1);

        BaseUInt96 const z2{};
        EXPECT_EQ(z2, z) << to_string(z2);

        BaseUInt96 const z3{0u};
        EXPECT_EQ(z3, z) << to_string(z3);
    }

    BaseUInt96 n{z};
    n++;
    EXPECT_EQ(n, BaseUInt96(1));
    n--;
    EXPECT_EQ(n, beast::kZero);
    EXPECT_EQ(n, z);
    n--;
    EXPECT_EQ(to_string(n), "FFFFFFFFFFFFFFFFFFFFFFFF");
    EXPECT_EQ(toShortString(n), "FFFFFFFF...");
    n = beast::kZero;
    EXPECT_EQ(n, z);

    BaseUInt96 zp1{z};
    zp1++;
    BaseUInt96 zm1{z};
    zm1--;
    BaseUInt96 const x{zm1 ^ zp1};
    uset.insert(x);
    EXPECT_EQ(to_string(x), "FFFFFFFFFFFFFFFFFFFFFFFE") << to_string(x);
    EXPECT_EQ(toShortString(x), "FFFFFFFF...") << toShortString(x);

    EXPECT_EQ(uset.size(), 4);

    BaseUInt96 tmp;
    EXPECT_TRUE(tmp.parseHex(to_string(u)));
    EXPECT_EQ(tmp, u);
    tmp = z;

    // fails with extra char
    EXPECT_FALSE(tmp.parseHex("A" + to_string(u)));
    tmp = z;

    // fails with extra char at end
    EXPECT_FALSE(tmp.parseHex(to_string(u) + "A"));

    // fails with a non-hex character at some point in the string:
    tmp = z;

    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string x = to_string(z);
        x[i] = ('G' + (i % 10));
        EXPECT_FALSE(tmp.parseHex(x));
    }

    // Walking 1s:
    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string s1 = "000000000000000000000000";
        s1[i] = '1';

        EXPECT_TRUE(tmp.parseHex(s1));
        EXPECT_EQ(to_string(tmp), s1);
    }

    // Walking 0s:
    for (std::size_t i = 0; i != 24; ++i)
    {
        std::string s1 = "111111111111111111111111";
        s1[i] = '0';

        EXPECT_TRUE(tmp.parseHex(s1));
        EXPECT_EQ(to_string(tmp), s1);
    }

    // Constexpr constructors
    {
        static_assert(BaseUInt96{}.signum() == 0);
        static_assert(BaseUInt96("0").signum() == 0);
        static_assert(BaseUInt96("000000000000000000000000").signum() == 0);
        static_assert(BaseUInt96("000000000000000000000001").signum() == 1);
        static_assert(BaseUInt96("800000000000000000000000").signum() == 1);

        // Using the constexpr constructor in a non-constexpr context
        // with an error in the parsing throws an exception.
        {
            // Invalid length for string.
            bool caught = false;
            try
            {
                // Try to prevent constant evaluation.
                std::vector<char> str(23, '7');
                std::string_view const sView(str.data(), str.size());
                [[maybe_unused]] BaseUInt96 const t96(sView);
            }
            catch (std::invalid_argument const& e)
            {
                EXPECT_EQ(e.what(), std::string("invalid length for hex string"));
                caught = true;
            }
            EXPECT_TRUE(caught);
        }
        {
            // Invalid character in string.
            bool caught = false;
            try
            {
                // Try to prevent constant evaluation.
                std::vector<char> str(23, '7');
                str.push_back('G');
                std::string_view const sView(str.data(), str.size());
                [[maybe_unused]] BaseUInt96 const t96(sView);
            }
            catch (std::range_error const& e)
            {
                EXPECT_EQ(e.what(), std::string("invalid hex character"));
                caught = true;
            }
            EXPECT_TRUE(caught);
        }

        // Verify that constexpr base_uints interpret a string the same
        // way parseHex() does.
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

        for (StrBaseUInt const& t : kTestCases)
        {
            BaseUInt96 t96;
            EXPECT_TRUE(t96.parseHex(t.str));
            EXPECT_EQ(t96, t.tst);
        }
    }
}

}  // namespace xrpl::test
