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
//
// Why base-58 instead of standard base-64 encoding?
// - Don't want 0OIl characters that look the same in some fonts and
//      could be used to create visually identical looking account numbers.
// - A string with non-alphanumeric characters is not as easily accepted as an account number.
// - E-mail usually won't line-break if there's no punctuation to break at.
// - Doubleclicking selects the whole number as one word if it's all alphanumeric.
//
#ifndef RIPPLE_BASE58_H
#define RIPPLE_BASE58_H

/** Performs Base 58 encoding and decoding.
*/
class Base58
{
public:
    // VFALCO TODO clean up this poor API
    static char const* getCurrentAlphabet ();
    static void setCurrentAlphabet (char const* alphabet);

    static char const* getBitcoinAlphabet ();
    static char const* getRippleAlphabet ();
    static char const* getTestnetAlphabet ();

    static std::string encode (const unsigned char* pbegin, const unsigned char* pend);
    static std::string encode (Blob const& vch);
    static std::string encodeWithCheck (Blob const& vchIn);

    static bool decode (const char* psz, Blob& vchRet, const char* pAlphabet = getCurrentAlphabet ());
    static bool decode (const std::string& str, Blob& vchRet);
    static bool decodeWithCheck (const char* psz, Blob& vchRet, const char* pAlphabet = getCurrentAlphabet ());
    static bool decodeWithCheck (const std::string& str, Blob& vchRet, const char* pAlphabet);

private:
    static char const* s_currentAlphabet;
};

#endif
// vim:ts=4
