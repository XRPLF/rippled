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

#ifndef RIPPLE_PROTOCOL_UINT160_H_INCLUDED
#define RIPPLE_PROTOCOL_UINT160_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/basics/strHex.h>
#include <boost/functional/hash.hpp>
#include <functional>

namespace ripple {

template <class Tag>
uint256 to256 (base_uint<160, Tag> const& a)
{
    // VFALCO This assumes uint256 is zero-initialized
    uint256 m;
    memcpy (m.begin (), a.begin (), a.size ());
    return m;
}

}

//------------------------------------------------------------------------------

namespace std {

template <>
struct hash <ripple::uint160> : ripple::uint160::hasher
{
    // VFALCO NOTE broken in vs2012
    //using ripple::uint160::hasher::hasher; // inherit ctors
};

}

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash <ripple::uint160> : ripple::uint160::hasher
{
    // VFALCO NOTE broken in vs2012
    //using ripple::uint160::hasher::hasher; // inherit ctors
};

}

#endif
