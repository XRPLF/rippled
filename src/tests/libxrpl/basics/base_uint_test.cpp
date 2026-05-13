#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
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
    static constexpr auto const kENDIAN = boost::endian::order::big;
    static constexpr std::size_t kWIDTH = Bits / 8;

    std::array<std::uint8_t, kWIDTH> data;

    Nonhash() = default;

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        assert(len == kWIDTH);
        memcpy(data.data(), key, len);
    }

    explicit
    operator std::size_t() noexcept
    {
        return kWIDTH;
    }
};

struct BaseUintTest : public ::testing::Test
{
    using test96 = BaseUInt<96>;
    static_assert(std::is_copy_constructible_v<test96>);
    static_assert(std::is_copy_assignable_v<test96>);

    static void
    testComparisons()
    {
        {
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
                kTEST_ARGS{
                    {{"0000000000000000", "0000000000000001"},
                     {"0000000000000000", "ffffffffffffffff"},
                     {"1234567812345678", "2345678923456789"},
                     {"8000000000000000", "8000000000000001"},
                     {"aaaaaaaaaaaaaaa9", "aaaaaaaaaaaaaaaa"},
                     {"fffffffffffffffe", "ffffffffffffffff"}}};

            for (auto const& arg : kTEST_ARGS)
            {
                xrpl::BaseUInt<64> const u{arg.first}, v{arg.second};
                EXPECT_TRUE(u < v);
                EXPECT_TRUE(u <= v);
                EXPECT_TRUE(u != v);
                EXPECT_TRUE(!(u == v));
                EXPECT_TRUE(!(u > v));
                EXPECT_TRUE(!(u >= v));
                EXPECT_TRUE(!(v < u));
                EXPECT_TRUE(!(v <= u));
                EXPECT_TRUE(v != u);
                EXPECT_TRUE(!(v == u));
                EXPECT_TRUE(v > u);
                EXPECT_TRUE(v >= u);
                EXPECT_TRUE(u == u);
                EXPECT_TRUE(v == v);
            }
        }

        {
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
                kTEST_ARGS{{
                    {"000000000000000000000000", "000000000000000000000001"},
                    {"000000000000000000000000", "ffffffffffffffffffffffff"},
                    {"0123456789ab0123456789ab", "123456789abc123456789abc"},
                    {"555555555555555555555555", "55555555555a555555555555"},
                    {"aaaaaaaaaaaaaaa9aaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaa"},
                    {"fffffffffffffffffffffffe", "ffffffffffffffffffffffff"},
                }};

            for (auto const& arg : kTEST_ARGS)
            {
                xrpl::BaseUInt<96> const u{arg.first}, v{arg.second};
                EXPECT_TRUE(u < v);
                EXPECT_TRUE(u <= v);
                EXPECT_TRUE(u != v);
                EXPECT_TRUE(!(u == v));
                EXPECT_TRUE(!(u > v));
                EXPECT_TRUE(!(u >= v));
                EXPECT_TRUE(!(v < u));
                EXPECT_TRUE(!(v <= u));
                EXPECT_TRUE(v != u);
                EXPECT_TRUE(!(v == u));
                EXPECT_TRUE(v > u);
                EXPECT_TRUE(v >= u);
                EXPECT_TRUE(u == u);
                EXPECT_TRUE(v == v);
            }
        }
    }

    static void
    run()
    {
        static_assert(!std::is_constructible_v<test96, std::complex<double>>);
        static_assert(!std::is_assignable_v<test96&, std::complex<double>>);

        testComparisons();

        // used to verify set insertion (hashing required)
        std::unordered_set<test96, HardenedHash<>> uset;

        Blob const raw{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        EXPECT_TRUE(test96::kBYTES == raw.size());

        test96 u = test96::fromRaw(raw);
        uset.insert(u);
        EXPECT_TRUE(raw.size() == u.size());
        EXPECT_TRUE(to_string(u) == "0102030405060708090A0B0C");
        EXPECT_TRUE(toShortString(u) == "01020304...");
        EXPECT_TRUE(*u.data() == 1);
        EXPECT_TRUE(u.signum() == 1);
        EXPECT_TRUE(!!u);
        EXPECT_TRUE(!u.isZero());
        EXPECT_TRUE(u.isNonZero());
        unsigned char t = 0;
        for (auto& d : u)
        {
            EXPECT_TRUE(d == ++t);
        }

        // Test hash_append by "hashing" with a no-op hasher (h)
        // and then extracting the bytes that were written during hashing
        // back into another base_uint (w) for comparison with the original
        Nonhash<96> h{};
        hash_append(h, u);
        test96 const w = test96::fromRaw(std::vector<std::uint8_t>(h.data.begin(), h.data.end()));
        EXPECT_TRUE(w == u);

        test96 v{~u};
        uset.insert(v);
        EXPECT_TRUE(to_string(v) == "FEFDFCFBFAF9F8F7F6F5F4F3");
        EXPECT_TRUE(toShortString(v) == "FEFDFCFB...");
        EXPECT_TRUE(*v.data() == 0xfe);
        EXPECT_TRUE(v.signum() == 1);
        EXPECT_TRUE(!!v);
        EXPECT_TRUE(!v.isZero());
        EXPECT_TRUE(v.isNonZero());
        t = 0xff;
        for (auto& d : v)
        {
            EXPECT_TRUE(d == --t);
        }

        EXPECT_TRUE(u < v);
        EXPECT_TRUE(v > u);

        v = u;
        EXPECT_TRUE(v == u);

        test96 z{beast::kZERO};
        uset.insert(z);
        EXPECT_TRUE(to_string(z) == "000000000000000000000000");
        EXPECT_TRUE(toShortString(z) == "00000000...");
        EXPECT_TRUE(*z.data() == 0);
        EXPECT_TRUE(*z.begin() == 0);
        EXPECT_TRUE(*std::prev(z.end(), 1) == 0);
        EXPECT_TRUE(z.signum() == 0);
        EXPECT_TRUE(!z);
        EXPECT_TRUE(z.isZero());
        EXPECT_TRUE(!z.isNonZero());
        for (auto& d : z)
        {
            EXPECT_TRUE(d == 0);
        }

        test96 n{z};
        n++;
        EXPECT_TRUE(n == test96(1));
        n--;
        EXPECT_TRUE(n == beast::kZERO);
        EXPECT_TRUE(n == z);
        n--;
        EXPECT_TRUE(to_string(n) == "FFFFFFFFFFFFFFFFFFFFFFFF");
        EXPECT_TRUE(toShortString(n) == "FFFFFFFF...");
        n = beast::kZERO;
        EXPECT_TRUE(n == z);

        test96 zp1{z};
        zp1++;
        test96 zm1{z};
        zm1--;
        test96 const x{zm1 ^ zp1};
        uset.insert(x);
        EXPECT_TRUE(to_string(x) == "FFFFFFFFFFFFFFFFFFFFFFFE") << to_string(x);
        EXPECT_TRUE(toShortString(x) == "FFFFFFFF...") << toShortString(x);

        EXPECT_TRUE(uset.size() == 4);

        test96 tmp;
        EXPECT_TRUE(tmp.parseHex(to_string(u)));
        EXPECT_TRUE(tmp == u);
        tmp = z;

        // fails with extra char
        EXPECT_TRUE(!tmp.parseHex("A" + to_string(u)));
        tmp = z;

        // fails with extra char at end
        EXPECT_TRUE(!tmp.parseHex(to_string(u) + "A"));

        // fails with a non-hex character at some point in the string:
        tmp = z;

        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string x = to_string(z);
            x[i] = ('G' + (i % 10));
            EXPECT_TRUE(!tmp.parseHex(x));
        }

        // Walking 1s:
        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string s1 = "000000000000000000000000";
            s1[i] = '1';

            EXPECT_TRUE(tmp.parseHex(s1));
            EXPECT_TRUE(to_string(tmp) == s1);
        }

        // Walking 0s:
        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string s1 = "111111111111111111111111";
            s1[i] = '0';

            EXPECT_TRUE(tmp.parseHex(s1));
            EXPECT_TRUE(to_string(tmp) == s1);
        }

        // Constexpr constructors
        {
            static_assert(test96{}.signum() == 0);
            static_assert(test96("0").signum() == 0);
            static_assert(test96("000000000000000000000000").signum() == 0);
            static_assert(test96("000000000000000000000001").signum() == 1);
            static_assert(test96("800000000000000000000000").signum() == 1);

// Everything within the #if should fail during compilation.
#if 0
            // Too few characters
            static_assert(test96("00000000000000000000000").signum() == 0);

            // Too many characters
            static_assert(test96("0000000000000000000000000").signum() == 0);

            // Non-hex characters
            static_assert(test96("00000000000000000000000 ").signum() == 1);
            static_assert(test96("00000000000000000000000/").signum() == 1);
            static_assert(test96("00000000000000000000000:").signum() == 1);
            static_assert(test96("00000000000000000000000@").signum() == 1);
            static_assert(test96("00000000000000000000000G").signum() == 1);
            static_assert(test96("00000000000000000000000`").signum() == 1);
            static_assert(test96("00000000000000000000000g").signum() == 1);
            static_assert(test96("00000000000000000000000~").signum() == 1);
#endif  // 0

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
                    [[maybe_unused]] test96 const t96(sView);
                }
                catch (std::invalid_argument const& e)
                {
                    EXPECT_TRUE(e.what() == std::string("invalid length for hex string"));
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
                    [[maybe_unused]] test96 const t96(sView);
                }
                catch (std::range_error const& e)
                {
                    EXPECT_TRUE(e.what() == std::string("invalid hex character"));
                    caught = true;
                }
                EXPECT_TRUE(caught);
            }

            // Verify that constexpr base_uints interpret a string the same
            // way parseHex() does.
            struct StrBaseUInt
            {
                char const* const str;
                test96 tst;

                constexpr StrBaseUInt(char const* s) : str(s), tst(s)
                {
                }
            };
            constexpr StrBaseUInt kTEST_CASES[] = {
                "000000000000000000000000",
                "000000000000000000000001",
                "fedcba9876543210ABCDEF91",
                "19FEDCBA0123456789abcdef",
                "800000000000000000000000",
                "fFfFfFfFfFfFfFfFfFfFfFfF"};

            for (StrBaseUInt const& t : kTEST_CASES)
            {
                test96 t96;
                EXPECT_TRUE(t96.parseHex(t.str));
                EXPECT_TRUE(t96 == t.tst);
            }
        }
    }
};

TEST_F(BaseUintTest, base_uint)
{
    run();
}

}  // namespace xrpl::test
