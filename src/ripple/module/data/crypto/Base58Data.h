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
// Copyright (c) 2011 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//
// Why base-58 instead of standard base-64 encoding?
// - Don't want 0OIl characters that look the same in some fonts and
//      could be used to create visually identical looking account numbers.
// - A string with non-alphanumeric characters is not as easily accepted as an account number.
// - E-mail usually won't line-break if there's no punctuation to break at.
// - Doubleclicking selects the whole number as one word if it's all alphanumeric.
//
#ifndef RIPPLE_BASE58DATA_H
#define RIPPLE_BASE58DATA_H

#include <ripple/types/api/Base58.h>
#include <ripple/types/api/base_uint.h>

namespace ripple {

class CBase58Data
{
protected:
    // NIKB TODO: combine nVersion into vchData so that CBase58Data becomes
    //            unnecessary and is replaced by a couple of helper functions
    //            that operate on a Blob.

    unsigned char nVersion;
    Blob vchData;

    CBase58Data ();
    ~CBase58Data ();

    void SetData (int version, Blob const& vchDataIn)
    {
        nVersion = version;
        vchData = vchDataIn;
    }

    template <size_t Bits, class Tag>
    void SetData (int version, base_uint<Bits, Tag> const& from)
    {
        nVersion = version;

        vchData.resize (from.size ());

        std::copy (std::begin (from), std::end(from), std::begin (vchData));
    }

public:
    bool SetString (std::string const& str, unsigned char version,
        Base58::Alphabet const& alphabet);

    std::string ToString () const;

    int compare (const CBase58Data& b58) const
    {
        if (nVersion < b58.nVersion)
            return -1;

        if (nVersion > b58.nVersion)
            return  1;

        if (vchData < b58.vchData)
            return -1;

        if (vchData > b58.vchData)
            return  1;

        return 0;
    }

    template <class Hasher>
    friend
    void
    hash_append(Hasher& hasher, CBase58Data const& value)
    {
        beast::hash_append(hasher, value.vchData);
    }

    friend std::size_t hash_value (const CBase58Data& b58);
};

inline bool
operator== (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) == 0;
}

inline bool
operator!= (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) != 0;
}

inline bool
operator< (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) <  0;
}

inline bool
operator<= (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) <= 0;
}

inline bool
operator> (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) > 0;
}

inline bool
operator>= (CBase58Data const& lhs, CBase58Data const& rhs)
{
    return lhs.compare (rhs) >= 0;
}

extern std::size_t hash_value (const CBase58Data& b58);

} // ripple

#endif
