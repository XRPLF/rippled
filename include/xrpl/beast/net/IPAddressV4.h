#pragma once

#include <boost/asio/ip/address_v4.hpp>

namespace beast::IP {

using AddressV4 = boost::asio::ip::address_v4;

/** Returns `true` if the address is a private unroutable address. */
bool
isPrivate(AddressV4 const& addr);

/** Returns `true` if the address is a public routable address. */
bool
isPublic(AddressV4 const& addr);

/** Returns the address class for the given address.
    @note Class 'D' represents multicast addresses (224.*.*.*).
*/
char
getClass(AddressV4 const& address);

}  // namespace beast::IP
