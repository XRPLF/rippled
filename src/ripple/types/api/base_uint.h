//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_TYPES_BASE_UINT_H_INCLUDED
#define RIPPLE_TYPES_BASE_UINT_H_INCLUDED

#include "Blob.h"
#include "strHex.h"
#include "ByteOrder.h"
    
#include "../../beast/beast/container/hardened_hash.h"

#include <functional>

namespace ripple {

// This class stores its values internally in big-endian form

template <std::size_t Bits, class Tag = void>
class base_uint
{
    static_assert ((Bits % 32) == 0,
        "The length of a base_uint in bits must be a multiple of 32.");

    static_assert (Bits >= 64,
        "The length of a base_uint in bits must be at least 64.");

protected:
    enum { WIDTH = Bits / 32 };

    // This is really big-endian in byte order.
    // We sometimes use unsigned int for speed.

    // NIKB TODO: migrate to std::array
    unsigned int pn[WIDTH];

public:
    //--------------------------------------------------------------------------
    //
    // STL Container Interface
    //

    static std::size_t const        bytes = Bits/8;

    typedef std::size_t             size_type;
    typedef std::ptrdiff_t          difference_type;
    typedef unsigned char           value_type;
    typedef value_type*             pointer;
    typedef value_type&             reference;
    typedef value_type const*       const_pointer;
    typedef value_type const&       const_reference;
    typedef pointer                 iterator;
    typedef const_pointer           const_iterator;
    typedef std::reverse_iterator
        <iterator>                  reverse_iterator;
    typedef std::reverse_iterator
        <const_iterator>            const_reverse_iterator;

    typedef Tag                     tag_type;

    pointer data() { return reinterpret_cast<pointer>(pn); }
    const_pointer data() const { return reinterpret_cast<const_pointer>(pn); }

    iterator begin() { return data(); }
    iterator end()   { return data()+bytes; }
    const_iterator begin()  const { return data(); }
    const_iterator end()    const { return data()+bytes; }
    const_iterator cbegin() const { return data(); }
    const_iterator cend()   const { return data()+bytes; }

    reverse_iterator rbegin() { return end(); }
    reverse_iterator rend()   { return begin(); }
    const_reverse_iterator rbegin()  const { return end(); }
    const_reverse_iterator rend()    const { return begin(); }
    const_reverse_iterator crbegin() const { return cend(); }
    const_reverse_iterator crend()   const { return cbegin(); }

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenarate parent containers.
    */
    typedef beast::hardened_hash <base_uint> hasher;

    /** Container equality testing function. */
    class key_equal
    {
    public:
        bool operator() (base_uint const& lhs, base_uint const& rhs) const
        {
            return lhs == rhs;
        }
    };

    //--------------------------------------------------------------------------

private:
    /** Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.

        @note the structure is used to disambiguate this from the std::uint64_t
              constructor: something like base_uint(0) is ambiguous.
    */
    // NIKB TODO Remove the need for this constructor.
    struct VoidHelper {};

    explicit base_uint (void const* data, VoidHelper)
    {
        memcpy (&pn [0], data, Bits / 8);
    }

public:
    base_uint () { zero (); }

    explicit base_uint (Blob const& vch)
    {
        assert (vch.size () == size ());

        if (vch.size () == size ())
            memcpy (pn, &vch[0], size ());
        else
            zero ();
    }

    explicit base_uint (std::uint64_t b)
    {
        *this = b;
    }

    // NIKB TODO remove the need for this constructor - have a free function
    //           to handle the hex string parsing.
    explicit base_uint (std::string const& str)
    {
        SetHex (str);
    }

    base_uint (base_uint const& other) = default;
    
    /* Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.
    */
    static base_uint
    fromVoid (void const* data)
    {
        return base_uint (data, VoidHelper ());
    }

    bool isZero () const
    {
        for (int i = 0; i < WIDTH; i++)
            if (pn[i] != 0)
                return false;

        return true;
    }

    bool isNonZero () const
    {
        return !isZero ();
    }

    bool operator! () const
    {
        return isZero ();
    }

    const base_uint operator~ () const
    {
        base_uint ret;

        for (int i = 0; i < WIDTH; i++)
            ret.pn[i] = ~pn[i];

        return ret;
    }

    base_uint& operator= (const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];

        return *this;
    }

    base_uint& operator= (std::uint64_t uHost)
    {
        zero ();

        // Put in least significant bits.
        ((std::uint64_t*) end ())[-1] = htobe64 (uHost);

        return *this;
    }

    base_uint& operator^= (const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] ^= b.pn[i];

        return *this;
    }

    base_uint& operator&= (const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] &= b.pn[i];

        return *this;
    }

    base_uint& operator|= (const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] |= b.pn[i];

        return *this;
    }

    base_uint& operator++ ()
    {
        // prefix operator
        for (int i = WIDTH - 1; i >= 0; --i)
        {
            pn[i] = htobe32 (be32toh (pn[i]) + 1);

            if (pn[i] != 0)
                break;
        }

        return *this;
    }

    const base_uint operator++ (int)
    {
        // postfix operator
        const base_uint ret = *this;
        ++ (*this);

        return ret;
    }

    base_uint& operator-- ()
    {
        for (int i = WIDTH - 1; i >= 0; --i)
        {
            std::uint32_t prev = pn[i];
            pn[i] = htobe32 (be32toh (pn[i]) - 1);

            if (prev != 0)
                break;
        }

        return *this;
    }

    const base_uint operator-- (int)
    {
        // postfix operator
        const base_uint ret = *this;
        -- (*this);

        return ret;
    }

    base_uint& operator+= (const base_uint& b)
    {
        std::uint64_t carry = 0;

        for (int i = WIDTH; i--;)
        {
            std::uint64_t n = carry + be32toh (pn[i]) + be32toh (b.pn[i]);

            pn[i] = htobe32 (n & 0xffffffff);
            carry = n >> 32;
        }

        return *this;
    }

    template <class Hasher>
    friend void hash_append(Hasher& h, base_uint const& a) noexcept
    {
        using beast::hash_append;
        hash_append (h, a.pn);
    }

    void SetHexExact (const char* psz)
    {
        // must be precisely the correct number of hex digits
        static signed char phexdigit[256] =
        {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            0, 1, 2, 3,  4, 5, 6, 7,  8, 9, -1, -1, -1, -1, -1, -1,

            -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        };

        char* pOut  = reinterpret_cast<char*> (pn);

        for (int i = 0; i < sizeof (pn); ++i)
        {
            *pOut = phexdigit[*psz++] << 4;
            *pOut++ |= phexdigit[*psz++];
        }

        assert (*psz == 0);
        assert (pOut == reinterpret_cast<char*> (end ()));
    }

    // Allow leading whitespace.
    // Allow leading "0x".
    // To be valid must be '\0' terminated.
    bool SetHex (const char* psz, bool bStrict = false)
    {
        // skip leading spaces
        if (!bStrict)
            while (isspace (*psz))
                psz++;

        // skip 0x
        if (!bStrict && psz[0] == '0' && tolower (psz[1]) == 'x')
            psz += 2;

        // hex char to int
        static signed char phexdigit[256] =
        {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            0, 1, 2, 3,  4, 5, 6, 7,  8, 9, -1, -1, -1, -1, -1, -1,

            -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        };

        const unsigned char* pEnd   = reinterpret_cast<const unsigned char*> (psz);
        const unsigned char* pBegin = pEnd;

        // Find end.
        while (phexdigit[*pEnd] >= 0)
            pEnd++;

        // Take only last digits of over long string.
        if ((unsigned int) (pEnd - pBegin) > 2 * size ())
            pBegin = pEnd - 2 * size ();

        unsigned char* pOut = end () - ((pEnd - pBegin + 1) / 2);

        zero ();

        if ((pEnd - pBegin) & 1)
            *pOut++ = phexdigit[*pBegin++];

        while (pBegin != pEnd)
        {
            unsigned char   cHigh   = phexdigit[*pBegin++] << 4;
            unsigned char   cLow    = pBegin == pEnd
                                      ? 0
                                      : phexdigit[*pBegin++];
            *pOut++ = cHigh | cLow;
        }

        return !*pEnd;
    }

    bool SetHex (const std::string& str, bool bStrict = false)
    {
        return SetHex (str.c_str (), bStrict);
    }

    void SetHexExact (const std::string& str)
    {
        SetHexExact (str.c_str ());
    }

    unsigned int size () const
    {
        return sizeof (pn);
    }

    void zero ()
    {
        memset (&pn[0], 0, sizeof (pn));
    }
};

typedef base_uint<128> uint128;
typedef base_uint<160> uint160;
typedef base_uint<256> uint256;

//------------------------------------------------------------------------------
extern std::size_t hash_value (uint128 const&);
extern std::size_t hash_value (uint160 const&);
extern std::size_t hash_value (uint256 const&);

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
int 
compare (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    auto ret = std::mismatch (a.cbegin (), a.cend (), b.cbegin ());

    if (ret.first == a.cend ())
        return 0;

    // a > b
    if (*ret.first > *ret.second)
        return 1;

    // a < b
    return -1;
}

template <std::size_t Bits, class Tag>
bool
operator< (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) < 0;
}

template <std::size_t Bits, class Tag>
bool
operator<= (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) <= 0;
}

template <std::size_t Bits, class Tag>
bool
operator> (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) > 0;
}

template <std::size_t Bits, class Tag>
bool
operator>= (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) >= 0;
}

template <std::size_t Bits, class Tag>
bool
operator== (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) == 0;
}

template <std::size_t Bits, class Tag>
bool
operator!= (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) != 0;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag = void>
bool
operator== (base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return a == base_uint<Bits, Tag>(b);
}

template <std::size_t Bits, class Tag = void>
bool
operator!= (base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return !(a == b);
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
const base_uint<Bits, Tag>
operator^ (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) ^= b;
}

template <std::size_t Bits, class Tag>
const base_uint<Bits, Tag>
operator& (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) &= b;
}

template <std::size_t Bits, class Tag>
const base_uint<Bits, Tag>
operator| (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) |= b;
}

template <std::size_t Bits, class Tag>
const base_uint<Bits, Tag>
operator+ (base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) += b;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
std::string to_string (base_uint<Bits, Tag> const& a)
{
    return strHex (a.begin (), a.size ());
}

template <std::size_t Bits, class Tag>
std::ostream& operator<< (std::ostream& out, base_uint<Bits, Tag> const& u)
{
    return out << to_string (u);
}

}

#endif
