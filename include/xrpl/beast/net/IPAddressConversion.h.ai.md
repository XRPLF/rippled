# `IPAddressConversion.h` — Asio/Beast Endpoint Translation Layer

This header declares the conversion boundary between Boost.Asio's IP address types and the XRPL-internal `beast::IP::Endpoint` abstraction. It is a small but architecturally significant file: it defines where the networking I/O layer (Asio) hands off to the application's canonical address representation.

## Why This Exists

XRPL's internal subsystems — PeerFinder, the Overlay layer, resource management, the RPC server — all reason about peers and clients in terms of `beast::IP::Endpoint`, a version-independent address-plus-port type that doesn't drag in Asio dependencies. Asio, meanwhile, is used for the actual socket I/O and resolves into its own `boost::asio::ip::address` and `boost::asio::ip::tcp::endpoint` types. This file provides the four translation functions that sit at the seam between those two worlds.

## The Four Free Functions

The primary API lives in the `beast::IP` namespace as free functions:

- `from_asio(boost::asio::ip::address const&)` — wraps a bare Asio address into an `Endpoint` with port zero. The zero-port contract is explicit in the comment and reflects the fact that an `ip::address` carries no port.
- `from_asio(boost::asio::ip::tcp::endpoint const&)` — the full-fidelity conversion, preserving both address and port.
- `to_asio_address(Endpoint const&)` — extracts just the address component, discarding the port. Used when only the address is needed by an Asio API.
- `to_asio_endpoint(Endpoint const&)` — round-trips a `beast::IP::Endpoint` back to an Asio TCP endpoint with port intact.

The implementation in `IPAddressConversion.cpp` is deliberately trivial: `from_asio(endpoint)` calls `Endpoint{endpoint.address(), endpoint.port()}`, and `to_asio_endpoint` calls `boost::asio::ip::tcp::endpoint{endpoint.address(), endpoint.port()}`. The simplicity is intentional — `beast::IP::Endpoint` is constructor-compatible with Asio's address type because `beast::IP::Address` is itself a thin alias over Asio's address internally.

The asymmetry between the two directions (both `from_asio` variants versus `to_asio_address` and `to_asio_endpoint` as separate functions) reflects usage reality: incoming Asio data always arrives with a full endpoint, but outgoing calls sometimes need only an address.

## The Deprecated `IPAddressConversion` Struct

The `beast::IPAddressConversion` struct at the bottom of the file is marked `// DEPRECATED` and simply re-exposes the four free functions as `static` methods. Despite the deprecation marker, a grep across the codebase shows it is still the form used almost universally: `OverlayImpl.cpp`, `ConnectAttempt.cpp`, `Logic.h`, `ServerHandler.cpp`, `ResourceManager.cpp`, `WSInfoSub.h`, `ResolverAsio.cpp`, and `BaseHTTPPeer.h` all call `beast::IPAddressConversion::from_asio(...)` or `beast::IPAddressConversion::to_asio_endpoint(...)`. The intended migration is to call the free functions in `beast::IP` directly, but that cleanup has not been completed.

## Relationship to `IPEndpoint.h`

`IPAddressConversion.h` includes `IPEndpoint.h`, which defines `beast::IP::Endpoint` and its full interface. The `Endpoint` class stores an `Address` and a `Port` (`std::uint16_t`), supports comparison operators, hashing (`std::hash` and `boost::hash` specializations), and streaming. `IPAddressConversion.h` is purely a conversion adapter on top of that type — it adds no new state or logic, only the translation bridge to Asio.

## Design Pattern

The pattern here — internal canonical type, thin conversion layer at the I/O boundary — keeps application logic decoupled from Asio's type hierarchy. Code inside PeerFinder or the resource rate-limiter never needs to `#include <boost/asio.hpp>`; they work with `beast::IP::Endpoint` throughout, and only the narrow I/O-facing code at socket accept/connect time calls these converters. This makes the internal logic independently testable and insulates it from Asio API evolution.