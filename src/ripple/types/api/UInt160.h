//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_TYPES_UINT160_H_INCLUDED
#define RIPPLE_TYPES_UINT160_H_INCLUDED

#include "base_uint.h"

namespace ripple {

class uint160 : public base_uint160
{
public:
    typedef base_uint160 basetype;

    uint160 ()
    {
        zero ();
    }

    uint160 (const basetype& b)
    {
        *this   = b;
    }

    uint160& operator= (const basetype& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];

        return *this;
    }

    uint160 (uint64 b)
    {
        *this = b;
    }

    uint160& operator= (uint64 uHost)
    {
        zero ();

        // Put in least significant bits.
        ((uint64*) end ())[-1]   = htobe64 (uHost);

        return *this;
    }

    explicit uint160 (const std::string& str)
    {
        SetHex (str);
    }

    explicit uint160 (Blob const& vch)
    {
        if (vch.size () == sizeof (pn))
            memcpy (pn, &vch[0], sizeof (pn));
        else
            zero ();
    }

    base_uint256 to256 () const;
};

inline bool operator== (const uint160& a, uint64 b)
{
    return (base_uint160)a == b;
}

inline bool operator!= (const uint160& a, uint64 b)
{
    return (base_uint160)a != b;
}

inline const uint160 operator^ (const base_uint160& a, const base_uint160& b)
{
    return uint160 (a) ^= b;
}

inline const uint160 operator& (const base_uint160& a, const base_uint160& b)
{
    return uint160 (a) &= b;
}

inline const uint160 operator| (const base_uint160& a, const base_uint160& b)
{
    return uint160 (a) |= b;
}

inline bool operator== (const base_uint160& a, const uint160& b)
{
    return (base_uint160)a == (base_uint160)b;
}

inline bool operator!= (const base_uint160& a, const uint160& b)
{
    return (base_uint160)a != (base_uint160)b;
}

inline const uint160 operator^ (const base_uint160& a, const uint160& b)
{
    return (base_uint160)a ^  (base_uint160)b;
}

inline const uint160 operator& (const base_uint160& a, const uint160& b)
{
    return (base_uint160)a &  (base_uint160)b;
}

inline const uint160 operator| (const base_uint160& a, const uint160& b)
{
    return (base_uint160)a |  (base_uint160)b;
}

inline bool operator== (const uint160& a, const base_uint160& b)
{
    return (base_uint160)a == (base_uint160)b;
}
inline bool operator!= (const uint160& a, const base_uint160& b)
{
    return (base_uint160)a != (base_uint160)b;
}
inline const uint160 operator^ (const uint160& a, const base_uint160& b)
{
    return (base_uint160)a ^  (base_uint160)b;
}

inline const uint160 operator& (const uint160& a, const base_uint160& b)
{
    return (base_uint160)a &  (base_uint160)b;
}

inline const uint160 operator| (const uint160& a, const base_uint160& b)
{
    return (base_uint160)a |  (base_uint160)b;
}

inline bool operator== (const uint160& a, const uint160& b)
{
    return (base_uint160)a == (base_uint160)b;
}

inline bool operator!= (const uint160& a, const uint160& b)
{
    return (base_uint160)a != (base_uint160)b;
}

inline const uint160 operator^ (const uint160& a, const uint160& b)
{
    return (base_uint160)a ^  (base_uint160)b;
}

inline const uint160 operator& (const uint160& a, const uint160& b)
{
    return (base_uint160)a &  (base_uint160)b;
}

inline const uint160 operator| (const uint160& a, const uint160& b)
{
    return (base_uint160)a |  (base_uint160)b;
}

inline const std::string strHex (const uint160& ui)
{
    return strHex (ui.begin (), ui.size ());
}

extern std::size_t hash_value (const uint160&);

}

#endif
