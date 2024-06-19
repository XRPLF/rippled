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
#include <boost/utility/string_view.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace ripple {

/** Format arbitrary binary data as an SQLite "blob literal".

    In SQLite, blob literals must be encoded when used in a query. Per
    https://sqlite.org/lang_expr.html#literal_values_constants_ they are
    encoded as string literals containing hexadecimal data and preceded
    by a single 'X' character.

    @param blob An arbitrary blob of binary data
    @return The input, encoded as a blob literal.
 */
std::string
sqlBlobLiteral(Blob const& blob);

template <class Iterator>
std::optional<Blob>
strUnHex(std::size_t strSize, Iterator begin, Iterator end)
{
    static constexpr std::array<int, 256> const unxtab = []() {
        std::array<int, 256> t{};

        for (auto& x : t)
            x = -1;

        for (int i = 0; i < 10; ++i)
            t['0' + i] = i;

        for (int i = 0; i < 6; ++i)
        {
            t['A' + i] = 10 + i;
            t['a' + i] = 10 + i;
        }

        return t;
    }();

    Blob out;

    out.reserve((strSize + 1) / 2);

    auto iter = begin;

    if (strSize & 1)
    {
        int c = unxtab[*iter++];

        if (c < 0)
            return {};

        out.push_back(c);
    }

    while (iter != end)
    {
        int cHigh = unxtab[*iter++];

        if (cHigh < 0)
            return {};

        int cLow = unxtab[*iter++];

        if (cLow < 0)
            return {};

        out.push_back(static_cast<unsigned char>((cHigh << 4) | cLow));
    }

    return {std::move(out)};
}

inline std::optional<Blob>
strUnHex(std::string const& strSrc)
{
    return strUnHex(strSrc.size(), strSrc.cbegin(), strSrc.cend());
}

inline std::optional<Blob>
strViewUnHex(std::string_view strSrc)
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
    std::optional<std::uint16_t> port;
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

std::optional<std::uint64_t>
to_uint64(std::string const& s);

/** Determines if the given string looks like a TOML-file hosting domain.

    Do not use this function to determine if a particular string is a valid
    domain, as this function may reject domains that are otherwise valid and
    doesn't check whether the TLD is valid.
 */
bool
isProperlyFormedTomlDomain(std::string_view domain);

}  // namespace ripple

#endif
