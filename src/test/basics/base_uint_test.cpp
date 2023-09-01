//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Blob.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/unit_test.h>
#include <boost/endian/conversion.hpp>
#include <complex>

#include <type_traits>

namespace ripple {
namespace test {

// a non-hashing Hasher that just copies the bytes.
// Used to test hash_append in base_uint
template <std::size_t Bits>
struct nonhash
{
    static constexpr auto const endian = boost::endian::order::big;
    static constexpr std::size_t WIDTH = Bits / 8;

    std::array<std::uint8_t, WIDTH> data_;

    nonhash() = default;

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        assert(len == WIDTH);
        memcpy(data_.data(), key, len);
    }

    explicit operator std::size_t() noexcept
    {
        return WIDTH;
    }
};

struct base_uint_test : beast::unit_test::suite
{
    using test96 = base_uint<96>;
    static_assert(std::is_copy_constructible<test96>::value);
    static_assert(std::is_copy_assignable<test96>::value);

    void
    testComparisons()
    {
        {
            static constexpr std::
                array<std::pair<std::string_view, std::string_view>, 6>
                    test_args{
                        {{"0000000000000000", "0000000000000001"},
                         {"0000000000000000", "ffffffffffffffff"},
                         {"1234567812345678", "2345678923456789"},
                         {"8000000000000000", "8000000000000001"},
                         {"aaaaaaaaaaaaaaa9", "aaaaaaaaaaaaaaaa"},
                         {"fffffffffffffffe", "ffffffffffffffff"}}};

            for (auto const& arg : test_args)
            {
                ripple::base_uint<64> const u{arg.first}, v{arg.second};
                BEAST_EXPECT(u < v);
                BEAST_EXPECT(u <= v);
                BEAST_EXPECT(u != v);
                BEAST_EXPECT(!(u == v));
                BEAST_EXPECT(!(u > v));
                BEAST_EXPECT(!(u >= v));
                BEAST_EXPECT(!(v < u));
                BEAST_EXPECT(!(v <= u));
                BEAST_EXPECT(v != u);
                BEAST_EXPECT(!(v == u));
                BEAST_EXPECT(v > u);
                BEAST_EXPECT(v >= u);
                BEAST_EXPECT(u == u);
                BEAST_EXPECT(v == v);
            }
        }

        {
            static constexpr std::array<
                std::pair<std::string_view, std::string_view>,
                6>
                test_args{{
                    {"000000000000000000000000", "000000000000000000000001"},
                    {"000000000000000000000000", "ffffffffffffffffffffffff"},
                    {"0123456789ab0123456789ab", "123456789abc123456789abc"},
                    {"555555555555555555555555", "55555555555a555555555555"},
                    {"aaaaaaaaaaaaaaa9aaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaa"},
                    {"fffffffffffffffffffffffe", "ffffffffffffffffffffffff"},
                }};

            for (auto const& arg : test_args)
            {
                ripple::base_uint<96> const u{arg.first}, v{arg.second};
                BEAST_EXPECT(u < v);
                BEAST_EXPECT(u <= v);
                BEAST_EXPECT(u != v);
                BEAST_EXPECT(!(u == v));
                BEAST_EXPECT(!(u > v));
                BEAST_EXPECT(!(u >= v));
                BEAST_EXPECT(!(v < u));
                BEAST_EXPECT(!(v <= u));
                BEAST_EXPECT(v != u);
                BEAST_EXPECT(!(v == u));
                BEAST_EXPECT(v > u);
                BEAST_EXPECT(v >= u);
                BEAST_EXPECT(u == u);
                BEAST_EXPECT(v == v);
            }
        }
    }

    void
    run() override
    {
        testcase("base_uint: general purpose tests");

        static_assert(
            !std::is_constructible<test96, std::complex<double>>::value);
        static_assert(
            !std::is_assignable<test96&, std::complex<double>>::value);

        testComparisons();

        // used to verify set insertion (hashing required)
        std::unordered_set<test96, hardened_hash<>> uset;

        Blob raw{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        BEAST_EXPECT(test96::bytes == raw.size());

        test96 u{raw};
        uset.insert(u);
        BEAST_EXPECT(raw.size() == u.size());
        BEAST_EXPECT(to_string(u) == "0102030405060708090A0B0C");
        BEAST_EXPECT(*u.data() == 1);
        BEAST_EXPECT(u.signum() == 1);
        BEAST_EXPECT(!!u);
        BEAST_EXPECT(!u.isZero());
        BEAST_EXPECT(u.isNonZero());
        unsigned char t = 0;
        for (auto& d : u)
        {
            BEAST_EXPECT(d == ++t);
        }

        // Test hash_append by "hashing" with a no-op hasher (h)
        // and then extracting the bytes that were written during hashing
        // back into another base_uint (w) for comparison with the original
        nonhash<96> h;
        hash_append(h, u);
        test96 w{std::vector<std::uint8_t>(h.data_.begin(), h.data_.end())};
        BEAST_EXPECT(w == u);

        test96 v{~u};
        uset.insert(v);
        BEAST_EXPECT(to_string(v) == "FEFDFCFBFAF9F8F7F6F5F4F3");
        BEAST_EXPECT(*v.data() == 0xfe);
        BEAST_EXPECT(v.signum() == 1);
        BEAST_EXPECT(!!v);
        BEAST_EXPECT(!v.isZero());
        BEAST_EXPECT(v.isNonZero());
        t = 0xff;
        for (auto& d : v)
        {
            BEAST_EXPECT(d == --t);
        }

        BEAST_EXPECT(u < v);
        BEAST_EXPECT(v > u);

        v = u;
        BEAST_EXPECT(v == u);

        test96 z{beast::zero};
        uset.insert(z);
        BEAST_EXPECT(to_string(z) == "000000000000000000000000");
        BEAST_EXPECT(*z.data() == 0);
        BEAST_EXPECT(*z.begin() == 0);
        BEAST_EXPECT(*std::prev(z.end(), 1) == 0);
        BEAST_EXPECT(z.signum() == 0);
        BEAST_EXPECT(!z);
        BEAST_EXPECT(z.isZero());
        BEAST_EXPECT(!z.isNonZero());
        for (auto& d : z)
        {
            BEAST_EXPECT(d == 0);
        }

        test96 n{z};
        n++;
        BEAST_EXPECT(n == test96(1));
        n--;
        BEAST_EXPECT(n == beast::zero);
        BEAST_EXPECT(n == z);
        n--;
        BEAST_EXPECT(to_string(n) == "FFFFFFFFFFFFFFFFFFFFFFFF");
        n = beast::zero;
        BEAST_EXPECT(n == z);

        test96 zp1{z};
        zp1++;
        test96 zm1{z};
        zm1--;
        test96 x{zm1 ^ zp1};
        uset.insert(x);
        BEAST_EXPECTS(to_string(x) == "FFFFFFFFFFFFFFFFFFFFFFFE", to_string(x));

        BEAST_EXPECT(uset.size() == 4);

        test96 tmp;
        BEAST_EXPECT(tmp.parseHex(to_string(u)));
        BEAST_EXPECT(tmp == u);
        tmp = z;

        // fails with extra char
        BEAST_EXPECT(!tmp.parseHex("A" + to_string(u)));
        tmp = z;

        // fails with extra char at end
        BEAST_EXPECT(!tmp.parseHex(to_string(u) + "A"));

        // fails with a non-hex character at some point in the string:
        tmp = z;

        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string x = to_string(z);
            x[i] = ('G' + (i % 10));
            BEAST_EXPECT(!tmp.parseHex(x));
        }

        // Walking 1s:
        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string s1 = "000000000000000000000000";
            s1[i] = '1';

            BEAST_EXPECT(tmp.parseHex(s1));
            BEAST_EXPECT(to_string(tmp) == s1);
        }

        // Walking 0s:
        for (std::size_t i = 0; i != 24; ++i)
        {
            std::string s1 = "111111111111111111111111";
            s1[i] = '0';

            BEAST_EXPECT(tmp.parseHex(s1));
            BEAST_EXPECT(to_string(tmp) == s1);
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
                    std::string_view sView(str.data(), str.size());
                    [[maybe_unused]] test96 t96(sView);
                }
                catch (std::invalid_argument const& e)
                {
                    BEAST_EXPECT(
                        e.what() ==
                        std::string("invalid length for hex string"));
                    caught = true;
                }
                BEAST_EXPECT(caught);
            }
            {
                // Invalid character in string.
                bool caught = false;
                try
                {
                    // Try to prevent constant evaluation.
                    std::vector<char> str(23, '7');
                    str.push_back('G');
                    std::string_view sView(str.data(), str.size());
                    [[maybe_unused]] test96 t96(sView);
                }
                catch (std::range_error const& e)
                {
                    BEAST_EXPECT(
                        e.what() == std::string("invalid hex character"));
                    caught = true;
                }
                BEAST_EXPECT(caught);
            }

            // Verify that constexpr base_uints interpret a string the same
            // way parseHex() does.
            struct StrBaseUint
            {
                char const* const str;
                test96 tst;

                constexpr StrBaseUint(char const* s) : str(s), tst(s)
                {
                }
            };
            constexpr StrBaseUint testCases[] = {
                "000000000000000000000000",
                "000000000000000000000001",
                "fedcba9876543210ABCDEF91",
                "19FEDCBA0123456789abcdef",
                "800000000000000000000000",
                "fFfFfFfFfFfFfFfFfFfFfFfF"};

            for (StrBaseUint const& t : testCases)
            {
                test96 t96;
                BEAST_EXPECT(t96.parseHex(t.str));
                BEAST_EXPECT(t96 == t.tst);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(base_uint, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
