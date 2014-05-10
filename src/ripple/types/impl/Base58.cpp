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

namespace ripple {

void Base58::fourbyte_hash256 (void* out, void const* in, std::size_t bytes)
{
    unsigned char const* const p (
        static_cast <unsigned char const*>(in));
    uint256 hash (SHA256Hash (p, p + bytes));
    memcpy (out, hash.begin(), 4);
}

Base58::Alphabet const& Base58::getBitcoinAlphabet ()
{
    static Alphabet alphabet (
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
        );
    return alphabet;
}

Base58::Alphabet const& Base58::getRippleAlphabet ()
{
    static Alphabet alphabet (
        "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz"
        );
    return alphabet;
}

std::string Base58::raw_encode (
    unsigned char const* begin, unsigned char const* end,
        Alphabet const& alphabet, bool withCheck)
{
    CAutoBN_CTX pctx;
    CBigNum bn58 = 58;
    CBigNum bn0 = 0;

    // Convert little endian data to bignum
    CBigNum bn (begin, end);
    std::size_t const size (std::distance (begin, end));

    // Convert bignum to std::string
    std::string str;
    // Expected size increase from base58 conversion is approximately 137%
    // use 138% to be safe
    str.reserve (size * 138 / 100 + 1);
    CBigNum dv;
    CBigNum rem;

    while (bn > bn0)
    {
        if (!BN_div (&dv, &rem, &bn, &bn58, pctx))
            throw bignum_error ("EncodeBase58 : BN_div failed");

        bn = dv;
        unsigned int c = rem.getuint ();
        str += alphabet [c];
    }
    
    for (const unsigned char* p = end-2; p >= begin && *p == 0; p--)
        str += alphabet [0];

    // Convert little endian std::string to big endian
    reverse (str.begin (), str.end ());
    return str;
}

//------------------------------------------------------------------------------

bool Base58::raw_decode (char const* first, char const* last, void* dest,
    std::size_t size, bool checked, Alphabet const& alphabet)
{
    CAutoBN_CTX pctx;
    CBigNum bn58 = 58;
    CBigNum bn = 0;
    CBigNum bnChar;

    // Convert big endian string to bignum
    for (char const* p = first; p != last; ++p)
    {
        int i (alphabet.from_char (*p));
        if (i == -1)
            return false;
        bnChar.setuint ((unsigned int) i);

        int const success (BN_mul (&bn, &bn, &bn58, pctx));
        assert (success);

        bn += bnChar;
    }

    // Get bignum as little endian data
    Blob vchTmp = bn.getvch ();

    // Trim off sign byte if present
    if (vchTmp.size () >= 2 && vchTmp.end ()[-1] == 0 && vchTmp.end ()[-2] >= 0x80)
        vchTmp.erase (vchTmp.end () - 1);

    char* const out (static_cast <char*> (dest));

    // Count leading zeros
    int nLeadingZeros = 0;
    for (char const* p = first; p!=last && *p==alphabet[0]; p++)
        nLeadingZeros++;

    // Verify that the size is correct
    if (vchTmp.size() + nLeadingZeros != size)
        return false;

    // Fill the leading zeros
    memset (out, 0, nLeadingZeros);

    // Copy little endian data to big endian
    std::reverse_copy (vchTmp.begin (), vchTmp.end (),
        out + nLeadingZeros);

    if (checked)
    {
        char hash4 [4];
        fourbyte_hash256 (hash4, out, size - 4);
        if (memcmp (hash4, out + size - 4, 4) != 0)
            return false;
    }

    return true;
}

bool Base58::decode (const char* psz, Blob& vchRet, Alphabet const& alphabet)
{
    CAutoBN_CTX pctx;
    vchRet.clear ();
    CBigNum bn58 = 58;
    CBigNum bn = 0;
    CBigNum bnChar;

    while (isspace (*psz))
        psz++;

    // Convert big endian string to bignum
    for (const char* p = psz; *p; p++)
    {
        // VFALCO TODO Make this use the inverse table!
        //             Or better yet ditch this and call raw_decode
        //
        const char* p1 = strchr (alphabet.chars(), *p);

        if (p1 == nullptr)
        {
            while (isspace (*p))
                p++;

            if (*p != '\0')
                return false;

            break;
        }

        bnChar.setuint (p1 - alphabet.chars());

        if (!BN_mul (&bn, &bn, &bn58, pctx))
            throw bignum_error ("DecodeBase58 : BN_mul failed");

        bn += bnChar;
    }

    // Get bignum as little endian data
    Blob vchTmp = bn.getvch ();

    // Trim off sign byte if present
    if (vchTmp.size () >= 2 && vchTmp.end ()[-1] == 0 && vchTmp.end ()[-2] >= 0x80)
        vchTmp.erase (vchTmp.end () - 1);

    // Restore leading zeros
    int nLeadingZeros = 0;

    for (const char* p = psz; *p == alphabet.chars()[0]; p++)
        nLeadingZeros++;

    vchRet.assign (nLeadingZeros + vchTmp.size (), 0);

    // Convert little endian data to big endian
    std::reverse_copy (vchTmp.begin (), vchTmp.end (), vchRet.end () - vchTmp.size ());
    return true;
}

bool Base58::decode (const std::string& str, Blob& vchRet)
{
    return decode (str.c_str (), vchRet);
}

bool Base58::decodeWithCheck (const char* psz, Blob& vchRet, Alphabet const& alphabet)
{
    if (!decode (psz, vchRet, alphabet))
        return false;

    if (vchRet.size () < 4)
    {
        vchRet.clear ();
        return false;
    }

    uint256 hash = SHA256Hash (vchRet.begin (), vchRet.end () - 4);

    if (memcmp (&hash, &vchRet.end ()[-4], 4) != 0)
    {
        vchRet.clear ();
        return false;
    }

    vchRet.resize (vchRet.size () - 4);
    return true;
}

bool Base58::decodeWithCheck (const std::string& str, Blob& vchRet, Alphabet const& alphabet)
{
    return decodeWithCheck (str.c_str (), vchRet, alphabet);
}

}

