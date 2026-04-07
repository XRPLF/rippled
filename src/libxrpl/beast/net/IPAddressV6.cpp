#include <xrpl/beast/net/IPAddressV4.h>
#include <xrpl/beast/net/IPAddressV6.h>

#include <boost/asio/ip/address_v4.hpp>

namespace beast {
namespace IP {

bool
is_private(AddressV6 const& addr)
{
    return (
        ((addr.to_bytes()[0] & 0xfd) != 0) ||  // TODO  fc00::/8 too ?
        (addr.is_v4_mapped() &&
         is_private(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr))));
}

bool
is_public(AddressV6 const& addr)
{
    if (addr.is_loopback())
        return false;
    if (addr.is_v4_mapped())
        return is_public(
            boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr));
    return !is_private(addr) && !addr.is_multicast();
}

}  // namespace IP
}  // namespace beast
