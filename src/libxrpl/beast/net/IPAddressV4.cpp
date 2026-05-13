#include <xrpl/beast/net/IPAddressV4.h>

namespace beast::IP {

/** Returns `true` if `addr` is non-routable on the public internet.
 *
 *  Matches the three RFC 1918 private ranges and the loopback range:
 *  - `10.0.0.0/8`      (`0xff000000` mask, RFC 1918)
 *  - `172.16.0.0/12`   (`0xfff00000` mask, RFC 1918)
 *  - `192.168.0.0/16`  (`0xffff0000` mask, RFC 1918)
 *  - `127.0.0.0/8`     (loopback, via `addr.is_loopback()`)
 *
 *  Link-local (`169.254.0.0/16`) and RFC 6598 shared address space
 *  (`100.64.0.0/10`) are intentionally not covered.
 *
 *  @param addr The IPv4 address to test.
 *  @return `true` if `addr` falls within a private or loopback range.
 */
bool
isPrivate(AddressV4 const& addr)
{
    return ((addr.to_uint() & 0xff000000) == 0x0a000000) ||  // Prefix /8,    10.  #.#.#
        ((addr.to_uint() & 0xfff00000) == 0xac100000) ||  // Prefix /12   172. 16.#.# - 172.31.#.#
        ((addr.to_uint() & 0xffff0000) == 0xc0a80000) ||  // Prefix /16   192.168.#.#
        addr.is_loopback();
}

/** Returns `true` if `addr` is routable on the public internet.
 *
 *  An address is public when it is neither private (RFC 1918 / loopback)
 *  nor multicast (`224.0.0.0/4`). Used by the overlay and peer-finder as
 *  the primary gate before counting an address against per-IP connection
 *  limits or including it in handshake `Remote-IP` headers.
 *
 *  @param addr The IPv4 address to test.
 *  @return `true` if `addr` is neither private nor multicast.
 */
bool
isPublic(AddressV4 const& addr)
{
    return !isPrivate(addr) && !addr.is_multicast();
}

/** Returns the historical classful address class of `addr`.
 *
 *  Inspects the three most-significant bits of the address and maps them
 *  to a class character via a static lookup table, following the original
 *  RFC 791 scheme:
 *
 *  | Top 3 bits | Indices | Class | Range         |
 *  |------------|---------|-------|---------------|
 *  | 000–011    | 0–3     | `'A'` | 0.0.0.0/1     |
 *  | 100–101    | 4–5     | `'B'` | 128.0.0.0/2   |
 *  | 110        | 6       | `'C'` | 192.0.0.0/3   |
 *  | 111        | 7       | `'D'` | 224.0.0.0/4   |
 *
 *  Classful addressing is obsolete (superseded by CIDR), but this function
 *  is retained as a coarse diagnostic and logging aid.
 *
 *  @param addr The IPv4 address to classify.
 *  @return One of `'A'`, `'B'`, `'C'`, or `'D'`.
 */
char
getClass(AddressV4 const& addr)
{
    static char const* kTABLE = "AAAABBCD";  // cspell:disable-line
    return kTABLE[(addr.to_uint() & 0xE0000000) >> 29];
}

}  // namespace beast::IP
