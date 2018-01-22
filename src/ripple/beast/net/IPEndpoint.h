//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NET_IPENDPOINT_H_INCLUDED
#define BEAST_NET_IPENDPOINT_H_INCLUDED

#include <ripple/beast/net/IPAddress.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <cstdint>
#include <ios>
#include <string>

namespace beast {
namespace IP {

using Port = std::uint16_t;

/** A version-independent IP address and port combination. */
class Endpoint
{
public:
    /** Create an unspecified endpoint. */
    Endpoint ();

    /** Create an endpoint from the address and optional port. */
    explicit Endpoint (Address const& addr, Port port = 0);

    /** Create an Endpoint from a string.
        If the port is omitted, the endpoint will have a zero port.
        @return A pair with the endpoint, and bool set to `true` on success.
    */
    static std::pair <Endpoint, bool> from_string_checked (std::string const& s);
    static Endpoint from_string (std::string const& s);
    static Endpoint from_string_altform (std::string const& s);

    /** Returns a string representing the endpoint. */
    std::string to_string () const;

    /** Returns the port number on the endpoint. */
    Port port () const
        { return m_port; }

    /** Returns a new Endpoint with a different port. */
    Endpoint at_port (Port port) const
        { return Endpoint (m_addr, port); }

    /** Returns the address portion of this endpoint. */
    Address const& address () const
        { return m_addr; }

    /** Convenience accessors for the address part. */
    /** @{ */
    bool is_v4 () const
        { return m_addr.is_v4(); }
    bool is_v6 () const
        { return m_addr.is_v6(); }
    AddressV4 const& to_v4 () const
        { return m_addr.to_v4 (); }
    AddressV6 const& to_v6 () const
        { return m_addr.to_v6 (); }
    /** @} */

    /** Arithmetic comparison. */
    /** @{ */
    friend bool operator== (Endpoint const& lhs, Endpoint const& rhs);
    friend bool operator<  (Endpoint const& lhs, Endpoint const& rhs);

    friend bool operator!= (Endpoint const& lhs, Endpoint const& rhs)
        { return ! (lhs == rhs); }
    friend bool operator>  (Endpoint const& lhs, Endpoint const& rhs)
        { return rhs < lhs; }
    friend bool operator<= (Endpoint const& lhs, Endpoint const& rhs)
        { return ! (lhs > rhs); }
    friend bool operator>= (Endpoint const& lhs, Endpoint const& rhs)
        { return ! (rhs > lhs); }
    /** @} */

    template <class Hasher>
    friend
    void
    hash_append (Hasher& h, Endpoint const& endpoint)
    {
        using ::beast::hash_append;
        hash_append(h, endpoint.m_addr, endpoint.m_port);
    }

private:
    Address m_addr;
    Port m_port;
};

//------------------------------------------------------------------------------

// Properties

/** Returns `true` if the endpoint is a loopback address. */
inline bool  is_loopback (Endpoint const& endpoint)
    { return is_loopback (endpoint.address ()); }

/** Returns `true` if the endpoint is unspecified. */
inline bool  is_unspecified (Endpoint const& endpoint)
    { return is_unspecified (endpoint.address ()); }

/** Returns `true` if the endpoint is a multicast address. */
inline bool  is_multicast (Endpoint const& endpoint)
    { return is_multicast (endpoint.address ()); }

/** Returns `true` if the endpoint is a private unroutable address. */
inline bool  is_private (Endpoint const& endpoint)
    { return is_private (endpoint.address ()); }

/** Returns `true` if the endpoint is a public routable address. */
inline bool  is_public (Endpoint const& endpoint)
    { return is_public (endpoint.address ()); }

//------------------------------------------------------------------------------

/** Returns the endpoint represented as a string. */
inline std::string to_string (Endpoint const& endpoint)
    { return endpoint.to_string(); }

/** Output stream conversion. */
template <typename OutputStream>
OutputStream& operator<< (OutputStream& os, Endpoint const& endpoint)
{
    os << to_string (endpoint);
    return os;
}

/** Input stream conversion. */
std::istream& operator>> (std::istream& is, Endpoint& endpoint);

}
}

//------------------------------------------------------------------------------

namespace std {
/** std::hash support. */
template <>
struct hash <::beast::IP::Endpoint>
{
    std::size_t operator() (::beast::IP::Endpoint const& endpoint) const
        { return ::beast::uhash<>{} (endpoint); }
};
}

namespace boost {
/** boost::hash support. */
template <>
struct hash <::beast::IP::Endpoint>
{
    std::size_t operator() (::beast::IP::Endpoint const& endpoint) const
        { return ::beast::uhash<>{} (endpoint); }
};
}

#endif
