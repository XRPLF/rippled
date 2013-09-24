//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_TYPES_UINT128_H_INCLUDED
#define RIPPLE_TYPES_UINT128_H_INCLUDED

#include "base_uint.h"

namespace ripple {

class uint128 : public base_uint128
{
public:
    typedef base_uint128 basetype;

    uint128 ()
    {
        zero ();
    }

    uint128 (const basetype& b)
    {
        *this = b;
    }

    uint128& operator= (const basetype& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];

        return *this;
    }

    // VFALCO NOTE This looks dangerous and wouldn't be obvious at call
    //             sites what is being performed here.
    //
    explicit uint128 (const base_uint256& b)
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = b.pn[i];
    }

    explicit uint128 (Blob const& vch)
    {
        if (vch.size () == size ())
            memcpy (pn, &vch[0], size ());
        else
            zero ();
    }

};

}

#endif
