#pragma once

#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/net/IPAddressV4.h>
#include <xrpl/beast/net/IPAddressV6.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/ip/address.hpp>
#include <boost/functional/hash.hpp>

#include <string>

//------------------------------------------------------------------------------

namespace beast {
namespace IP {

using Address = boost::asio::ip::address;

/** Returns the address represented as a string. */
inline std::string
to_string(Address const& addr)
{
    return addr.to_string();
}

/** Returns `true` if this is a loopback address. */
inline bool
isLoopback(Address const& addr)
{
    return addr.is_loopback();
}

/** Returns `true` if the address is unspecified. */
inline bool
isUnspecified(Address const& addr)
{
    return addr.is_unspecified();
}

/** Returns `true` if the address is a multicast address. */
inline bool
isMulticast(Address const& addr)
{
    return addr.is_multicast();
}

/** Returns `true` if the address is a private unroutable address. */
inline bool
isPrivate(Address const& addr)
{
    return (addr.is_v4()) ? isPrivate(addr.to_v4()) : isPrivate(addr.to_v6());
}

/** Returns `true` if the address is a public routable address. */
inline bool
isPublic(Address const& addr)
{
    return (addr.is_v4()) ? isPublic(addr.to_v4()) : isPublic(addr.to_v6());
}

}  // namespace IP

//------------------------------------------------------------------------------

template <class Hasher>
void
hash_append(Hasher& h, beast::IP::Address const& addr) noexcept
{
    using beast::hash_append;
    if (addr.is_v4())
    {
        hash_append(h, addr.to_v4().to_bytes());
    }
    else if (addr.is_v6())
    {
        hash_append(h, addr.to_v6().to_bytes());
    }
    else
    {
        // LCOV_EXCL_START
        UNREACHABLE("beast::hash_append : invalid address type");
        // LCOV_EXCL_STOP
    }
}
}  // namespace beast

namespace boost {
template <>
struct hash<::beast::IP::Address>
{
    explicit hash() = default;

    std::size_t
    operator()(::beast::IP::Address const& addr) const
    {
        return ::beast::Uhash<>{}(addr);
    }
};
}  // namespace boost
