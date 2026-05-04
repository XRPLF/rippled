#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/ip/address_v6.hpp>

namespace beast::IP {

using AddressV6 = boost::asio::ip::address_v6;

/** Returns `true` if the address is a private unroutable address. */
bool
isPrivate(AddressV6 const& addr);

/** Returns `true` if the address is a public routable address. */
bool
isPublic(AddressV6 const& addr);

}  // namespace beast::IP
