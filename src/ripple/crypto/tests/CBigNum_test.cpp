//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/crypto/CBigNum.h>
#include <ripple/basics/base_uint.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class CBigNum_test : public beast::unit_test::suite
{
private:
    // Generic test for constructing CBigNum from native types.
    template<typename T>
    void
    testNativeCtor()
    {
        static_assert (std::numeric_limits<T>::is_integer, "Requires int type");
        if (std::numeric_limits<T>::is_signed)
        {
            CBigNum const neg (static_cast<T>(std::numeric_limits<T>::min()));
            this->expect (neg == std::numeric_limits<T>::min());
            this->expect (neg < 0);
        }
        else
        {
            CBigNum const naught (static_cast<T>(0));
            this->expect (naught == 0);
        }
        CBigNum const big (static_cast<T>(std::numeric_limits<T>::max()));
        this->expect (big == std::numeric_limits<T>::max());
        this->expect (big > 0);
    }

    // Return pointer to start of array.
    template<typename T, std::size_t N>
    static auto
    array_cbegin(T (&a)[N]) -> T const*
    {
        return &a[0];
    }

    // Return pointer to one past end of array.
    template<typename T, std::size_t N>
    static auto
    array_cend(T (&a)[N]) -> T const*
    {
        return &a[N];
    }

public:
    void
    run ()
    {
        {
            // Default constructor.
            CBigNum const big0;
            expect (big0 == 0);
            // Construct from unsigned char.
            CBigNum const big1 (static_cast<unsigned char>(1));
            expect (big1 == 1);
            // Assignment.
            CBigNum bigA;
            bigA = big1;
            expect (bigA == big1);
        }
        {
            // Test constructing from native types.
            testNativeCtor<char>();
            testNativeCtor<unsigned char>();
            testNativeCtor<short>();
            testNativeCtor<unsigned short>();
            testNativeCtor<int>();
            testNativeCtor<unsigned int>();
            testNativeCtor<long>();
//          testNativeCtor<unsigned long>(); // unsigned long not supported
            testNativeCtor<long long>();
            testNativeCtor<unsigned long long>();
        }
        {
            // Construction from uint256.
            uint256 const naught256 (0);
            uint256 big256 = naught256;
            --big256;
            CBigNum naught (naught256);
            expect (naught == 0);
            CBigNum big (big256);
            expect (big.GetHex() == "ffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        }
        {
            // Construction from Blob.
            Blob const naughtBlob (67, 0);
            Blob const bigBlob (40, 255);
            CBigNum const naught (naughtBlob);
            expect (naught == 0);
            CBigNum big (bigBlob);
            expect (big.GetHex() == "-7fffffffffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        }
        {
            // Construction from BIGNUM*.
            BIGNUM b;
            BN_init (&b);
            // Scope guard to free b when we leave scope.
            std::shared_ptr<void> guard (nullptr, [&b] (void*)
            {
                BN_clear_free (&b);
            });
            expect (BN_set_word (&b, 0x123456789ABCDF0ull) == 1);
            CBigNum big (&b);
            --big;
            expect (BN_cmp (&big, &b) == -1);
            ++big;
            expect (BN_cmp (&big, &b) == 0);
            ++big;
            expect (BN_cmp (&big, &b) == 1);
        }
        {
            // Construction from unsigned char*.
            static unsigned char const a[] {0x0,0xF,
                0xE,0xD,0xC,0xB,0xA,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0};
            CBigNum big (array_cbegin(a), array_cend(a));
            expect (big.GetHex() == "102030405060708090a0b0c0d0e0f00");
        }
        {
            // setuint() and getuint().
            CBigNum big;
            expect (big.getuint() == 0);
            --big;
            // Note that getuint() does not throw an exception on underflow.
            expect (big.getuint() == 1);

            unsigned int const biggest_uint =
                std::numeric_limits<unsigned int>::max();
            big.setuint (biggest_uint);
            expect (big.getuint() == biggest_uint);
            // Note that getuint() does not throw an exception on overflow.
            ++big;
            expect (big.getuint() == 0);
        }
        {
            // getint().
            CBigNum neg (std::numeric_limits<int>::min());
            expect (neg.getint() == std::numeric_limits<int>::min());
            // Note that getint() limits at INT_MIN on negative overflow.
            --neg;
            expect (neg.getint() == std::numeric_limits<int>::min());

            CBigNum pos (std::numeric_limits<int>::max());
            expect (pos.getint() == std::numeric_limits<int>::max());
            // Note that getint() limits at INT_MAX on positive overflow.
            ++pos;
            expect (pos.getint() == std::numeric_limits<int>::max());
        }
        {
            // setint64().
            CBigNum big;
            big.setint64 (std::numeric_limits<std::int64_t>::min());
            expect (big.GetHex() == "-8000000000000000");
            --big;
            expect (big.GetHex() == "-8000000000000001");

            big.setint64 (std::numeric_limits<std::int64_t>::max());
            expect (big.GetHex() == "7fffffffffffffff");
            ++big;
            expect (big.GetHex() == "8000000000000000");
        }
        {
            // setuint64() and getuint64().
            CBigNum big;
            big.setuint64 (static_cast<std::uint64_t>(0));
            expect (big.getuint64() == 0);
            // Note that getuint64() drops the sign.
            --big;
            expect (big.getuint64() == 1);

            big.setuint64 (std::numeric_limits<std::uint64_t>::max());
            expect (
                big.getuint64() == std::numeric_limits<std::uint64_t>::max());
            // Note that one some platforms getuint() throws on positive
            // overflow and on other platforms it does not.  Hence the macro.
            ++big;
#if (ULONG_MAX > UINT_MAX)
            expect (
                big.getuint64() == std::numeric_limits<std::uint64_t>::max());
#else
            bool overflowException = false;
            try
            {
                big.getuint64();
            }
            catch (std::runtime_error const&)
            {
                overflowException = true;
            }
            expect (overflowException);
#endif
        }
        {
            // setuint256() and getuint256().
            uint256 const naught256 (Blob (32, 0));
            uint256 const max256 (Blob (32, 0xFF));

            CBigNum big;
            big.setuint256(max256);
            expect (big.GetHex() == "ffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffff");
            expect (big.getuint256() == max256);

            // Note that getuint256() returns zero on overflow.
            ++big;
            expect (big.GetHex() == "1000000000000000"
                "0000000000000000000000000000000000000000000000000");
            expect (big.getuint256() == naught256);

            --big;
            expect (big.getuint256() == max256);

            big.setuint256(naught256);
            expect (big == 0);
            expect (big.getuint256() == naught256);
            --big;
            expect (big == -1);
            ++big;
            expect (big.getuint256() == naught256);
        }
        {
            // setvch() and getvch().
            CBigNum big;
            expect (big.getvch().size() == 0);

            // Small values.
            static unsigned char const one[] {1,0};
            big.setvch(array_cbegin(one), array_cend(one));
            expect (big == 1);
            --big;
            expect (big.getvch().size() == 0);
            --big;
            Blob smallBlob = big.getvch();
            expect (smallBlob.size() == 1);
            expect (smallBlob[0] ==0x81);
            smallBlob[0] = 0xff;
            expect (big == -1);
            big.setvch(smallBlob);
            expect (big == -127);
            expect (big.getvch().size() == 1);

            // Big values
            static unsigned char const large[80] {
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f,
            };
            static unsigned char const larger[81] {
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00
            };

            big.setvch (array_cbegin(large), array_cend(large));
            CBigNum bigger;
            bigger.setvch(array_cbegin(larger), array_cend(larger));
            expect (big > 0);
            expect (big < bigger);

            Blob bigBlob = big.getvch();
            expect (bigBlob.size() == 80);
            expect (bigBlob.back() == 0x7f);

            ++big;
            expect (big == bigger);
            bigBlob = big.getvch();
            expect (bigBlob.size() == 81);
            expect (std::equal(array_cbegin(larger), array_cend(larger),
                               bigBlob.begin(), bigBlob.end()));

            bigBlob[0] = 1;
            bigger.setvch(bigBlob);
            expect (big < bigger);
            ++big;
            expect (big == bigger);
        }
        {
            // GetCompact() and SetCompact().
            CBigNum big (0);
            expect (big.GetCompact() == 0);
            big.SetCompact (0x1010000);
            expect (big == 1);
            big.SetCompact (0x1810000);
            expect (big == -1);

            // Positive values.
            big.SetCompact (0x2010000);
            expect (big.GetCompact() == 0x2010000);
            ++big;
            expect (big.GetCompact() == 0x2010100);

            big.SetCompact (0x3010000);
            expect (big.GetCompact() == 0x3010000);
            ++big;
            expect (big.GetCompact() == 0x3010001);

            {
                big.SetCompact (0x4010000);
                expect (big.getuint64() == 0x1000000);
                unsigned int const compact = big.GetCompact();
                ++big;
                expect (compact == big.GetCompact());
            }
            big.SetCompact (0xFF7FFFFF);
            --big;
            expect (big.GetCompact() == 0xFF7FFFFE);
            expect (big.GetHex() ==
                "7ffffefffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffff");

            // Negative values.
            big.SetCompact (0x2810000);
            expect (big.GetCompact() == 0x2810000);
            --big;
            expect (big.GetCompact() == 0x2810100);

            big.SetCompact (0x3810000);
            expect (big.GetCompact() == 0x3810000);
            --big;
            expect (big.GetCompact() == 0x3810001);

            {
                big.SetCompact (0x4810000);
                expect (big.getint() == -16777216);
                unsigned int const compact = big.GetCompact();
                --big;
                expect (compact == big.GetCompact());
            }
            big.SetCompact (0xFFFFFFFF);
            ++big;
            expect (big.GetCompact() == 0xFFFFFFFE);
            expect (big.GetHex() ==
                "-7ffffefffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffffffffffffffffffffff"
                "fffffffffffffff");
        }
        {
            // SetHex() and GetHex().
            CBigNum big (1);
            expect (big != 0);
            big.SetHex ("   ");
            expect (big == 0);
            big.SetHex ("   -0x  1");
            expect (big == -1);
            big.SetHex ("  -0");
            expect (big.GetHex() == "0");
            big.SetHex ("Feeble");
            expect (big.GetHex () == "feeb");
        }
        {
            // ToString().
            CBigNum big;
            expect (big.ToString (0) == "0");

            // Trying to use base 0 throws an exception.
            ++big;
            {
                bool badDivisor = false;
                try
                {
                    big.ToString (0);
                }
                catch (std::runtime_error const&)
                {
                    badDivisor = true;
                }
                expect (badDivisor);
            }
            ++big;
            expect (big.ToString (2) == "10");
            expect (big.ToString (10) == "2");
        }
        {
            // Member math operators.
            CBigNum big;
            expect (!big);
            --big;
            expect (! (!big));
            big += 2;
            expect (big == 1);
            big -= 3;
            expect (big == -2);
            big *= -1;
            expect (big == 2);
            big /= 2;
            expect (big == 1);
            big = 8;
            big %= 5;
            expect (big == 3);
            ++big;
            expect (big == 4);
            --big;
            expect (big == 3);
            {
                CBigNum const preIncr = big++;
                expect (preIncr == 3);
                expect (big == 4);
            }
            {
                CBigNum const preDecr = big--;
                expect (preDecr == 4);
                expect (big == 3);
            }
            big.setuint64 (0x80);
            big >>= 1;
            expect (big == 0x40);
            big <<= 2;
            expect (big == 0x100);
            big >>= 9;
            expect (big == 0);
        }
        {
            // Non-member math operators.
            CBigNum a (5);
            CBigNum b (3);
            CBigNum c = a + b;
            expect (c == 8);
            c = c * a;
            expect (c == 40);
            c = c - b;
            expect (c == 37);
            a = c / b;
            expect (a == 12);
            a = c % b;
            expect (a == 1);
            a = -c;
            expect (a == -37);
            b = a << 1;
            expect (b == -74);
            a = c >> 2;
            expect (a == 9);
            // All right shifts of negative numbers yield zero.
            a = (b >> 1);
            expect (a == 0);
        }
        {
            // Test non-member comparison operators.
            auto comparison_test = [this] (int center)
            {
                CBigNum delta (1);
                CBigNum lo (center);
                lo -= delta;
                CBigNum const ref (center);
                CBigNum const mid (center);
                CBigNum hi (center);
                hi += delta;

                this->expect ( (lo  <  ref));
                this->expect (!(mid <  ref));
                this->expect (!(hi  <  ref));

                this->expect ( (lo  <= ref));
                this->expect ( (mid <= ref));
                this->expect (!(hi  <= ref));

                this->expect (!(lo  >  ref));
                this->expect (!(mid >  ref));
                this->expect ( (hi  >  ref));

                this->expect (!(lo  >= ref));
                this->expect ( (mid >= ref));
                this->expect ( (hi  >= ref));

                this->expect (!(lo  == ref));
                this->expect ( (mid == ref));
                this->expect (!(hi  == ref));

                this->expect ( (lo  != ref));
                this->expect (!(mid != ref));
                this->expect ( (hi  != ref));
            };

            comparison_test (537);
            comparison_test (0);
            comparison_test (-2058);
        }
        {
            // BN math functions defined in CBigNum.h.
            CBigNum a;
            expect (BN_is_zero (&a));
            expect (BN_add_word64 (&a, 0xF000000000000000ull) == 1);
            expect (BN_add_word64 (&a, 0x0FFFFFFFFFFFFFFFull) == 1);

            CBigNum b;
            expect (BN_set_word (&b, 0xFFFFFFFFFFFFFFFFull) == 1);
            expect (BN_cmp (&a, &b) == 0);

            expect (BN_sub_word64 (&a, 0xFF00000000000000ull) == 1);
            expect (BN_set_word (&b, 0x00FFFFFFFFFFFFFFull) == 1);
            expect (BN_cmp (&a, &b) == 0);

            expect (BN_mul_word64 (&a, 0x10) == 1);
            expect (BN_set_word (&b, 0x0FFFFFFFFFFFFFF0ull) == 1);
            expect (BN_cmp (&a, &b) == 0);

            expect (BN_div_word64 (&a, 0x10) == 1);
            expect (BN_set_word (&b, 0x00FFFFFFFFFFFFFFull) == 1);
            expect (BN_cmp (&a, &b) == 0);

            expect (BN_div_word64 (&a, 0x200) == 1);
            expect (BN_set_word (&b, 0x00007FFFFFFFFFFFull) == 1);
            expect (BN_cmp (&a, &b) == 0);

            a *= -1;
            expect (a < 0);
            expect (BN_div_word64 (&a, 0x400) == 1);
            expect (BN_set_word (&b, 0x0000001FFFFFFFFFull) == 1);
            b *= -1;
            expect (a == b);

            // Divide by 0 should return an error.
            expect (BN_div_word64 (&a, 0) != 1);
        }
    }
};

BEAST_DEFINE_TESTSUITE(CBigNum,ripple_data,ripple);

} // ripple
