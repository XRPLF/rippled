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

#ifndef RIPPLE_OVERLAY_PROTOCOLVERSION_H_INCLUDED
#define RIPPLE_OVERLAY_PROTOCOLVERSION_H_INCLUDED

#include <boost/beast/core/string.hpp>
#include <boost/optional.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

/** Represents a particular version of the peer-to-peer protocol.

    The protocol is represented as two pairs of 16-bit integers; a major
    and a minor.
 * */
using ProtocolVersion = std::pair<std::uint16_t, std::uint16_t>;

inline constexpr ProtocolVersion
make_protocol(std::uint16_t major, std::uint16_t minor)
{
    return {major, minor};
}

/** Print a protocol version a human-readable string. */
std::string
to_string(ProtocolVersion const& p);

/** Parse a set of protocol versions.

    Given a comma-separated string, extract and return all those that look
    like valid protocol versions (i.e. RTXP/1.2 and XRPL/2.0 and later). Any
    strings that are not parseable as valid protocol strings are excluded
    from the result set.

    @return A list of all apparently valid protocol versions.

    @note The returned list of protocol versions is guaranteed to contain
          no duplicates and will be sorted in ascending protocol order.
*/
std::vector<ProtocolVersion>
parseProtocolVersions(boost::beast::string_view const& s);

/** Given a list of supported protocol versions, choose the one we prefer. */
boost::optional<ProtocolVersion>
negotiateProtocolVersion(std::vector<ProtocolVersion> const& versions);

/** Given a list of supported protocol versions, choose the one we prefer. */
boost::optional<ProtocolVersion>
negotiateProtocolVersion(boost::beast::string_view const& versions);

/** The list of all the protocol versions we support. */
std::string const&
supportedProtocolVersions();

/** Determine whether we support a specific protocol version. */
bool
isProtocolSupported(ProtocolVersion const& v);

}  // namespace ripple

#endif
