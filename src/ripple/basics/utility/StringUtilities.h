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

#ifndef RIPPLE_STRINGUTILITIES_H
#define RIPPLE_STRINGUTILITIES_H

#include <ripple/types/api/Blob.h>
#include <ripple/types/api/ByteOrder.h>
#include <ripple/types/api/strHex.h>
#include <beast/module/core/text/StringPairArray.h>
#include <string>

namespace ripple {

// String utility functions.

// Ripple specific constant used for parsing qualities and other things
//
// VFALCO NOTE This does not belong here!
// NIKB TODO Why is this here instead of somewhere more sensible? What
// "other things" is this being used for?
#define QUALITY_ONE         1000000000  // 10e9

//------------------------------------------------------------------------------
extern std::string urlEncode (std::string const& strSrc);

// NIKB TODO remove this function - it's only used for some logging in the UNL
//           code which can be trivially rewritten.
template<class Iterator>
std::string strJoin (Iterator first, Iterator last, std::string strSeperator)
{
    std::ostringstream  ossValues;

    for (Iterator start = first; first != last; first++)
    {
        ossValues << str (boost::format ("%s%s") % (start == first ? "" : strSeperator) % *first);
    }

    return ossValues.str ();
}

// NIKB TODO Remove the need for all these overloads. Move them out of here.
inline const std::string strHex (std::string const& strSrc)
{
    return strHex (strSrc.begin (), strSrc.size ());
}

inline std::string strHex (Blob const& vucData)
{
    return strHex (vucData.begin (), vucData.size ());
}

inline std::string strHex (const std::uint64_t uiHost)
{
    uint64_t    uBig    = htobe64 (uiHost);

    return strHex ((unsigned char*) &uBig, sizeof (uBig));
}

inline static std::string sqlEscape (std::string const& strSrc)
{
    static boost::format f ("X'%s'");
    return str (boost::format (f) % strHex (strSrc));
}

inline static std::string sqlEscape (Blob const& vecSrc)
{
    size_t size = vecSrc.size ();

    if (size == 0)
        return "X''";

    std::string j (size * 2 + 3, 0);

    unsigned char* oPtr = reinterpret_cast<unsigned char*> (&*j.begin ());
    const unsigned char* iPtr = &vecSrc[0];

    *oPtr++ = 'X';
    *oPtr++ = '\'';

    for (int i = size; i != 0; --i)
    {
        unsigned char c = *iPtr++;
        *oPtr++ = charHex (c >> 4);
        *oPtr++ = charHex (c & 15);
    }

    *oPtr++ = '\'';
    return j;
}

int strUnHex (std::string& strDst, std::string const& strSrc);

uint64_t uintFromHex (std::string const& strSrc);

std::pair<Blob, bool> strUnHex (std::string const& strSrc);

Blob strCopy (std::string const& strSrc);
std::string strCopy (Blob const& vucSrc);

bool parseIpPort (std::string const& strSource, std::string& strIP, int& iPort);

bool parseUrl (std::string const& strUrl, std::string& strScheme,
               std::string& strDomain, int& iPort, std::string& strPath);

/** Create a Parameters from a String.

    Parameter strings have the format:

    <key>=<value>['|'<key>=<value>]
*/
extern beast::StringPairArray
parseDelimitedKeyValueString (
    beast::String s, beast::beast_wchar delimiter='|');

} // ripple

#endif
