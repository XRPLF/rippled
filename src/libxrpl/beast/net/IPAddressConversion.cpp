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
