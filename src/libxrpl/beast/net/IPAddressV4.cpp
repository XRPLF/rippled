#include <xrpl/beast/net/IPAddressV4.h>

namespace beast::IP {

bool
isPrivate(AddressV4 const& addr)
{
    return ((addr.to_uint() & 0xff000000) == 0x0a000000) ||  // Prefix /8,    10.  #.#.#
        ((addr.to_uint() & 0xfff00000) == 0xac100000) ||  // Prefix /12   172. 16.#.# - 172.31.#.#
        ((addr.to_uint() & 0xffff0000) == 0xc0a80000) ||  // Prefix /16   192.168.#.#
        addr.is_loopback();
}

bool
isPublic(AddressV4 const& addr)
{
    return !isPrivate(addr) && !addr.is_multicast();
}

char
getClass(AddressV4 const& addr)
{
    static char const* kTABLE = "AAAABBCD";  // cspell:disable-line
    return kTABLE[(addr.to_uint() & 0xE0000000) >> 29];
}

}  // namespace beast::IP
