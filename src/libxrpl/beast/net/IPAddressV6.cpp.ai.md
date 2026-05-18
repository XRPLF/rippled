# `IPAddressV6.cpp` — IPv6 Address Classification

This file provides two free functions in the `beast::IP` namespace — `is_private` and `is_public` — that classify an IPv6 address as routable or not. It is a direct sibling of `IPAddressV4.cpp`, which supplies the same interface for IPv4. Both files wrap `boost::asio` address types with the thin domain-specific logic the rest of the XRPL peer-networking layer needs to make decisions like whether to accept or advertise a discovered endpoint.

`AddressV6` is nothing more than a type alias for `boost::asio::ip::address_v6`, defined in the header. The classification functions therefore operate purely on Boost.Asio's byte-level accessors and need no custom address representation.

## `is_private`

The function attempts to identify private, non-routable IPv6 addresses through two independent checks combined with short-circuit OR logic.

The first check — `(addr.to_bytes()[0] & 0xfd) != 0` — is explicitly incomplete and annotated with a `// TODO fc00::/8 too ?` comment. The stated intent is to detect Unique Local Addresses (ULA), the IPv6 equivalent of RFC 1918 private space, which occupy `fc00::/7` (first bytes `0xfc` or `0xfd` per RFC 4193). However, the bitmask as written is logically unsound: `0xfd` in binary is `11111101`, so masking any first byte with it and testing for non-zero produces `true` for virtually every globally-routable IPv6 address (e.g., `0x20` for `2001::/3`, `0x26`, `0x2a`, etc.). The only first bytes that pass this mask as zero are `0x00` and `0x02`. In practice this means the function classifies almost all native IPv6 addresses, including public ones, as private — a known defect signaled by the dual TODO markers here and in `is_public`.

The correct expression for `fc00::/7` would be `(addr.to_bytes()[0] & 0xfe) == 0xfc`; the existing code appears to have been written with the inverse comparison in mind and never corrected.

The second check handles IPv4-mapped addresses (`::ffff:0:0/96`). When `addr.is_v4_mapped()` returns true, Boost.Asio's `make_address_v4` with the `v4_mapped` tag strips the mapping prefix and produces a plain `boost::asio::ip::address_v4`, which is then passed to the IPv4 overload of `is_private`. That overload properly handles `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, and loopback, making the IPv4 private-address logic correct even when the address arrives through an IPv6 socket.

## `is_public`

`is_public` is defined as the logical complement of both `is_private` and `addr.is_multicast()`, inheriting the same `// TODO is this correct?` caveat. The pattern is identical to the IPv4 version in `IPAddressV4.cpp`. Multicast addresses (`ff00::/8`) are excluded from "public" because they are scoped group identifiers, not routable unicast destinations. Since `is_private` is currently over-broad, `is_public` in turn under-reports public addresses for non-mapped IPv6 — a consequence that propagates into any call site that gates peer connections or address advertisement on this predicate.

## Design Notes

The `beast::IP` namespace follows a consistent style: Boost.Asio types are re-exported as local aliases (`AddressV4`, `AddressV6`) and classification functions are implemented as non-member free functions rather than methods, keeping the types themselves thin wrappers and the policy logic separate. This allows the classification rules to evolve without touching the address representation. The IPv4 and IPv6 variants share the same header-declared API shape, so call sites can use overload resolution to dispatch on address family without conditionals.

No exceptions are thrown, no heap allocations occur, and all operations are O(1) byte reads. The functions are safe to call from any context, including network I/O threads, with no synchronization concerns.