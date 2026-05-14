/** @file
 *  Translation boundary between Boost.Asio networking types and
 *  `beast::IP::Endpoint`.
 *
 *  Each function in this file is a single-expression body; the implementation
 *  is intentionally placed here rather than inline in the header so that
 *  Boost.Asio headers (`boost/asio/ip/address.hpp`, `boost/asio/ip/tcp.hpp`)
 *  are not transitively included by code that only needs `IPEndpoint.h`.
 *  No validation or policy enforcement is performed — conversion is purely
 *  mechanical.
 *
 *  @see beast::IP::fromAsio(boost::asio::ip::address const&)
 *  @see beast::IP::toAsioEndpoint(Endpoint const&)
 */

#include <xrpl/beast/net/IPAddressConversion.h>

#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast::IP {

Endpoint
fromAsio(boost::asio::ip::address const& address)
{
    return Endpoint{address};
}

Endpoint
fromAsio(boost::asio::ip::tcp::endpoint const& endpoint)
{
    return Endpoint{endpoint.address(), endpoint.port()};
}

boost::asio::ip::address
toAsioAddress(Endpoint const& endpoint)
{
    return endpoint.address();
}

boost::asio::ip::tcp::endpoint
toAsioEndpoint(Endpoint const& endpoint)
{
    return boost::asio::ip::tcp::endpoint{endpoint.address(), endpoint.port()};
}

}  // namespace beast::IP
