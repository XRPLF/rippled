# `include/xrpl/beast/net/IPAddress.h`

## Role in the System

This header is the central aggregation point for the `beast::IP` address layer. Its job is narrow but load-bearing: it defines the canonical `Address` type used throughout the XRPL peer networking stack, augments it with routing-classification predicates unavailable in Boost.Asio directly, and wires the type into both Beast's internal hashing infrastructure and `boost::hash`.

The header sits at the top of a three-file address hierarchy. `IPAddressV4.h` and `IPAddressV6.h` define the version-specific aliases (`AddressV4`, `AddressV6`) and declare the `is_private`/`is_public` predicates for each protocol version. This file then provides a unified, version-agnostic surface by delegating through a version dispatch to those implementations. `IPEndpoint.h` sits above all three, combining an `Address` with a `Port` into the `Endpoint` type that most callers actually hold.

## Type Alias Strategy

Rather than wrapping `boost::asio::ip::address` in a new class, the file declares a simple type alias:

```cpp
using Address = boost::asio::ip::address;
```

This is a deliberate non-wrapping design. The XRPL codebase interacts heavily with Boost.Asio's networking primitives (sockets, resolvers, acceptors), and those APIs all traffic in `boost::asio::ip::address` directly. Introducing a wrapper would require constant `.native()` extraction or implicit-conversion gymnastics. The alias keeps addresses compatible with the Asio ecosystem while allowing the `beast::IP` namespace to extend the type's behavior via free functions.

## Free Function Set

The six predicates follow a consistent pattern: they accept a `const Address&` and return `bool`. Three of them — `is_loopback`, `is_unspecified`, and `is_multicast` — are simple forwarding wrappers over the equivalent Boost.Asio member functions and exist purely to make calling code more uniform (a caller can invoke `beast::IP::is_loopback(addr)` without knowing whether `addr` is a Boost type or a future replacement).

The remaining two, `is_private` and `is_public`, are more substantive. Boost.Asio does not expose these classifications natively, so the file dispatches through a runtime version check:

```cpp
return (addr.is_v4()) ? is_private(addr.to_v4()) : is_private(addr.to_v6());
```

The actual classification logic lives in the `.cpp` files backing `IPAddressV4.h` and `IPAddressV6.h`. For IPv4 this typically involves checking RFC 1918 ranges (10/8, 172.16/12, 192.168/16) and link-local (169.254/16). The `is_public` predicate is the logical complement, used by the peering layer to decide whether a discovered endpoint is Internet-routable and worth advertising to other nodes.

## Hashing Design

The file provides hashing support in two independent layers.

**`beast::hash_append`** — This is the primary mechanism. The function is a template over any `Hasher` that satisfies the `hash_append` protocol used throughout the Beast hashing subsystem (see `hash_append.h`). It extracts the raw byte representation via `.to_bytes()` on the underlying v4 or v6 address object, letting the hasher consume the address as a plain byte sequence. The `else` branch with `UNREACHABLE` is a defensive invariant: `boost::asio::ip::address` is always one of v4 or v6, so this branch should never execute, but the assertion makes the assumption explicit and will catch any future protocol extensions at development time (the `LCOV_EXCL_START/STOP` markers exclude it from coverage reporting since it is unreachable by design).

**`boost::hash<beast::IP::Address>`** — Many of rippled's containers use `boost::unordered_map` or otherwise depend on `boost::hash`. The explicit specialization placed in `namespace boost` routes through `beast::uhash<>`, which in turn calls `hash_append` using the xxhasher backend. This means both the generic `hash_append` pathway and the `boost::hash` pathway ultimately feed into the same xxhash computation, ensuring consistent hash values regardless of which container type is in use.

Notably, no `std::hash` specialization is provided for `Address`. In contrast, `IPEndpoint.h` provides both `std::hash` and `boost::hash` for `Endpoint`. Since most code that needs to key on an address alone reaches for Boost containers, the omission is intentional and keeps the specializations minimal.

## Relationship to `IPEndpoint`

`IPEndpoint.h` includes this file and builds directly on the `Address` type alias. The `Endpoint::hash_append` implementation hashes `m_addr` using the `hash_append` defined here, meaning the peer-to-peer subsystem's endpoint hashing implicitly relies on this file's byte-level hashing of the address portion. The free function predicates defined here are also mirrored verbatim in `IPEndpoint.h` as pass-through wrappers, forming a consistent property-query API across both the bare-address and address+port representations.