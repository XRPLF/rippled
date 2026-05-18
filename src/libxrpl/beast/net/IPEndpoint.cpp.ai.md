# `IPEndpoint.cpp` — IP Endpoint Parsing, Formatting, and Comparison

This file provides the implementation of `beast::IP::Endpoint`, the XRPL node's representation of a network socket address: an IP address paired with a port number. It lives in the `beast::IP` namespace alongside `IPAddress.h`, `IPAddressV4.cpp`, and `IPAddressV6.cpp`, collectively forming the low-level networking identity layer used throughout the rippled peer-to-peer stack for peer tracking, connection management, and configuration parsing.

## What `Endpoint` Is

`Endpoint` is a thin value type holding a `beast::IP::Address` — itself a typedef for `boost::asio::ip::address` — and a `Port` (`std::uint16_t`). The header adds hashing support for both `std::hash` and `boost::hash`, making `Endpoint` directly usable as an unordered map key, which is critical for the peer table. The `.cpp` implements the non-trivial parts: constructors, string round-tripping, ordering, and stream I/O.

## Parsing Pipeline

The parsing design is deliberately layered.

`from_string_checked()` is the safe entry point. It enforces a 64-character length cap before doing any further work, rejecting obviously malformed input cheaply. It trims whitespace with `boost::trim_copy`, wraps the result in a `std::stringstream`, and delegates to `operator>>`. After the stream extraction, it additionally checks `is.rdbuf()->in_avail() == 0` — confirming the entire input was consumed with no trailing garbage. On any failure, it returns `std::nullopt` rather than throwing.

`from_string()` is a convenience shim that simply unwraps `from_string_checked()`, returning a default-constructed (zero-port, unspecified-address) `Endpoint` on failure. Callers that need to distinguish success from failure should prefer `from_string_checked`.

The real logic lives in `operator>>(std::istream&, Endpoint&)`. Rather than handing the entire string to `boost::asio::ip::make_address` directly, the operator walks the stream character-by-character. This serves two purposes: detecting the address/port boundary correctly, and avoiding reliance on Boost's parser for delimiter handling.

## Stream Extraction Design

The operator reads the first character to decide the format:

- If it is `[`, the input is a bracketed IPv6 endpoint (`[::1]:8080`), and `readTo` is set to `]`.
- Otherwise the character is appended to the address string and the operator infers the type lazily: the first `.` sets `readTo = ':'` (IPv4, colon separates port), while the first `:` sets `readTo = ' '` (bare IPv6, historically space-separated from port).

The character whitelist — `'.'`, `'0'`–`':'` (covering digits and `:`), `'a'`–`'f'`, `'A'`–`'F'` — is deliberately minimal and matches the valid character set of both IPv4 dotted-decimal and IPv6 colon-hex notation. Any character outside this set causes `unget()` and `failbit` to be set. Length is bounded: if the address string hits `INET6_ADDRSTRLEN` characters without resolving, the stream is marked failed.

A comment in the code explicitly acknowledges a **legacy format** where a space character serves as the address/port separator. This is honored via the `isspace` check in the loop-exit condition, which means space terminates the address portion just as cleanly as the expected delimiter. This backward compatibility is a deliberate protocol consideration for reading stored peer data.

After bracket handling for IPv6 endpoints (checking that the character following `]` is either a space or `:`), the accumulated address string is validated through `boost::asio::ip::make_address()` with an error code. Only then is the port read from the stream using the stream's own integer extraction (`is >> port`), inheriting that operator's parsing and range semantics.

## String Formatting

`to_string()` produces RFC 5952-style output. It pre-reserves the string capacity based on address version and whether a port is present — avoiding reallocation for the common case. The IPv6-with-port form is `[addr]:port`; IPv4-with-port is `addr:port`; address-only omits the port entirely. This symmetry with the parser ensures round-trip fidelity.

## Ordering

`operator<` implements lexicographic address-then-port ordering. Address comparison is done first; only when addresses are equal does port break the tie. This ordering is consistent and total, enabling use of `Endpoint` in `std::set` or `std::map` without a custom comparator. The header derives `!=`, `>`, `<=`, and `>=` from the two primitive operators defined in this file.

## Design Tradeoffs

The manual character scanner in `operator>>` is more verbose than simply feeding the whole string to Boost's address parser, but it is necessary for correct delimiter detection — Boost's parser does not know where the address ends and the port begins. The 64-character input cap in `from_string_checked` is a cheap denial-of-service guard against pathologically long inputs reaching the more expensive parsing path. Failures propagate via `std::ios_base::failbit` rather than exceptions, consistent with standard C++ stream idioms and the no-exception policy typical in network I/O hot paths.