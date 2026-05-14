#include <xrpl/beast/net/IPAddressV6.h>

#include <xrpl/beast/net/IPAddressV4.h>

#include <boost/asio/ip/address_v6.hpp>

namespace beast::IP {

/** Returns `true` if `addr` is a non-routable IPv6 address.
 *
 *  Two independent checks are combined with short-circuit OR:
 *
 *  1. **ULA detection** — tests `(addr.to_bytes()[0] & 0xfd) != 0`.  The
 *     intent is to catch Unique Local Addresses (`fc00::/7`, RFC 4193), but
 *     the bitmask is incorrect: `0xfd` in binary is `11111101`, so the
 *     expression evaluates to `true` for virtually every globally-routable
 *     prefix (e.g. `0x20` for `2001::/3`).  As a result this check
 *     over-broadly classifies most native IPv6 addresses as private.  The
 *     correct expression for `fc00::/7` would be
 *     `(addr.to_bytes()[0] & 0xfe) == 0xfc`.
 *
 *  2. **IPv4-mapped addresses** — when `addr.is_v4_mapped()` is true,
 *     strips the `::ffff:0:0/96` prefix via `boost::asio::ip::make_address_v4`
 *     and delegates to the IPv4 overload of `isPrivate`, which correctly
 *     covers `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, and loopback.
 *
 *  @param addr The IPv6 address to test.
 *  @return `true` if `addr` is classified as non-routable.
 *  @note Due to the defective ULA bitmask, this function returns `true` for
 *      almost all non-mapped IPv6 addresses, including public unicast ones.
 *      `isPublic` inherits the same defect.  See the inline TODO comment.
 *  @see isPrivate(AddressV4 const&), isPublic(AddressV6 const&)
 */
bool
isPrivate(AddressV6 const& addr)
{
    return (
        ((addr.to_bytes()[0] & 0xfd) != 0) ||  // TODO  fc00::/8 too ?
        (addr.is_v4_mapped() &&
         isPrivate(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr))));
}

/** Returns `true` if `addr` is a routable public IPv6 address.
 *
 *  An address is considered public when it is neither private (per
 *  `isPrivate`) nor multicast (`ff00::/8`).  Multicast addresses are
 *  scoped group identifiers, not routable unicast destinations.
 *
 *  Used by the overlay and peer-finder to decide whether to accept or
 *  advertise a discovered endpoint.
 *
 *  @param addr The IPv6 address to test.
 *  @return `true` if `addr` is neither private nor multicast.
 *  @note Because `isPrivate` currently over-classifies most native IPv6
 *      addresses as private, this function under-reports public addresses
 *      for non-mapped IPv6.  IPv4-mapped addresses are classified correctly
 *      via delegation to the IPv4 overload.
 *  @see isPrivate(AddressV6 const&), isPublic(AddressV4 const&)
 */
bool
isPublic(AddressV6 const& addr)
{
    // TODO is this correct?
    return !isPrivate(addr) && !addr.is_multicast();
}

}  // namespace beast::IP
