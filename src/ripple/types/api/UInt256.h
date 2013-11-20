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

#ifndef RIPPLE_TYPES_UINT256_H_INCLUDED
#define RIPPLE_TYPES_UINT256_H_INCLUDED

#include "base_uint.h"

namespace ripple {

class uint256 : public base_uint256
{
public:
    typedef base_uint256 basetype;

    uint256 ()
    {
        zero ();
    }

    uint256 (const basetype& b)
    {
        *this   = b;
    }

    uint256& operator= (const basetype& b)
    {
        if (pn != b.pn)
            memcpy (pn, b.pn, sizeof (pn));

        return *this;
    }

    uint256 (uint64 b)
    {
        *this = b;
    }

private:
    uint256 (void const* data, FromVoid)
        : base_uint256 (data, FromVoid ())
    {
    }
public:
    static uint256 fromVoid (void const* data)
    {
        return uint256 (data, FromVoid ());
    }

    uint256& operator= (uint64 uHost)
    {
        zero ();

        // Put in least significant bits.
        ((uint64*) end ())[-1]   = htobe64 (uHost);

        return *this;
    }

    explicit uint256 (const std::string& str)
    {
        SetHex (str);
    }

    explicit uint256 (Blob const& vch)
    {
        if (vch.size () == sizeof (pn))
            memcpy (pn, &vch[0], sizeof (pn));
        else
        {
            assert (false);
            zero ();
        }
    }
};

inline bool operator== (uint256 const& a, uint64 b)
{
    return (base_uint256)a == b;
}

inline bool operator!= (uint256 const& a, uint64 b)
{
    return (base_uint256)a != b;
}

inline const uint256 operator^ (const base_uint256& a, const base_uint256& b)
{
    return uint256 (a) ^= b;
}

inline const uint256 operator& (const base_uint256& a, const base_uint256& b)
{
    return uint256 (a) &= b;
}

inline const uint256 operator| (const base_uint256& a, const base_uint256& b)
{
    return uint256 (a) |= b;
}

inline bool operator== (const base_uint256& a, uint256 const& b)
{
    return (base_uint256)a == (base_uint256)b;
}

inline bool operator!= (const base_uint256& a, uint256 const& b)
{
    return (base_uint256)a != (base_uint256)b;
}

inline const uint256 operator^ (const base_uint256& a, uint256 const& b)
{
    return (base_uint256)a ^  (base_uint256)b;
}

inline const uint256 operator& (const base_uint256& a, uint256 const& b)
{
    return (base_uint256)a &  (base_uint256)b;
}

inline const uint256 operator| (const base_uint256& a, uint256 const& b)
{
    return (base_uint256)a |  (base_uint256)b;
}

inline bool operator== (uint256 const& a, const base_uint256& b)
{
    return (base_uint256)a == (base_uint256)b;
}

inline bool operator!= (uint256 const& a, const base_uint256& b)
{
    return (base_uint256)a != (base_uint256)b;
}

inline const uint256 operator^ (uint256 const& a, const base_uint256& b)
{
    return (base_uint256)a ^  (base_uint256)b;
}

inline const uint256 operator& (uint256 const& a, const base_uint256& b)
{
    return uint256 (a) &= b;
}

inline const uint256 operator| (uint256 const& a, const base_uint256& b)
{
    return (base_uint256)a |  (base_uint256)b;
}

inline bool operator== (uint256 const& a, uint256 const& b)
{
    return (base_uint256)a == (base_uint256)b;
}

inline bool operator!= (uint256 const& a, uint256 const& b)
{
    return (base_uint256)a != (base_uint256)b;

}

inline const uint256 operator^ (uint256 const& a, uint256 const& b)
{
    return (base_uint256)a ^  (base_uint256)b;
}

inline const uint256 operator& (uint256 const& a, uint256 const& b)
{
    return (base_uint256)a &  (base_uint256)b;
}

inline const uint256 operator| (uint256 const& a, uint256 const& b)
{
    return (base_uint256)a |  (base_uint256)b;
}

inline int Testuint256AdHoc (std::vector<std::string> vArg)
{
    uint256 g (uint64 (0));

    printf ("%s\n", g.ToString ().c_str ());
    --g;
    printf ("--g\n");
    printf ("%s\n", g.ToString ().c_str ());
    g--;
    printf ("g--\n");
    printf ("%s\n", g.ToString ().c_str ());
    g++;
    printf ("g++\n");
    printf ("%s\n", g.ToString ().c_str ());
    ++g;
    printf ("++g\n");
    printf ("%s\n", g.ToString ().c_str ());
    g++;
    printf ("g++\n");
    printf ("%s\n", g.ToString ().c_str ());
    ++g;
    printf ("++g\n");
    printf ("%s\n", g.ToString ().c_str ());



    uint256 a (7);
    printf ("a=7\n");
    printf ("%s\n", a.ToString ().c_str ());

    uint256 b;
    printf ("b undefined\n");
    printf ("%s\n", b.ToString ().c_str ());
    int c = 3;

    a = c;
    a.pn[3] = 15;
    printf ("%s\n", a.ToString ().c_str ());
    uint256 k (c);

    a = 5;
    a.pn[3] = 15;
    printf ("%s\n", a.ToString ().c_str ());
    b = 1;
    // b <<= 52;

    a |= b;

    // a ^= 0x500;

    printf ("a %s\n", a.ToString ().c_str ());

    a = a | b | (uint256)0x1000;


    printf ("a %s\n", a.ToString ().c_str ());
    printf ("b %s\n", b.ToString ().c_str ());

    a = 0xfffffffe;
    a.pn[4] = 9;

    printf ("%s\n", a.ToString ().c_str ());
    a++;
    printf ("%s\n", a.ToString ().c_str ());
    a++;
    printf ("%s\n", a.ToString ().c_str ());
    a++;
    printf ("%s\n", a.ToString ().c_str ());
    a++;
    printf ("%s\n", a.ToString ().c_str ());

    a--;
    printf ("%s\n", a.ToString ().c_str ());
    a--;
    printf ("%s\n", a.ToString ().c_str ());
    a--;
    printf ("%s\n", a.ToString ().c_str ());
    uint256 d = a--;
    printf ("%s\n", d.ToString ().c_str ());
    printf ("%s\n", a.ToString ().c_str ());
    a--;
    printf ("%s\n", a.ToString ().c_str ());
    a--;
    printf ("%s\n", a.ToString ().c_str ());

    d = a;

    printf ("%s\n", d.ToString ().c_str ());

    for (int i = uint256::WIDTH - 1; i >= 0; i--) printf ("%08x", d.pn[i]);

    printf ("\n");

    uint256 neg = d;
    neg = ~neg;
    printf ("%s\n", neg.ToString ().c_str ());


    uint256 e = uint256 ("0xABCDEF123abcdef12345678909832180000011111111");
    printf ("\n");
    printf ("%s\n", e.ToString ().c_str ());


    printf ("\n");
    uint256 x1 = uint256 ("0xABCDEF123abcdef12345678909832180000011111111");
    uint256 x2;
    printf ("%s\n", x1.ToString ().c_str ());

    for (int i = 0; i < 270; i += 4)
    {
        // x2 = x1 << i;
        printf ("%s\n", x2.ToString ().c_str ());
    }

    printf ("\n");
    printf ("%s\n", x1.ToString ().c_str ());

    for (int i = 0; i < 270; i += 4)
    {
        x2 = x1;
        // x2 >>= i;
        printf ("%s\n", x2.ToString ().c_str ());
    }

#if 0

    for (int i = 0; i < 100; i++)
    {
        uint256 k = (~uint256 (0) >> i);
        printf ("%s\n", k.ToString ().c_str ());
    }

    for (int i = 0; i < 100; i++)
    {
        uint256 k = (~uint256 (0) << i);
        printf ("%s\n", k.ToString ().c_str ());
    }

#endif

    return (0);
}

extern std::size_t hash_value (uint256 const& );

}

#endif
