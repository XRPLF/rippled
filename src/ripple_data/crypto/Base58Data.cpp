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

namespace ripple {

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

CBase58Data::CBase58Data ()
    : nVersion (1)
{
}

CBase58Data::~CBase58Data ()
{
    // Ensures that any potentially sensitive data is cleared from memory
    std::fill (std::begin (vchData), std::end(vchData), 0);
}

bool CBase58Data::SetString (
    std::string const& str,
    unsigned char version, 
    Base58::Alphabet const& alphabet)
{
    Blob vchTemp;
    Base58::decodeWithCheck (str.c_str (), vchTemp, alphabet);

    if (vchTemp.empty () || vchTemp[0] != version)
    {
        vchData.clear ();
        nVersion = 1;
        return false;
    }

    nVersion = vchTemp[0];

    vchData.assign (vchTemp.begin () + 1, vchTemp.end ());

    // Ensures that any potentially sensitive data is cleared from memory
    std::fill (vchTemp.begin(), vchTemp.end(), 0);

    return true;
}

std::string CBase58Data::ToString () const
{
    Blob vch (1, nVersion);

    vch.insert (vch.end (), vchData.begin (), vchData.end ());

    return Base58::encodeWithCheck (vch);
}

std::size_t hash_value (const CBase58Data& b58)
{
    std::size_t seed = HashMaps::getInstance ().getNonce <size_t> ()
                       + (b58.nVersion * HashMaps::goldenRatio);

    boost::hash_combine (seed, b58.vchData);

    return seed;
}

} // ripple
