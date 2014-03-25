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

#include "../../beast/beast/container/hardened_hash.h"

#include <functional>

namespace ripple {

class uint128;
class uint160;
class uint256;
inline int Testuint256AdHoc (std::vector<std::string> vArg);

// This class stores its values internally in big-endian form

// We have to keep a separate base class without constructors
// so the compiler will let us use it in a union
//
// VFALCO NOTE This class produces undefined behavior when
//             BITS is not a multiple of 32!!!
//
template<unsigned int BITS>
class base_uint
{
protected:
    enum { WIDTH = BITS / 32 };

    // This is really big-endian in byte order.
    // We sometimes use unsigned int for speed.
    unsigned int pn[WIDTH];

public:
    /** Construct uninitialized.
        Requirements:
            std::is_trivially_constructible<base_uint>::value == true
    */
    base_uint () { }

    //--------------------------------------------------------------------------
    //
    // STL Container Interface
    //

    static std::size_t const        bytes = (BITS/8);

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

protected:
    // This is to disambiguate from other 1 parameter ctors
    struct FromVoid { };

    /** Construct from a raw pointer.
    
        The buffer pointed to by `data` must be at least 32 bytes.
    */
    base_uint (void const* data, FromVoid)
    {
        // BITS must be a multiple of 32
        static_bassert ((BITS % 32) == 0);

        memcpy (&pn [0], data, BITS / 8);
    }
public:
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

    std::size_t hash_combine (std::size_t& seed) const noexcept
    {
        for (int i = 0; i < WIDTH; ++i)
            boost::hash_combine (seed, pn[i]);

        return seed;
    }

    friend inline int compare (const base_uint& a, const base_uint& b)
    {
        const unsigned char* pA     = a.begin ();
        const unsigned char* pAEnd  = a.end ();
        const unsigned char* pB     = b.begin ();

        while (*pA == *pB)
        {
            if (++pA == pAEnd)
                return 0;

            ++pB;
        }

        return (*pA < *pB) ? -1 : 1;
    }

    friend inline bool operator< (const base_uint& a, const base_uint& b)
    {
        return compare (a, b) < 0;
    }

    friend inline bool operator<= (const base_uint& a, const base_uint& b)
    {
        return compare (a, b) <= 0;
    }

    friend inline bool operator> (const base_uint& a, const base_uint& b)
    {
        return compare (a, b) > 0;
    }

    friend inline bool operator>= (const base_uint& a, const base_uint& b)
    {
        return compare (a, b) >= 0;
    }

    friend inline bool operator== (const base_uint& a, const base_uint& b)
    {
        return memcmp (a.pn, b.pn, sizeof (a.pn)) == 0;
    }

    friend inline bool operator!= (const base_uint& a, const base_uint& b)
    {
        return memcmp (a.pn, b.pn, sizeof (a.pn)) != 0;
    }

    std::string GetHex () const
    {
        return strHex (begin (), size ());
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

    std::string ToString () const
    {
        return GetHex ();
    }

    unsigned int size () const
    {
        return sizeof (pn);
    }

    void zero ()
    {
        memset (&pn[0], 0, sizeof (pn));
    }

    unsigned int GetSerializeSize (int nType = 0) const
    {
        return sizeof (pn);
    }

    template<typename Stream>
    void Serialize (Stream& s, int nType = 0) const
    {
        s.write ((char*)pn, sizeof (pn));
    }

    template<typename Stream>
    void Unserialize (Stream& s, int nType = 0)
    {
        s.read ((char*)pn, sizeof (pn));
    }

    friend class uint128;
    friend class uint160;
    friend class uint256;
    friend inline int Testuint256AdHoc (std::vector<std::string> vArg);
};

typedef base_uint<128> base_uint128;
typedef base_uint<160> base_uint160;
typedef base_uint<256> base_uint256;

template<unsigned int BITS>
std::ostream& operator<< (std::ostream& out, const base_uint<BITS>& u)
{
    return out << u.GetHex ();
}

}

//------------------------------------------------------------------------------

namespace std {

/** Specialization for hash. */
template <unsigned int BITS>
struct hash <ripple::base_uint <BITS> >
{
public:
    typedef ripple::base_uint <BITS> argument_type;
    typedef std::size_t              result_type;

    hash ()
    {
        static typename argument_type::hasher s_hash;
        m_hash = s_hash;
    }

    template <typename Arg>
    explicit hash (Arg arg) : m_hash (arg)
    {
    }

    result_type operator() (argument_type const& key) const
    {
        return m_hash (key);
    }

private:
    typename argument_type::hasher m_hash;
};

//------------------------------------------------------------------------------

/** Specialization for equal_to. */
template <unsigned int BITS>
struct equal_to <ripple::base_uint <BITS> >
{
public:
    typedef bool                    result_type;
    typedef ripple::base_uint<BITS> argument_type;
    typedef argument_type           first_argument_type;
    typedef argument_type           second_argument_type;

    equal_to ()
    {
    }

    template <typename Arg>
    explicit equal_to (Arg arg)
        : m_equal (arg)
    {
    }

    result_type operator() (argument_type const& lhs,
                            argument_type const& rhs) const
    {
        return m_equal (lhs, rhs);
    }

private:
    typename argument_type::equal m_equal;
};

}

#endif
