//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <boost/function_output_iterator.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <functional>

namespace ripple {

/** The list of protocol versions we speak and we prefer to use.

    @note The list must be sorted in strictly ascending order (and so
          it may not contain any duplicates!)
*/

// clang-format off
constexpr ProtocolVersion const supportedProtocolList[]
{
    {2, 0},
    {2, 1},
    {2, 2}
};
// clang-format on

// This ugly construct ensures that supportedProtocolList is sorted in strictly
// ascending order and doesn't contain any duplicates.
// FIXME: With C++20 we can use std::is_sorted with an appropriate comparator
static_assert(
    []() constexpr->bool {
        auto const len = std::distance(
            std::begin(supportedProtocolList), std::end(supportedProtocolList));

        // There should be at least one protocol we're willing to speak.
        if (len == 0)
            return false;

        // A list with only one entry is, by definition, sorted so we don't
        // need to check it.
        if (len != 1)
        {
            for (auto i = 0; i != len - 1; ++i)
            {
                if (supportedProtocolList[i] >= supportedProtocolList[i + 1])
                    return false;
            }
        }

        return true;
    }(),
    "The list of supported protocols isn't properly sorted.");

std::string
to_string(ProtocolVersion const& p)
{
    return "XRPL/" + std::to_string(p.first) + "." + std::to_string(p.second);
}

std::vector<ProtocolVersion>
parseProtocolVersions(boost::beast::string_view const& value)
{
    static boost::regex re(
        "^"                        // start of line
        "XRPL/"                    // The string "XRPL/"
        "([2-9]|(?:[1-9][0-9]+))"  // a number (greater than 2 with no leading
                                   // zeroes)
        "\\."                      // a period
        "(0|(?:[1-9][0-9]*))"      // a number (no leading zeroes unless exactly
                                   // zero)
        "$"                        // The end of the string
        ,
        boost::regex_constants::optimize);

    std::vector<ProtocolVersion> result;

    for (auto const& s : beast::rfc2616::split_commas(value))
    {
        boost::smatch m;

        if (boost::regex_match(s, m, re))
        {
            std::uint16_t major;
            std::uint16_t minor;
            if (!beast::lexicalCastChecked(major, std::string(m[1])))
                continue;

            if (!beast::lexicalCastChecked(minor, std::string(m[2])))
                continue;

            auto const proto = make_protocol(major, minor);

            // This is an extra sanity check: we check that the protocol we just
            // decoded corresponds to the token we were parsing.
            if (to_string(proto) == s)
                result.push_back(make_protocol(major, minor));
        }
    }

    // We guarantee that the returned list is sorted and contains no duplicates:
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

std::optional<ProtocolVersion>
negotiateProtocolVersion(std::vector<ProtocolVersion> const& versions)
{
    std::optional<ProtocolVersion> result;

    // The protocol version we want to negotiate is the largest item in the
    // intersection of the versions supported by us and the peer. Since the
    // output of std::set_intersection is sorted, that item is always going
    // to be the last one. So we get a little clever and avoid the need for
    // a container:
    std::function<void(ProtocolVersion const&)> pickVersion =
        [&result](ProtocolVersion const& v) { result = v; };

    std::set_intersection(
        std::begin(versions),
        std::end(versions),
        std::begin(supportedProtocolList),
        std::end(supportedProtocolList),
        boost::make_function_output_iterator(pickVersion));

    return result;
}

std::optional<ProtocolVersion>
negotiateProtocolVersion(boost::beast::string_view const& versions)
{
    auto const them = parseProtocolVersions(versions);

    return negotiateProtocolVersion(them);
}

std::string const&
supportedProtocolVersions()
{
    static std::string const supported = []() {
        std::string ret;
        for (auto const& v : supportedProtocolList)
        {
            if (!ret.empty())
                ret += ", ";
            ret += to_string(v);
        }

        return ret;
    }();

    return supported;
}

bool
isProtocolSupported(ProtocolVersion const& v)
{
    return std::end(supportedProtocolList) !=
        std::find(
               std::begin(supportedProtocolList),
               std::end(supportedProtocolList),
               v);
}

}  // namespace ripple
