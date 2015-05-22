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

#ifndef RIPPLE_BASICS_BASE_UINT_H_INCLUDED
#define RIPPLE_BASICS_BASE_UINT_H_INCLUDED

#include <ripple/basics/ByteOrder.h>
#include <ripple/basics/Blob.h>
#include <ripple/basics/strHex.h>
#include <ripple/basics/hardened_hash.h>
#include <beast/utility/Zero.h>

#include <boost/functional/hash.hpp>

#include <functional>

using beast::zero;
using beast::Zero;

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

    static std::size_t const        bytes = Bits / 8;

    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;
    using value_type             = unsigned char;
    using pointer                = value_type*;
    using reference              = value_type&;
    using const_pointer          = value_type const*;
    using const_reference        = value_type const&;
    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using tag_type               = Tag;

    pointer data() { return reinterpret_cast<pointer>(pn); }
    const_pointer data() const { return reinterpret_cast<const_pointer>(pn); }

    iterator begin() { return data(); }
    iterator end()   { return data()+bytes; }
    const_iterator begin()  const { return data(); }
    const_iterator end()    const { return data()+bytes; }
    const_iterator cbegin() const { return data(); }
    const_iterator cend()   const { return data()+bytes; }

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenarate parent containers.
    */
    using hasher = hardened_hash <>;

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
        memcpy (&pn [0], data, bytes);
    }

public:
    base_uint () { *this = beast::zero; }

    explicit base_uint (Blob const& vch)
    {
        assert (vch.size () == size ());

        if (vch.size () == size ())
            memcpy (pn, &vch[0], size ());
        else
            *this = beast::zero;
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

    base_uint (base_uint<Bits, Tag> const& other) = default;

    template <class OtherTag>
    void copyFrom (base_uint<Bits, OtherTag> const& other)
    {
        memcpy (&pn [0], other.data(), bytes);
    }

    /* Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.
    */
    static base_uint
    fromVoid (void const* data)
    {
        return base_uint (data, VoidHelper ());
    }

    int signum() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (pn[i] != 0)
                return 1;

        return 0;
    }

    bool operator! () const
    {
        return *this == beast::zero;
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
        *this = beast::zero;
        union
        {
            unsigned u[2];
            std::uint64_t ul;
        };
        // Put in least significant bits.
        ul = htobe64 (uHost);
        pn[WIDTH-2] = u[0];
        pn[WIDTH-1] = u[1];
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

    bool SetHexExact (const char* psz)
    {
        // must be precisely the correct number of hex digits
        unsigned char* pOut  = begin ();

        for (int i = 0; i < sizeof (pn); ++i)
        {
            auto cHigh = charUnHex(*psz++);
            auto cLow  = charUnHex(*psz++);

            if (cHigh == -1 || cLow == -1)
                return false;

            *pOut++ = (cHigh << 4) | cLow;
        }

        assert (*psz == 0);
        assert (pOut == end ());

        return true;
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

        const unsigned char* pEnd   = reinterpret_cast<const unsigned char*> (psz);
        const unsigned char* pBegin = pEnd;

        // Find end.
        while (charUnHex(*pEnd) != -1)
            pEnd++;

        // Take only last digits of over long string.
        if ((unsigned int) (pEnd - pBegin) > 2 * size ())
            pBegin = pEnd - 2 * size ();

        unsigned char* pOut = end () - ((pEnd - pBegin + 1) / 2);

        *this = beast::zero;

        if ((pEnd - pBegin) & 1)
            *pOut++ = charUnHex(*pBegin++);

        while (pBegin != pEnd)
        {
            auto cHigh = charUnHex(*pBegin++);
            auto cLow  = pBegin == pEnd
                            ? 0
                            : charUnHex(*pBegin++);

            if (cHigh == -1 || cLow == -1)
                return false;

            *pOut++ = (cHigh << 4) | cLow;
        }

        return !*pEnd;
    }

    bool SetHex (std::string const& str, bool bStrict = false)
    {
        return SetHex (str.c_str (), bStrict);
    }

    void SetHexExact (std::string const& str)
    {
        SetHexExact (str.c_str ());
    }

    unsigned int size () const
    {
        return sizeof (pn);
    }

    base_uint<Bits, Tag>& operator=(Zero)
    {
        memset (&pn[0], 0, sizeof (pn));
        return *this;
    }

    // Deprecated.
    bool isZero () const { return *this == beast::zero; }
    bool isNonZero () const { return *this != beast::zero; }
    void zero () { *this = beast::zero; }
};

using uint128 = base_uint<128>;
using uint160 = base_uint<160>;
using uint256 = base_uint<256>;

template <std::size_t Bits, class Tag>
inline int compare (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
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
inline bool operator< (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) < 0;
}

template <std::size_t Bits, class Tag>
inline bool operator<= (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) <= 0;
}

template <std::size_t Bits, class Tag>
inline bool operator> (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) > 0;
}

template <std::size_t Bits, class Tag>
inline bool operator>= (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) >= 0;
}

template <std::size_t Bits, class Tag>
inline bool operator== (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) == 0;
}

template <std::size_t Bits, class Tag>
inline bool operator!= (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare (a, b) != 0;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline bool operator== (base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return a == base_uint<Bits, Tag>(b);
}

template <std::size_t Bits, class Tag>
inline bool operator!= (base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return !(a == b);
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag> operator^ (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) ^= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag> operator& (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) &= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag> operator| (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) |= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag> operator+ (
    base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag> (a) += b;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline std::string to_string (base_uint<Bits, Tag> const& a)
{
    return strHex (a.begin (), a.size ());
}

template <std::size_t Bits, class Tag>
inline std::ostream& operator<< (
    std::ostream& out, base_uint<Bits, Tag> const& u)
{
    return out << to_string (u);
}

} // rippled

namespace boost
{

template <std::size_t Bits, class Tag>
struct hash<ripple::base_uint<Bits, Tag>>
{
    using argument_type = ripple::base_uint<Bits, Tag>;

    std::size_t
    operator()(argument_type const& u) const
    {
        return ripple::hardened_hash<>{}(u);
    }
};

}  // boost

#endif
