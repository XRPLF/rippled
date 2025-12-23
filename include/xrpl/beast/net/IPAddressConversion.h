#ifndef BEAST_NET_IPADDRESSCONVERSION_H_INCLUDED
#define BEAST_NET_IPADDRESSCONVERSION_H_INCLUDED

#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio.hpp>

namespace beast {
namespace IP {

/** Convert to Endpoint.
    The port is set to zero.
*/
Endpoint
from_asio(boost::asio::ip::address const& address);

/** Convert to Endpoint. */
Endpoint
from_asio(boost::asio::ip::tcp::endpoint const& endpoint);

/** Convert to asio::ip::address.
    The port is ignored.
*/
boost::asio::ip::address
to_asio_address(Endpoint const& endpoint);

/** Convert to asio::ip::tcp::endpoint. */
boost::asio::ip::tcp::endpoint
to_asio_endpoint(Endpoint const& endpoint);

}  // namespace IP
}  // namespace beast

namespace beast {

// DEPRECATED
struct IPAddressConversion
{
    explicit IPAddressConversion() = default;

    static IP::Endpoint
    from_asio(boost::asio::ip::address const& address)
    {
        return IP::from_asio(address);
    }
    static IP::Endpoint
    from_asio(boost::asio::ip::tcp::endpoint const& endpoint)
    {
        return IP::from_asio(endpoint);
    }
    static boost::asio::ip::address
    to_asio_address(IP::Endpoint const& address)
    {
        return IP::to_asio_address(address);
    }
    static boost::asio::ip::tcp::endpoint
    to_asio_endpoint(IP::Endpoint const& address)
    {
        return IP::to_asio_endpoint(address);
    }
};

}  // namespace beast

#endif
