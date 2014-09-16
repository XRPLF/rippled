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

#ifndef RIPPLE_TYPES_STRHEX_H_INCLUDED
#define RIPPLE_TYPES_STRHEX_H_INCLUDED

namespace ripple {

/** Converts an integer to the corresponding hex digit 
    @param iDigit 0-15 inclusive
    @return a character from '0'-'9' or 'A'-'F' on success; 0 on failure.
*/
char charHex (int iDigit);

/** Converts a hex digit to the corresponding integer
    @param cDigit one of '0'-'9', 'A'-'F' or 'a'-'f'
    @return an integer from 0 to 15 on success; -1 on failure.
*/
int charUnHex (char cDigit);

// NIKB TODO cleanup this function and reduce the need for the many overloads
//           it has in various places.
template<class Iterator>
std::string strHex (Iterator first, int iSize)
{
    std::string strDst;

    strDst.resize (iSize * 2);

    for (int i = 0; i < iSize; i++)
    {
        unsigned char c = *first++;

        strDst[i * 2]     = charHex (c >> 4);
        strDst[i * 2 + 1] = charHex (c & 15);
    }

    return strDst;
}

}

#endif
