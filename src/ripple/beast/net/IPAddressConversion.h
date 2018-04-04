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

#ifndef BEAST_NET_IPADDRESSCONVERSION_H_INCLUDED
#define BEAST_NET_IPADDRESSCONVERSION_H_INCLUDED

#include <ripple/beast/net/IPEndpoint.h>

#include <sstream>

#include <boost/asio.hpp>

namespace beast {
namespace IP {

/** Convert to Endpoint.
    The port is set to zero.
*/
Endpoint from_asio (boost::asio::ip::address const& address);

/** Convert to Endpoint. */
Endpoint from_asio (boost::asio::ip::tcp::endpoint const& endpoint);

/** Convert to asio::ip::address.
    The port is ignored.
*/
boost::asio::ip::address to_asio_address (Endpoint const& endpoint);

/** Convert to asio::ip::tcp::endpoint. */
boost::asio::ip::tcp::endpoint to_asio_endpoint (Endpoint const& endpoint);

}
}

namespace beast {

// DEPRECATED
struct IPAddressConversion
{
    explicit IPAddressConversion() = default;

    static IP::Endpoint from_asio (boost::asio::ip::address const& address)
        { return IP::from_asio (address); }
    static IP::Endpoint from_asio (boost::asio::ip::tcp::endpoint const& endpoint)
        { return IP::from_asio (endpoint); }
    static boost::asio::ip::address to_asio_address (IP::Endpoint const& address)
        { return IP::to_asio_address (address); }
    static boost::asio::ip::tcp::endpoint to_asio_endpoint (IP::Endpoint const& address)
        { return IP::to_asio_endpoint (address); }
};

}

#endif
