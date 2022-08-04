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

#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/net/IPEndpoint.h>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <algorithm>

namespace ripple {

std::string
sqlBlobLiteral(Blob const& blob)
{
    std::string j;

    j.reserve(blob.size() * 2 + 3);
    j.push_back('X');
    j.push_back('\'');
    boost::algorithm::hex(blob.begin(), blob.end(), std::back_inserter(j));
    j.push_back('\'');

    return j;
}

bool
parseUrl(parsedURL& pUrl, std::string const& strUrl)
{
    // scheme://username:password@hostname:port/rest
    static boost::regex reUrl(
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
    try
    {
        if (!boost::regex_match(strUrl, smMatch, reUrl))
            return false;
    }
    catch (...)
    {
        return false;
    }

    pUrl.scheme = smMatch[1];
    boost::algorithm::to_lower(pUrl.scheme);
    pUrl.username = smMatch[2];
    pUrl.password = smMatch[3];
    const std::string domain = smMatch[4];
    // We need to use Endpoint to parse the domain to
    // strip surrounding brackets from IPv6 addresses,
    // e.g. [::1] => ::1.
    const auto result = beast::IP::Endpoint::from_string_checked(domain);
    pUrl.domain = result ? result->address().to_string() : domain;
    const std::string port = smMatch[5];
    if (!port.empty())
    {
        pUrl.port = beast::lexicalCast<std::uint16_t>(port);

        // For inputs larger than 2^32-1 (65535), lexicalCast returns 0.
        // parseUrl returns false for such inputs.
        if (pUrl.port == 0)
        {
            return false;
        }
    }
    pUrl.path = smMatch[6];

    return true;
}

std::string
trim_whitespace(std::string str)
{
    boost::trim(str);
    return str;
}

std::optional<std::uint64_t>
to_uint64(std::string const& s)
{
    std::uint64_t result;
    if (beast::lexicalCastChecked(result, s))
        return result;
    return std::nullopt;
}

bool
isProperlyFormedTomlDomain(std::string const& domain)
{
    // The domain must be between 4 and 128 characters long
    if (domain.size() < 4 || domain.size() > 128)
        return false;

    // This regular expression should do a decent job of weeding out
    // obviously wrong domain names but it isn't perfect. It does not
    // really support IDNs. If this turns out to be an issue, a more
    // thorough regex can be used or this check can just be removed.
    static boost::regex const re(
        "^"                   // Beginning of line
        "("                   // Beginning of a segment
        "(?!-)"               //  - must not begin with '-'
        "[a-zA-Z0-9-]{1,63}"  //  - only alphanumeric and '-'
        "(?<!-)"              //  - must not end with '-'
        "\\."                 // segment separator
        ")+"                  // 1 or more segments
        "[A-Za-z]{2,63}"      // TLD
        "$"                   // End of line
        ,
        boost::regex_constants::optimize);

    return boost::regex_match(domain, re);
}

}  // namespace ripple
