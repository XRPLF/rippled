# `ProtocolVersion.h` — XRPL Peer Protocol Version Negotiation Interface

This header defines the type system and declares the full API for managing version negotiation of the XRPL peer-to-peer wire protocol. It lives in `overlay/detail/` because version negotiation is an implementation concern of the overlay layer, not part of its public interface to the rest of the node.

## The `ProtocolVersion` Type

`ProtocolVersion` is a simple type alias for `std::pair<std::uint16_t, std::uint16_t>`, encoding a major and minor version number. Using `std::pair` rather than a dedicated struct is a deliberate design shortcut: pair's built-in lexicographic comparison makes `<`, `==`, and ordering just work — a critical property since much of the negotiation logic depends on sorting and set intersection over version lists. The `make_protocol(major, minor)` factory is a thin inline convenience wrapper that names the construction intent at call sites rather than having callers write raw brace-initialization.

The canonical string form of a version is `"XRPL/major.minor"` (e.g. `"XRPL/2.1"`), produced by `to_string()`. This format is used directly as the value of the HTTP `Upgrade` header during the peer handshake. At the time of writing, the implementation supports `{2,1}` and `{2,2}`.

## Parsing: `parseProtocolVersions()`

`parseProtocolVersions()` takes a `boost::beast::string_view` containing the raw value of the HTTP `Upgrade` header — a comma-separated list of tokens such as `"RTXP/1.2, XRPL/2.1, XRPL/2.2"`. It extracts and returns only those tokens that represent valid XRPL protocol versions, applying three layers of validation in the implementation:

1. **Regex filter**: only `XRPL/` prefixed tokens with a major version of 2 or higher (no leading zeros) and a valid minor (no leading zeros except plain `0`) pass through. This explicitly excludes the legacy `RTXP` protocol used before version 2.
2. **Numeric range check**: values are cast to `uint16_t` via `beast::lexicalCastChecked`, rejecting anything that overflows.
3. **Round-trip sanity check**: the parsed version is converted back to a string and compared to the original token, guarding against any edge case where parsing and formatting could diverge.

The return value is guaranteed sorted in ascending order and free of duplicates, which is a prerequisite for the set-intersection logic in `negotiateProtocolVersion()`.

## Negotiation: `negotiateProtocolVersion()`

Two overloads handle the negotiation step. The `string_view` overload is a convenience form for callers that hold a raw header value (the common case in `OverlayImpl.cpp`); it delegates internally to `parseProtocolVersions()` and then calls the `vector` overload. The vector overload contains the actual logic: it computes the intersection between the peer's advertised versions and the local `supportedProtocolList` (the compile-time array of versions this build speaks), and returns the **highest** version in that intersection as a `std::optional<ProtocolVersion>`. Returning `std::nullopt` signals that no mutually acceptable version exists, and the connection should be rejected.

The choice of highest version rather than lowest is intentional — it ensures both sides use the most capable protocol they both support, maximizing available features while maintaining backward compatibility with older peers.

## Build-time Version Registry

`supportedProtocolVersions()` returns a `const std::string&` holding the full, comma-separated `Upgrade` header value (e.g. `"XRPL/2.1, XRPL/2.2"`). This string is computed once at first call and cached in a function-local static, avoiding repeated allocation. It is used by `Handshake.cpp` when constructing the outbound HTTP `GET` request during peer connection establishment.

`isProtocolSupported()` is a simple point-lookup into the same compile-time list, used when a specific version identity needs to be checked rather than a full negotiation performed.

## Relationship to the Handshake Flow

The full negotiation flow across the overlay: `Handshake.cpp:makeRequest()` inserts the result of `supportedProtocolVersions()` into the outbound `Upgrade` header. On the receiving side, `OverlayImpl.cpp` extracts that header value and calls `negotiateProtocolVersion()`. If negotiation succeeds, the resulting `ProtocolVersion` is passed into `makeResponse()` (back in `Handshake.cpp`) and stored on the `PeerImp` instance, where it governs message framing and feature availability for the entire lifetime of that peer session.