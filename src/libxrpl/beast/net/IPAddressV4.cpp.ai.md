# `IPAddressV4.cpp` — IPv4 Address Classification Utilities

This file implements three free functions in the `beast::IP` namespace that classify IPv4 addresses by network scope and historical address class. It is a thin but meaningful layer on top of Boost.Asio's `boost::asio::ip::address_v4`, which is aliased as `AddressV4` in the corresponding header.

## Role in the System

The XRPL peer-finder and overlay subsystems need to distinguish publicly routable addresses from private or multicast ones before making connection decisions. For example, `peerfinder/detail/Logic.h` gates per-IP connection limits (`ipLimit`) on `is_public()`, and the overlay handshake logic uses the same predicate to validate advertised peer endpoints. Without these predicates, the network layer would have to replicate CIDR range arithmetic at every call site.

## `is_private()`

```cpp
bool is_private(AddressV4 const& addr)
```

Classifies an address as non-routable by checking three RFC 1918 private ranges and the loopback range:

| Mask | Range | RFC |
|---|---|---|
| `/8` (`0xff000000`) | `10.0.0.0 – 10.255.255.255` | RFC 1918 |
| `/12` (`0xfff00000`) | `172.16.0.0 – 172.31.255.255` | RFC 1918 |
| `/16` (`0xffff0000`) | `192.168.0.0 – 192.168.255.255` | RFC 1918 |
| — | `127.0.0.0/8` (loopback) | handled by `addr.is_loopback()` |

The implementation calls `addr.to_uint()` once per check, converting the address to a host-order `uint32_t` for direct bitmasking. Each condition ANDs the address with the appropriate prefix mask and compares the result to the network base address — the canonical, branch-free CIDR test. The loopback check is delegated to Boost.Asio's `is_loopback()` rather than replicating `127.0.0.0/8` arithmetic inline, which keeps the intent readable and avoids a subtle mistake (loopback is a full `/8`, not just `127.0.0.1`).

Note that link-local (`169.254.0.0/16`) and `100.64.0.0/10` (RFC 6598 shared address space) are intentionally excluded. The function covers exactly the three RFC 1918 blocks the XRPL peer filter cares about.

## `is_public()`

```cpp
bool is_public(AddressV4 const& addr)
```

A simple compositional predicate: an address is public if and only if it is neither private nor multicast. This is expressed directly as:

```cpp
return !is_private(addr) && !addr.is_multicast();
```

The short-circuit evaluation means `is_multicast()` (which covers `224.0.0.0/4`) is only tested when `is_private()` returns false. In peer-connection logic this function acts as the primary gate: only addresses that pass `is_public()` are considered viable candidates for outbound connections or counted against per-IP limits.

## `get_class()`

```cpp
char get_class(AddressV4 const& addr)
```

Returns the historical IPv4 address class (`'A'`, `'B'`, `'C'`, or `'D'`) by examining the top three bits of the address. The implementation uses a static lookup table `"AAAABBCD"` indexed by `(addr.to_uint() & 0xE0000000) >> 29`, which shifts the three most significant bits into a 0–7 index.

The bit-to-class mapping follows the original RFC 791 classful scheme:

- Bits `000`–`011` (indices 0–3) → class `A` (0.0.0.0/1)
- Bits `100`–`101` (indices 4–5) → class `B` (128.0.0.0/2)
- Bit `110` (index 6) → class `C` (192.0.0.0/3)
- Bit `111` (index 7) → class `D` (224.0.0.0/4, multicast)

The `// cspell:disable-line` annotation on the table is a spell-checker suppression; "AAAABBCD" would otherwise trigger a false alarm. The table is `static const`, so it is initialized once and lives in read-only memory for the process lifetime — a trivial but correct optimization for a hot path.

Classful addressing is technically obsolete (superseded by CIDR), but `get_class()` remains useful in the XRPL codebase as a coarse diagnostic tool and for logging peer address metadata.

## Design Notes

All three functions are pure, stateless free functions. There is no object to construct, no error path, and no allocation — every call reduces to a handful of bitwise operations on a 32-bit integer. This makes them safe to call from any context including lock-holding code, which is exactly how `Logic.h` uses `is_public()`. The underlying `AddressV4` type is Boost.Asio's `address_v4`, so parsing and validation are already handled upstream before any of these predicates are reached.