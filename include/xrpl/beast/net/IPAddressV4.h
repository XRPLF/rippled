#ifndef BEAST_NET_IPADDRESSV4_H_INCLUDED
#define BEAST_NET_IPADDRESSV4_H_INCLUDED

#include <xrpl/beast/hash/hash_append.h>

#include <boost/asio/ip/address_v4.hpp>

namespace beast {
namespace IP {

using AddressV4 = boost::asio::ip::address_v4;

/** Returns `true` if the address is a private unroutable address. */
bool
is_private(AddressV4 const& addr);

/** Returns `true` if the address is a public routable address. */
bool
is_public(AddressV4 const& addr);

/** Returns the address class for the given address.
    @note Class 'D' represents multicast addresses (224.*.*.*).
*/
char
get_class(AddressV4 const& address);

}  // namespace IP
}  // namespace beast

#endif
