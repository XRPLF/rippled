#pragma once

#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio.hpp>

namespace beast::IP {

/** Convert to Endpoint.
    The port is set to zero.
*/
Endpoint
fromAsio(boost::asio::ip::address const& address);

/** Convert to Endpoint. */
Endpoint
fromAsio(boost::asio::ip::tcp::endpoint const& endpoint);

/** Convert to asio::ip::address.
    The port is ignored.
*/
boost::asio::ip::address
toAsioAddress(Endpoint const& endpoint);

/** Convert to asio::ip::tcp::endpoint. */
boost::asio::ip::tcp::endpoint
toAsioEndpoint(Endpoint const& endpoint);

}  // namespace beast::IP

namespace beast {

// DEPRECATED
struct IPAddressConversion
{
    explicit IPAddressConversion() = default;

    static IP::Endpoint
    fromAsio(boost::asio::ip::address const& address)
    {
        return IP::fromAsio(address);
    }
    static IP::Endpoint
    fromAsio(boost::asio::ip::tcp::endpoint const& endpoint)
    {
        return IP::fromAsio(endpoint);
    }
    static boost::asio::ip::address
    toAsioAddress(IP::Endpoint const& address)
    {
        return IP::toAsioAddress(address);
    }
    static boost::asio::ip::tcp::endpoint
    toAsioEndpoint(IP::Endpoint const& address)
    {
        return IP::toAsioEndpoint(address);
    }
};

}  // namespace beast
