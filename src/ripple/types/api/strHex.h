//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_TYPES_STRHEX_H_INCLUDED
#define RIPPLE_TYPES_STRHEX_H_INCLUDED

namespace ripple {

char charHex (int iDigit);

template<class Iterator>
std::string strHex (Iterator first, int iSize)
{
    std::string     strDst;

    strDst.resize (iSize * 2);

    for (int i = 0; i < iSize; i++)
    {
        unsigned char c = *first++;

        strDst[i * 2]     = charHex (c >> 4);
        strDst[i * 2 + 1]   = charHex (c & 15);
    }

    return strDst;
}

}

#endif
