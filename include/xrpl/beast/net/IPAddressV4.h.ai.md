# `include/xrpl/beast/net/IPAddressV4.h`

## Role in the System

This header is part of the `beast::IP` networking utility layer within the XRPL node software. It extends `boost::asio::ip::address_v4` — which the file aliases as `AddressV4` — with three free functions that encode IPv4 address-classification logic needed throughout the peer overlay system. Rather than scattering bitmask arithmetic across call sites, these declarations give every consumer a single, named vocabulary for the two questions the overlay layer asks most often: "is this peer reachable from the public internet?" and "what legacy address class does it fall into?"

## `AddressV4` Type Alias

`AddressV4` is a transparent alias for `boost::asio::ip::address_v4`. No wrapper class is introduced; the goal is purely to anchor the utility functions in the `beast::IP` namespace so they compose naturally with the parallel `IPAddress.h` abstraction for the polymorphic `Address` type. `IPAddress.h` dispatches `is_private` and `is_public` to the V4 and V6 variants by checking the address family at runtime — this header supplies the V4 half of that dispatch.

## Function Semantics

**`is_private(AddressV4 const& addr)`** returns `true` for addresses that are not globally routable under RFC 1918 and loopback conventions. The implementation in `IPAddressV4.cpp` tests three private CIDR blocks with raw 32-bit bitmasks: `10.0.0.0/8` (mask `0xff000000`), `172.16.0.0/12` (mask `0xfff00000`), and `192.168.0.0/16` (mask `0xffff0000`), plus delegates to `addr.is_loopback()` for `127.*`. The choice of bitmask arithmetic over CIDR helper utilities is deliberate — it avoids constructing temporary subnet objects on a hot path and keeps the check branch-free.

**`is_public(AddressV4 const& addr)`** is defined simply as `!is_private(addr) && !addr.is_multicast()`. Multicast (`224.0.0.0/4`) is excluded because those addresses are neither routable in the normal unicast sense nor private, so neither `is_private` nor the negative alone would give the right answer.

**`get_class(AddressV4 const& address)`** returns the traditional classful letter (`'A'`, `'B'`, `'C'`, `'D'`) based on the three high-order bits of the address. The implementation uses a compact lookup table `"AAAABBCD"` indexed by `(addr.to_uint() & 0xE0000000) >> 29`, mapping the eight possible 3-bit prefix values to their historical class. Class `'D'` (prefix bits `110x` and `111x` — indices 6 and 7) corresponds to multicast. This function is classically deprecated for real routing decisions but remains useful for diagnostics and logging.

## Relationship to the Overlay Layer

The primary consumer of `is_public` is the peer handshake logic in `overlay/detail/Handshake.cpp`. During connection establishment, the node inserts its own public IP into the HTTP upgrade headers only when the remote address is public — preventing disclosure of internal topology when a peer connects from a private network. The overlay also validates `Local-IP` / `Remote-IP` header consistency using `is_public`, rejecting connections where a publicly-reachable peer reports conflicting address information that could indicate a misconfigured or spoofed connection.

## Dependency on `hash_append`

The `#include <xrpl/beast/hash/hash_append.h>` inclusion at line 3 is shared infrastructure pulled in for the wider `beast::IP` module rather than being required by the three functions declared here. The hashing machinery is used by `IPAddress.h` and `IPEndpoint.h` when addresses appear as hash map keys.

## Design Note

The header introduces no classes and no template machinery — it is intentionally thin. The `boost::asio` type does all the heavy lifting for address representation, parsing, and comparison; this file only layers on the XRPL-specific semantic predicates (`is_private`, `is_public`) and the legacy diagnostic utility (`get_class`) that the rest of the codebase needs. Keeping the functions as non-member free functions in the `beast::IP` namespace means they compose uniformly with the `Address` (version-agnostic) overloads in `IPAddress.h` without any inheritance or virtual dispatch.