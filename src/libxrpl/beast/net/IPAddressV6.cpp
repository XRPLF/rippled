#include <xrpl/beast/net/IPAddressV6.h>

#include <xrpl/beast/net/IPAddressV4.h>

#include <boost/asio/ip/address_v6.hpp>

namespace beast::IP {

bool
isPrivate(AddressV6 const& addr)
{
    // fc00::/7 - Unique Local Address (ULA), covers fc00:: and fd00::
    if ((addr.to_bytes()[0] & 0xfe) == 0xfc)
        return true;
    if (addr.is_v4_mapped())
        return isPrivate(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr));
    return false;
}

bool
isPublic(AddressV6 const& addr)
{
    if (addr.is_loopback())
        return false;
    if (addr.is_v4_mapped())
        return isPublic(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr));
    if (isPrivate(addr))
        return false;
    if (addr.is_multicast())
        return false;
    if (addr.is_unspecified())
        return false;

    auto const b = addr.to_bytes();

    // fe80::/10 - Link-local
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
        return false;
    // 100::/64 - Discard prefix (RFC 6666)
    if (b[0] == 0x01 && b[1] == 0x00 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
        b[6] == 0 && b[7] == 0)
        return false;
    // 2001:db8::/32 - Documentation (RFC 3849)
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8)
        return false;
    // 2001::/32 - IETF Protocol Assignments / Teredo (RFC 4380)
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00)
        return false;
    // 2001:20::/28 - ORCHIDv2 (RFC 7343)
    // 28-bit prefix: 0x2001002 => b[0]=0x20, b[1]=0x01, b[2]=0x00,
    // top nibble of b[3]=0x2
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && (b[3] & 0xf0) == 0x20)
        return false;
    // 2002::/16 - 6to4 (RFC 3056, deprecated by RFC 7526)
    if (b[0] == 0x20 && b[1] == 0x02)
        return false;

    return true;
}

}  // namespace beast::IP
