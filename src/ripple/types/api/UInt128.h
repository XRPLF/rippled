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

#ifndef RIPPLE_TYPES_UINT128_H_INCLUDED
#define RIPPLE_TYPES_UINT128_H_INCLUDED

#include "base_uint.h"

namespace ripple {

class uint128 : public base_uint128
{
private:
    uint128 (value_type const* ptr)
    {
        memcpy (pn, ptr, bytes);
    }

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

    explicit uint128 (Blob const& vch)
    {
        if (vch.size () == size ())
            memcpy (pn, &vch[0], size ());
        else
            zero ();
    }

    static uint128 low128(base_uint256 const& b)
    {
        return uint128 (b.data());
    }
};

}

#endif
