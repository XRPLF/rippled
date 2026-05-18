# `IPAddressV6.h` — IPv6 Address Classification Utilities

This header is part of the `beast::IP` networking abstraction layer within rippled's embedded Beast library. Its sole purpose is to declare two classification predicates for IPv6 addresses: `is_private()` and `is_public()`. It mirrors the parallel structure of `IPAddressV4.h`, which provides the same pair of predicates for IPv4 along with an additional `get_class()` helper.

## Type Alias Strategy

Rather than wrapping `boost::asio::ip::address_v6` in a new class, the header simply re-exports it as `beast::IP::AddressV6`:

```cpp
using AddressV6 = boost::asio::ip::address_v6;
```

This is a deliberate zero-cost abstraction. Beast gets a stable name in its own namespace without paying any overhead — no wrapper class, no virtual dispatch, no conversion. The same pattern is used for `AddressV4` and the version-agnostic `Address` type. The uniformity means callers can use `beast::IP::AddressV6` throughout and remain insulated from any future change to the underlying Asio type.

## The Classification Functions

`is_private()` and `is_public()` are declared here but defined in `IPAddressV6.cpp`. The implementation reveals the actual logic:

```cpp
bool is_private(AddressV6 const& addr) {
    return (
        ((addr.to_bytes()[0] & 0xfd) != 0) ||  // TODO fc00::/8 too?
        (addr.is_v4_mapped() &&
         is_private(boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, addr))));
}
```

The first clause checks whether the most significant byte ANDed with `0xfd` is nonzero, targeting the `fc00::/7` Unique Local Address (ULA) range (RFC 4193), which covers `fc00::` through `fdff::`. The `TODO` comment flags genuine uncertainty: the mask `0xfd` catches `fd00::/8` but may not fully cover `fc00::/8`. The second clause handles IPv4-mapped IPv6 addresses (the `::ffff:0:0/96` prefix), delegating to `is_private(AddressV4)` after extracting the embedded IPv4 address — this correctly classifies RFC 1918 addresses embedded in IPv6 form.

`is_public()` is defined as the logical complement: an address is public if it is neither private nor multicast. The `TODO` comment in the implementation acknowledges that this definition may not be fully rigorous.

## Role in the Dispatch Chain

The broader `IPAddress.h` header shows why these per-family predicates exist. Its version-agnostic `is_private(Address const&)` dispatches based on address family:

```cpp
return (addr.is_v4()) ? is_private(addr.to_v4()) : is_private(addr.to_v6());
```

`IPAddressV6.h` provides one half of that dispatch pair. The design avoids any runtime polymorphism — the dispatch is a simple conditional at the call site, and both branches resolve to non-virtual free functions. This keeps network classification paths entirely allocation-free and inline-friendly.

## Usage Context

These predicates are used throughout rippled to make peering and overlay decisions — for example, determining whether a discovered peer address should be treated as externally reachable, or whether a connection should be filtered as a private/internal node. The separation of IPv4 and IPv6 handling into distinct files (`IPAddressV4.h` / `IPAddressV6.h`) keeps classification logic clearly delineated by protocol version, making it straightforward to audit or extend either family independently.