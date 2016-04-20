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

#include <ripple/beast/net/IPAddressConversion.h>

namespace beast {
namespace IP {

Endpoint from_asio (boost::asio::ip::address const& address)
{
    if (address.is_v4 ())
    {
        boost::asio::ip::address_v4::bytes_type const bytes (
            address.to_v4().to_bytes());
        return Endpoint (AddressV4 (
            bytes [0], bytes [1], bytes [2], bytes [3]));
    }

    // VFALCO TODO IPv6 support
    assert(false);
    return Endpoint();
}

Endpoint from_asio (boost::asio::ip::tcp::endpoint const& endpoint)
{
    return from_asio (endpoint.address()).at_port (endpoint.port());
}

boost::asio::ip::address to_asio_address (Endpoint const& endpoint)
{
    if (endpoint.address().is_v4())
    {
        return boost::asio::ip::address (
            boost::asio::ip::address_v4 (
                endpoint.address().to_v4().value));
    }

    // VFALCO TODO IPv6 support
    assert(false);
    return boost::asio::ip::address (
        boost::asio::ip::address_v6 ());
}

boost::asio::ip::tcp::endpoint to_asio_endpoint (Endpoint const& endpoint)
{
    return boost::asio::ip::tcp::endpoint (
        to_asio_address (endpoint), endpoint.port());
}

}
}
