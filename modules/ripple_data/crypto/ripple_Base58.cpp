//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with  or without fee is hereby granted,  provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
    MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

char const* Base58::s_currentAlphabet = Base58::getRippleAlphabet ();

char const* Base58::getCurrentAlphabet ()
{
    return s_currentAlphabet;
}

void Base58::setCurrentAlphabet (char const* alphabet)
{
    s_currentAlphabet = alphabet;
}

char const* Base58::getBitcoinAlphabet ()
{
    return "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
}

char const* Base58::getRippleAlphabet ()
{
    return "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";
}

char const* Base58::getTestnetAlphabet ()
{
    return "RPShNAF39wBUDnEGHJKLM4pQrsT7VWXYZ2bcdeCg65jkm8ofqi1tuvaxyz";
}

std::string Base58::encode (const unsigned char* pbegin, const unsigned char* pend)
{
    char const* alphabet = getCurrentAlphabet ();

    CAutoBN_CTX pctx;
    CBigNum bn58 = 58;
    CBigNum bn0 = 0;

    // Convert big endian data to little endian
    // Extra zero at the end make sure bignum will interpret as a positive number
    Blob vchTmp (pend - pbegin + 1, 0);
    std::reverse_copy (pbegin, pend, vchTmp.begin ());

    // Convert little endian data to bignum
    CBigNum bn (vchTmp);

    // Convert bignum to std::string
    std::string str;
    // Expected size increase from base58 conversion is approximately 137%
    // use 138% to be safe
    str.reserve ((pend - pbegin) * 138 / 100 + 1);
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

    // Leading zeroes encoded as base58 zeros
    for (const unsigned char* p = pbegin; p < pend && *p == 0; p++)
        str += alphabet [0];

    // Convert little endian std::string to big endian
    reverse (str.begin (), str.end ());
    return str;
}

std::string Base58::encode (Blob const& vch)
{
    return encode (&vch[0], &vch[0] + vch.size ());
}

std::string Base58::encodeWithCheck (Blob const& vchIn)
{
    // add 4-byte hash check to the end
    Blob vch (vchIn);
    uint256 hash = SHA256Hash (vch.begin (), vch.end ());
    vch.insert (vch.end (), (unsigned char*)&hash, (unsigned char*)&hash + 4);
    return encode (vch);
}

bool Base58::decode (const char* psz, Blob& vchRet, const char* pAlpha)
{
    assert (pAlpha != 0);

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
        const char* p1 = strchr (pAlpha, *p);

        if (p1 == NULL)
        {
            while (isspace (*p))
                p++;

            if (*p != '\0')
                return false;

            break;
        }

        bnChar.setuint (p1 - pAlpha);

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

    for (const char* p = psz; *p == pAlpha[0]; p++)
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

bool Base58::decodeWithCheck (const char* psz, Blob& vchRet, const char* pAlphabet)
{
    assert (pAlphabet != NULL);

    if (!decode (psz, vchRet, pAlphabet))
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

bool Base58::decodeWithCheck (const std::string& str, Blob& vchRet, const char* pAlphabet)
{
    return decodeWithCheck (str.c_str (), vchRet, pAlphabet);
}

// vim:ts=4
