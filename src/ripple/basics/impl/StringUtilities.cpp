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

#include <ripple/basics/contract.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <ripple/beast/core/LexicalCast.h>
#include <boost/algorithm/string.hpp>
#include <ripple/beast/net/IPEndpoint.h>
#include <boost/regex.hpp>
#include <algorithm>
#include <cstdarg>

namespace ripple {

boost::optional<Blob> strUnHex (std::string const& strSrc)
{
    Blob out;

    out.reserve ((strSrc.size () + 1) / 2);

    auto iter = strSrc.cbegin ();

    if (strSrc.size () & 1)
    {
        int c = charUnHex (*iter);

        if (c < 0)
            return {};

        out.push_back(c);
        ++iter;
    }

    while (iter != strSrc.cend ())
    {
        int cHigh = charUnHex (*iter);
        ++iter;

        if (cHigh < 0)
            return {};

        int cLow = charUnHex (*iter);
        ++iter;

        if (cLow < 0)
            return {};

        out.push_back (static_cast<unsigned char>((cHigh << 4) | cLow));
    }

    return {std::move(out)};
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

bool parseUrl (parsedURL& pUrl, std::string const& strUrl)
{
    // scheme://username:password@hostname:port/rest
    static boost::regex reUrl (
        "(?i)\\`\\s*"
        // required scheme
        "([[:alpha:]][-+.[:alpha:][:digit:]]*?):"
        // We choose to support only URIs whose `hier-part` has the form
        // `"//" authority path-abempty`.
        "//"
        // optional userinfo
        "(?:([^:@/]*?)(?::([^@/]*?))?@)?"
        // optional host
        "([[:digit:]:]*[[:digit:]]|\\[[^]]+\\]|[^:/?#]*?)"
        // optional port
        "(?::([[:digit:]]+))?"
        // optional path
        "(/.*)?"
        "\\s*?\\'");
    boost::smatch smMatch;

    // Bail if there is no match.
    try {
        if (! boost::regex_match (strUrl, smMatch, reUrl))
            return false;
    } catch (...) {
        return false;
    }

    pUrl.scheme = smMatch[1];
    boost::algorithm::to_lower (pUrl.scheme);
    pUrl.username = smMatch[2];
    pUrl.password = smMatch[3];
    const std::string domain = smMatch[4];
    // We need to use Endpoint to parse the domain to
    // strip surrounding brackets from IPv6 addresses,
    // e.g. [::1] => ::1.
    const auto result = beast::IP::Endpoint::from_string_checked (domain);
    pUrl.domain = result
        ? result->address().to_string()
        : domain;
    const std::string port = smMatch[5];
    if (!port.empty())
    {
        pUrl.port = beast::lexicalCast <std::uint16_t> (port);
    }
    pUrl.path = smMatch[6];

    return true;
}

std::string trim_whitespace (std::string str)
{
    boost::trim (str);
    return str;
}

boost::optional<std::uint64_t>
to_uint64(std::string const& s)
{
    std::uint64_t result;
    if (beast::lexicalCastChecked (result, s))
        return result;
    return boost::none;
}

} // ripple
