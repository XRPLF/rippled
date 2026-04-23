#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast {
namespace IP {

Endpoint
from_asio(boost::asio::ip::address const& address)
{
    return Endpoint{address};
}

Endpoint
from_asio(boost::asio::ip::tcp::endpoint const& endpoint)
{
    return Endpoint{endpoint.address(), endpoint.port()};
}

boost::asio::ip::address
to_asio_address(Endpoint const& endpoint)
{
    return endpoint.address();
}

boost::asio::ip::tcp::endpoint
to_asio_endpoint(Endpoint const& endpoint)
{
    return boost::asio::ip::tcp::endpoint{endpoint.address(), endpoint.port()};
}

}  // namespace IP
}  // namespace beast
