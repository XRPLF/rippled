//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
#ifndef RIPPLE_TYPES_BASE58_H
#define RIPPLE_TYPES_BASE58_H

#include "Blob.h"

namespace ripple {

/** Performs Base 58 encoding and decoding. */
class Base58
{
public:
    // VFALCO TODO clean up this poor API
    static char const* getCurrentAlphabet ();
    static void setCurrentAlphabet (char const* alphabet);

    static char const* getBitcoinAlphabet ();
    static char const* getRippleAlphabet ();
    static char const* getTestnetAlphabet ();

    static std::string encode (
        const unsigned char* pbegin,
        const unsigned char* pend,
        char const* alphabet,
        bool withCheck);

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

}

#endif
