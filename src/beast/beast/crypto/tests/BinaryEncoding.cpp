//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

// MODULES: ../../../modules/beast_core/beast_core.beast_core.unity.cpp ../../strings/Strings.cpp ../../chrono/Chrono.cpp ../../threads/Threads.cpp
#include <beast/crypto/BinaryEncoding.h>
#include <beast/crypto/UnsignedInteger.h>

#include <beast/unit_test/suite.h>
#include <beast/module/core/maths/Random.h>

#include <cstddef>
#include <string>

namespace beast {

/** Generic algorithms for base encoding and decoding. */
class BinaryEncoding
{
public:
    /** Concept: Conversion

        X denotes a Conversion class, a is a value of type X,
        i is an integral type.

        Requirements:

            Expression      Type            Notes/Contracts
            -------------   -----------     ------------------
            X a;
            X::radix        size_type       constexpr
            a.map (i)       char            maps base numeral to a char
    */

    /** Encode the unsigned integer into a string using the specified conversion. */
    template <typename Conversion, std::size_t Bytes>
    static std::string encode (UnsignedInteger <Bytes> v, Conversion c = Conversion ())
    {
        // bi is destroyed in this process
        typename UnsignedInteger <Bytes>::CalcType bi (v.toCalcType ());
        std::size_t const radix (Conversion::radix);
        std::string s;
        s.reserve (bi.size() * 3); // guess
        while (bi.isNotZero ())
        {
            std::size_t const m (bi % radix);
            bi /= radix;
            s.push_back (c.map (m));
        }
        std::reverse (s.begin(), s.end());
        return s;
    }

    /** Decode the string into an unsigned integer.
        The size must match exactly
        @return `true` on success.
    */
    template <typename Conversion, std::size_t Bytes>
    static bool decode (UnsignedInteger <Bytes>& rv,
        std::string const& s, Conversion c = Conversion ())
    {
        typename UnsignedInteger <Bytes>::CalcType bi (rv.toCalcType (false));
        std::size_t const radix (Conversion::radix);
        bi.clear ();
        for (std::string::const_iterator iter (s.begin()); iter != s.end(); ++iter)
        {
            int const v (c.invert (*iter));
            if (v == -1)
                return false;
            bi *= radix;
            bi += v;
        }
        bi.toCanonical();
        return true;
    }
};

//------------------------------------------------------------------------------

// Some common code
template <class Conversion>
class BaseConversion
{
public:
    char map (std::size_t i) const
    {
        return Conversion::alphabet () [i];
    }

    int invert (char c) const
    {
        return Conversion::inverse_alphabet () [c];
    }

    static std::vector <int> const& inverse_alphabet ()
    {
        static std::vector <int> t (invert (Conversion::alphabet(), Conversion::radix));
        return t;
    }

    /** Build the inverse mapping table from characters to digits. */
    static std::vector <int>
    invert (std::string const& alphabet, int radix)
    {
        std::vector <int> table (256, -1);
        for (int i (0); i < radix; ++i)
            table [alphabet [i]] = i;
        return table;
    }

};

//------------------------------------------------------------------------------

/** Foolproof hexadecimal encoding and decoding facility.
    This is to check the correctness of the more complex converters.
*/
class HexEncoding
{
public:
    template <std::size_t Bytes>
    static std::string encode (UnsignedInteger <Bytes> const& v)
    {
        std::string s;
        std::uint8_t const* src (v.cbegin()-1);
        char const* const tab (alphabet().c_str());
        s.reserve (Bytes * 2);
        for (std::size_t bytes (v.size);bytes--;)
        {
            std::uint8_t const v (*++src);
            s.push_back (tab [v>>4]);
            s.push_back (tab [v&0x0f]);
        }
        return s;
    }

    template <std::size_t Bytes>
    static bool decode (UnsignedInteger <Bytes>& rv,
        std::string const& s)
    {
        // can't have an odd size
        if (s.size() & 1)
            return false;
        std::uint8_t* dest (rv.begin()-1);
        int const* const tab (&inverse_alphabet().front());
        for (std::string::const_iterator iter (s.begin()); iter != s.end();)
        {
            int const n1 (tab [*iter++]);
            if (n1 == -1)
                return false;
            int const n2 (tab [*iter++]);
            if (n2 == -1)
                return false;
            *++dest = ((std::uint8_t)((n1<<4)|n2));
        }
        return true;
    }

    static std::string const& alphabet ()
    {
        static std::string s ("0123456789ABCDEF");
        return s;
    }

    static std::vector <int> const& inverse_alphabet ()
    {
        static std::vector <int> s (invert (alphabet(), 16));
        return s;
    }

    /** Build the inverse mapping table from characters to digits. */
    static std::vector <int>
    invert (std::string const& alphabet, int radix)
    {
        std::vector <int> table (256, -1);
        for (int i (0); i < radix; ++i)
            table [alphabet [i]] = int(i);
        return table;
    }
};

//------------------------------------------------------------------------------

/** Base58 conversion used by Bitcoin. */
class BitcoinBase58Conversion : public BaseConversion <BitcoinBase58Conversion>
{
public:
    static std::size_t const radix = 58;

    char const* name () const
    {
        return "BitcoinBase58";
    }

    static std::string const& alphabet ()
    {
        static std::string s (
            "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
        );
        return s;
    }
};

//------------------------------------------------------------------------------

/** Base58 conversion used by Ripple. */
class RippleBase58Conversion : public BaseConversion <RippleBase58Conversion>
{
public:
    static std::size_t const radix = 58;

    char const* name () const
    {
        return "RippleBase58";
    }

    static std::string const& alphabet ()
    {
        static std::string s (
            "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz"
        );
        return s;
    }
};

//------------------------------------------------------------------------------

class Base64Conversion : public BaseConversion <Base64Conversion>
{
public:
    static std::size_t const radix = 64;

    char const* name () const
    {
        return "Base64";
    }

    static std::string const& alphabet ()
    {
        static std::string s (
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
        );
        return s;
    }
};

//------------------------------------------------------------------------------

class Base16Conversion : public BaseConversion <Base16Conversion>
{
public:
    static std::size_t const radix = 16;

    char const* name () const
    {
        return "Hex";
    }

    static std::string const& alphabet ()
    {
        static std::string s (
            "0123456789ABCDEF"
        );
        return s;
    }
};

//------------------------------------------------------------------------------

class BinaryEncoding_test : public unit_test::suite
{
public:
    // This is a baseline for the other tests
    template <std::size_t Bytes>
    void testBase16 ()
    {
        Random r;
        testcase ("base16");
        for (int i = 0; i < 50; ++i)
        {
            typedef UnsignedInteger <Bytes> UInt;
            UInt v0;
            r.fillBitsRandomly (v0.begin(), UInt::size);
            std::string const good (HexEncoding::encode (v0));

            UInt v1;
            bool const success (HexEncoding::decode (v1, good));
            if (expect (success))
            {
                expect (v0 == v1);

                Base16Conversion c;
                std::string const check (BinaryEncoding::encode (v0, c));
                expect (good == check,
                    std::string ("expected ") + good + " but got " + check);
            }
        }
    }

    template <std::size_t Bytes>
    void testBase64Bytes (
        std::string const& vin, std::string const& vout,
        Base64Conversion c = Base64Conversion ())
    {
        typedef UnsignedInteger <Bytes> UInt;
        UInt v1 (vin.c_str());
        std::string const s1 (BinaryEncoding::encode (v1, c));
        log <<
            vout + " to " + s1;
        expect (vout == s1);

        UInt v2;
        bool const success (BinaryEncoding::decode (v2, vout, c));
        if (expect (success))
        {
            std::string const s2 (BinaryEncoding::encode (v2, c));
            log <<
                vin + " to " + s2;
        }
    }

    void testBase64 ()
    {
        testcase ("Base64");

        // input (uint)
        std::string const vin [] = {
            "","f","fo","foo","foob","fooba","foobar"
        };

        // output (encoded string)
        std::string const vout [] = {
            "","Zg==","Zm8=","Zm9v","Zm9vYg==","Zm9vYmE=","Zm9vYmFy"
        };

        //testBase64Bytes <0> (vin [0], vout [0]);
        testBase64Bytes <1> (vin [1], vout [1]);
        testBase64Bytes <2> (vin [2], vout [2]);
        testBase64Bytes <3> (vin [3], vout [3]);
        testBase64Bytes <4> (vin [4], vout [4]);
        testBase64Bytes <5> (vin [5], vout [5]);
        testBase64Bytes <6> (vin [6], vout [6]);
    }

    template <typename Conversion, std::size_t Bytes>
    void testEncode (Conversion c = Conversion())
    {
        typedef UnsignedInteger <Bytes> UInt;

        std::stringstream ss;
        ss <<
            c.name() << " <" << Bytes << ">";
        testcase (ss.str());

        Random r;
        for (int i = 0; i < 50; ++i)
        {
            UInt v1;
            r.fillBitsRandomly (v1.begin(), UInt::size);
            std::string const s1 (BinaryEncoding::encode (v1, c));

            UInt v2;
            bool success (BinaryEncoding::decode (v2, s1, c));
            if (expect (success))
                expect (v1 == v2);
        }
    }

    void run ()
    {
        testBase16 <10> ();

#if 0
        testEncode <Base16Conversion, 1>  ();
        testEncode <Base16Conversion, 2>  ();
        testEncode <Base16Conversion, 3>  ();
        testEncode <Base16Conversion, 4>  ();
        testEncode <Base16Conversion, 5>  ();
        testEncode <Base16Conversion, 6>  ();
        testEncode <Base16Conversion, 7>  ();
        testEncode <Base16Conversion, 8>  ();
        testEncode <Base16Conversion, 9>  ();
        testEncode <Base16Conversion, 10> ();

        testBase64 ();
        testEncode <Base64Conversion, 20> ();
        testEncode <Base64Conversion, 33> ();
        testEncode <RippleBase58Conversion, 20> ();
        testEncode <RippleBase58Conversion, 33> ();
        testEncode <BitcoinBase58Conversion, 20> ();
        testEncode <BitcoinBase58Conversion, 33> ();
#endif
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(BinaryEncoding,crypto,beast);

}
