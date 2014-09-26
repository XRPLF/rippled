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

#ifndef RIPPLE_BUILDINFO_H_INCLUDED
#define RIPPLE_BUILDINFO_H_INCLUDED

namespace ripple {

/** Versioning information for this build. */
struct BuildInfo
{
public:
    /** Server version.

        Follows the Semantic Versioning Specification:

        http://semver.org/
    */
    static std::string const& getVersionString ();

    /** Full server version string.

        This includes the name of the server. It is used in the peer
        protocol hello message and also the headers of some HTTP replies.
    */
    static std::string const& getFullVersionString ();

    //--------------------------------------------------------------------------

    /** The wire protocol version.

        The version consists of two unsigned 16 bit integers representing
        major and minor version numbers. All values are permissible.
    */
    using Protocol = std::pair <std::uint16_t const, std::uint16_t const>;

    /** Construct a protocol version from a packed 32-bit protocol identifier */
    static
    BuildInfo::Protocol
    make_protocol (std::uint32_t version)
    {
        return BuildInfo::Protocol (
            static_cast<std::uint16_t> ((version >> 16) & 0xffff),
            static_cast<std::uint16_t> (version & 0xffff));
    }

    /** The protocol version we speak and prefer. */
    static Protocol const& getCurrentProtocol ();

    /** The oldest protocol version we will accept. */
    static Protocol const& getMinimumProtocol ();

    static char const* getRawVersionString ();
};

std::string
to_string (BuildInfo::Protocol const& p);

std::uint32_t
to_packed (BuildInfo::Protocol const& p);

} // ripple

#endif
