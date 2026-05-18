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
    if (isPrivate(addr))
        return false;
    if (addr.is_multicast())
        return false;

    auto const ip = addr.to_uint();

    // 0.0.0.0/8        "This network"
    if ((ip & 0xff000000) == 0x00000000)
        return false;
    // 100.64.0.0/10     Shared Address Space (CGNAT) - RFC 6598
    if ((ip & 0xffc00000) == 0x64400000)
        return false;
    // 169.254.0.0/16    Link-local
    if ((ip & 0xffff0000) == 0xa9fe0000)
        return false;
    // 192.0.0.0/24      IETF Protocol Assignments - RFC 6890
    if ((ip & 0xffffff00) == 0xc0000000)
        return false;
    // 192.0.2.0/24      TEST-NET-1 (documentation) - RFC 5737
    if ((ip & 0xffffff00) == 0xc0000200)
        return false;
    // 192.88.99.0/24    6to4 Relay Anycast (deprecated) - RFC 7526
    if ((ip & 0xffffff00) == 0xc0586300)
        return false;
    // 198.18.0.0/15     Benchmarking - RFC 2544
    if ((ip & 0xfffe0000) == 0xc6120000)
        return false;
    // 198.51.100.0/24   TEST-NET-2 (documentation) - RFC 5737
    if ((ip & 0xffffff00) == 0xc6336400)
        return false;
    // 203.0.113.0/24    TEST-NET-3 (documentation) - RFC 5737
    if ((ip & 0xffffff00) == 0xcb007100)
        return false;
    // 240.0.0.0/4       Reserved for future use - RFC 1112
    if ((ip & 0xf0000000) == 0xf0000000)
        return false;

    return true;
}

char
getClass(AddressV4 const& addr)
{
    static char const* kTable = "AAAABBCD";  // cspell:disable-line
    return kTable[(addr.to_uint() & 0xE0000000) >> 29];
}

}  // namespace beast::IP
