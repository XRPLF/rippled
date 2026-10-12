#pragma once

#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio.hpp>

namespace beast::ip {

/**
 * Convert to Endpoint.
 * The port is set to zero.
 */
Endpoint
fromAsio(boost::asio::ip::address const& address);

/**
 * Convert to Endpoint.
 */
Endpoint
fromAsio(boost::asio::ip::tcp::endpoint const& endpoint);

/**
 * Convert to asio::ip::address.
 * The port is ignored.
 */
boost::asio::ip::address
toAsioAddress(Endpoint const& endpoint);

/**
 * Convert to asio::ip::tcp::endpoint.
 */
boost::asio::ip::tcp::endpoint
toAsioEndpoint(Endpoint const& endpoint);

}  // namespace beast::ip

namespace beast {

// DEPRECATED
struct IPAddressConversion
{
    explicit IPAddressConversion() = default;

    static ip::Endpoint
    fromAsio(boost::asio::ip::address const& address)
    {
        return ip::fromAsio(address);
    }
    static ip::Endpoint
    fromAsio(boost::asio::ip::tcp::endpoint const& endpoint)
    {
        return ip::fromAsio(endpoint);
    }
    static boost::asio::ip::address
    toAsioAddress(ip::Endpoint const& address)
    {
        return ip::toAsioAddress(address);
    }
    static boost::asio::ip::tcp::endpoint
    toAsioEndpoint(ip::Endpoint const& address)
    {
        return ip::toAsioEndpoint(address);
    }
};

}  // namespace beast
