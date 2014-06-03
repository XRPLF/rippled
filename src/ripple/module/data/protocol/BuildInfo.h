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
    /** Server version.

        Follows the Semantic Versioning Specification:

        http://semver.org/
    */
    static beast::String const& getVersionString ();

    /** Full server version string.

        This includes the name of the server. It is used in the peer
        protocol hello message and also the headers of some HTTP replies.
    */
    static char const* getFullVersionString ();

    //--------------------------------------------------------------------------

    /** The wire protocol version.

        The version consists of two unsigned 16 bit integers representing
        major and minor version numbers. All values are permissible.
    */
    struct Protocol
    {
        unsigned short vmajor;
        unsigned short vminor;

        //----

        /** The serialized format of the protocol version. */
        typedef std::uint32_t PackedFormat;

        Protocol ();
        Protocol (unsigned short vmajor, unsigned short vminor);
        explicit Protocol (PackedFormat packedVersion);

        PackedFormat toPacked () const noexcept;

        std::string toStdString () const noexcept;

        bool operator== (Protocol const& other) const noexcept { return toPacked () == other.toPacked (); }
        bool operator!= (Protocol const& other) const noexcept { return toPacked () != other.toPacked (); }
        bool operator>= (Protocol const& other) const noexcept { return toPacked () >= other.toPacked (); }
        bool operator<= (Protocol const& other) const noexcept { return toPacked () <= other.toPacked (); }
        bool operator>  (Protocol const& other) const noexcept { return toPacked () >  other.toPacked (); }
        bool operator<  (Protocol const& other) const noexcept { return toPacked () <  other.toPacked (); }
    };

    /** The protocol version we speak and prefer. */
    static Protocol const& getCurrentProtocol ();

    /** The oldest protocol version we will accept. */
    static Protocol const& getMinimumProtocol ();

    static char const* getRawVersionString ();
};

} // ripple

#endif
