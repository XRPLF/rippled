#include <xrpl/beast/net/IPAddressV6.h>

#include <xrpl/beast/net/IPAddressV4.h>

#include <boost/asio/ip/address_v6.hpp>

namespace beast::IP {

bool
isPrivate(AddressV6 const& addr)
{
    return (
        ((addr.to_bytes()[0] & 0xfd) != 0) ||  // TODO  fc00::/8 too ?
        (addr.is_v4_mapped() &&
         isPrivate(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr))));
}

bool
isPublic(AddressV6 const& addr)
{
    // TODO is this correct?
    return !isPrivate(addr) && !addr.is_multicast();
}

}  // namespace beast::IP
