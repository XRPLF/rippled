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

#ifndef RIPPLE_BASICS_STRINGUTILITIES_H_INCLUDED
#define RIPPLE_BASICS_STRINGUTILITIES_H_INCLUDED

#include <ripple/basics/Blob.h>
#include <ripple/basics/strHex.h>

#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_view.hpp>
#include <sstream>
#include <string>

namespace ripple {
inline static std::string
sqlEscape(std::string const& strSrc)
{
    static boost::format f("X'%s'");
    return str(boost::format(f) % strHex(strSrc));
}

inline static std::string
sqlEscape(Blob const& vecSrc)
{
    size_t size = vecSrc.size();

    if (size == 0)
        return "X''";

    std::string j(size * 2 + 3, 0);

    unsigned char* oPtr = reinterpret_cast<unsigned char*>(&*j.begin());
    const unsigned char* iPtr = &vecSrc[0];

    *oPtr++ = 'X';
    *oPtr++ = '\'';

    for (int i = size; i != 0; --i)
    {
        unsigned char c = *iPtr++;
        *oPtr++ = charHex(c >> 4);
        *oPtr++ = charHex(c & 15);
    }

    *oPtr++ = '\'';
    return j;
}

uint64_t
uintFromHex(std::string const& strSrc);

template <class Iterator>
boost::optional<Blob>
strUnHex(std::size_t strSize, Iterator begin, Iterator end)
{
    Blob out;

    out.reserve((strSize + 1) / 2);

    auto iter = begin;

    if (strSize & 1)
    {
        int c = charUnHex(*iter);

        if (c < 0)
            return {};

        out.push_back(c);
        ++iter;
    }

    while (iter != end)
    {
        int cHigh = charUnHex(*iter);
        ++iter;

        if (cHigh < 0)
            return {};

        int cLow = charUnHex(*iter);
        ++iter;

        if (cLow < 0)
            return {};

        out.push_back(static_cast<unsigned char>((cHigh << 4) | cLow));
    }

    return {std::move(out)};
}

inline boost::optional<Blob>
strUnHex(std::string const& strSrc)
{
    return strUnHex(strSrc.size(), strSrc.cbegin(), strSrc.cend());
}

inline boost::optional<Blob>
strViewUnHex(boost::string_view const& strSrc)
{
    return strUnHex(strSrc.size(), strSrc.cbegin(), strSrc.cend());
}

struct parsedURL
{
    explicit parsedURL() = default;

    std::string scheme;
    std::string username;
    std::string password;
    std::string domain;
    boost::optional<std::uint16_t> port;
    std::string path;

    bool
    operator==(parsedURL const& other) const
    {
        return scheme == other.scheme && domain == other.domain &&
            port == other.port && path == other.path;
    }
};

bool
parseUrl(parsedURL& pUrl, std::string const& strUrl);

std::string
trim_whitespace(std::string str);

boost::optional<std::uint64_t>
to_uint64(std::string const& s);

}  // namespace ripple

#endif
