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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/unit_test/suite.h>
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <cstdarg>

namespace ripple {

// NIKB NOTE: This function is only used by strUnHex (std::string const& strSrc)
// which results in a pointless copy from std::string into std::vector. Should
// we just scrap this function altogether?
int strUnHex (std::string& strDst, std::string const& strSrc)
{
    std::string tmp;

    tmp.reserve ((strSrc.size () + 1) / 2);

    auto iter = strSrc.cbegin ();

    if (strSrc.size () & 1)
    {
        int c = charUnHex (*iter);

        if (c < 0)
            return -1;

        tmp.push_back(c);
        ++iter;
    }

    while (iter != strSrc.cend ())
    {
        int cHigh = charUnHex (*iter);
        ++iter;

        if (cHigh < 0)
            return -1;

        int cLow = charUnHex (*iter);
        ++iter;

        if (cLow < 0)
            return -1;

        tmp.push_back (static_cast<char>((cHigh << 4) | cLow));
    }

    strDst = std::move(tmp);

    return strDst.size ();
}

std::pair<Blob, bool> strUnHex (std::string const& strSrc)
{
    std::string strTmp;

    if (strUnHex (strTmp, strSrc) == -1)
        return std::make_pair (Blob (), false);

    return std::make_pair(strCopy (strTmp), true);
}

uint64_t uintFromHex (std::string const& strSrc)
{
    uint64_t uValue (0);

    if (strSrc.size () > 16)
        Throw<std::invalid_argument> ("overlong 64-bit value");

    for (auto c : strSrc)
    {
        int ret = charUnHex (c);

        if (ret == -1)
            Throw<std::invalid_argument> ("invalid hex digit");

        uValue = (uValue << 4) | ret;
    }

    return uValue;
}

//
// Misc string
//

Blob strCopy (std::string const& strSrc)
{
    Blob vucDst;

    vucDst.resize (strSrc.size ());

    std::copy (strSrc.begin (), strSrc.end (), vucDst.begin ());

    return vucDst;
}

std::string strCopy (Blob const& vucSrc)
{
    std::string strDst;

    strDst.resize (vucSrc.size ());

    std::copy (vucSrc.begin (), vucSrc.end (), strDst.begin ());

    return strDst;

}

//
// IP Port parsing
//
// <-- iPort: "" = -1
// VFALCO TODO Make this not require boost... and especially boost::asio
bool parseIpPort (std::string const& strSource, std::string& strIP, int& iPort)
{
    boost::smatch   smMatch;
    bool            bValid  = false;

    static boost::regex reEndpoint ("\\`\\s*(\\S+)(?:\\s+(\\d+))?\\s*\\'");

    if (boost::regex_match (strSource, smMatch, reEndpoint))
    {
        boost::system::error_code   err;
        std::string                 strIPRaw    = smMatch[1];
        std::string                 strPortRaw  = smMatch[2];

        boost::asio::ip::address    addrIP      = boost::asio::ip::address::from_string (strIPRaw, err);

        bValid  = !err;

        if (bValid)
        {
            strIP   = addrIP.to_string ();
            iPort   = strPortRaw.empty () ? -1 : beast::lexicalCastThrow <int> (strPortRaw);
        }
    }

    return bValid;
}

// TODO Callers should be using beast::URL and beast::parse_URL instead.
bool parseUrl (std::string const& strUrl, std::string& strScheme, std::string& strDomain, int& iPort, std::string& strPath)
{
    // scheme://username:password@hostname:port/rest
    static boost::regex reUrl ("(?i)\\`\\s*([[:alpha:]][-+.[:alpha:][:digit:]]*)://([^:/]+)(?::(\\d+))?(/.*)?\\s*?\\'");
    boost::smatch   smMatch;

    bool    bMatch  = boost::regex_match (strUrl, smMatch, reUrl);          // Match status code.

    if (bMatch)
    {
        std::string strPort;

        strScheme   = smMatch[1];
        strDomain   = smMatch[2];
        strPort     = smMatch[3];
        strPath     = smMatch[4];

        boost::algorithm::to_lower (strScheme);

        iPort   = strPort.empty () ? -1 : beast::lexicalCast <int> (strPort);
    }

    return bMatch;
}
} // ripple
