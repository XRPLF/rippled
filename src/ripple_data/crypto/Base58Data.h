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

#include ".././ripple/types/api/Base58.h"

namespace ripple {

class CBase58Data
{
protected:
    unsigned char nVersion;
    Blob vchData;

    CBase58Data ();
    ~CBase58Data ();

    void SetData (int nVersionIn, Blob const& vchDataIn);
    void SetData (int nVersionIn, const void* pdata, size_t nSize);
    void SetData (int nVersionIn, const unsigned char* pbegin, const unsigned char* pend);

public:
    bool SetString (const char* psz, unsigned char version, Base58::Alphabet const& alphabet = Base58::getCurrentAlphabet ());
    bool SetString (const std::string& str, unsigned char version);

    std::string ToString () const;
    int CompareTo (const CBase58Data& b58) const;

    bool operator== (const CBase58Data& b58) const;
    bool operator!= (const CBase58Data& b58) const;
    bool operator<= (const CBase58Data& b58) const;
    bool operator>= (const CBase58Data& b58) const;
    bool operator< (const CBase58Data& b58) const;
    bool operator> (const CBase58Data& b58) const;

    friend std::size_t hash_value (const CBase58Data& b58);
};

extern std::size_t hash_value (const CBase58Data& b58);

} // ripple

#endif
