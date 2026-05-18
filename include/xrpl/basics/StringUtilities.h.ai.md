# `include/xrpl/basics/StringUtilities.h`

## Role and Purpose

This header is the central string manipulation toolkit for the `xrpl` namespace, gathering five loosely-related but frequently needed operations: SQLite blob escaping, hex decoding, URL parsing, whitespace trimming, integer parsing, and TOML domain validation. These utilities are used throughout the node's database layer, RPC subsystem, configuration parser, and peer-handshake code, making this one of the more broadly depended-upon headers in the `basics` module.

The header deliberately keeps its own template logic (the `strUnHex` family) inline while deferring everything regex-heavy or Boost-heavy to the `.cpp` implementation, keeping compile times manageable for translation units that only need hex conversion.

---

## Key Components

### Hex Decoding — `strUnHex`

The primary workhorse is the templated `strUnHex(strSize, begin, end)`. Rather than calling a library function, it builds a `static constexpr` 256-entry lookup table at compile time. The table maps every `unsigned char` value to its nibble value (0–15) or -1 for invalid characters, supporting both upper- and lower-case hex. Returning -1 for invalid bytes (rather than throwing) allows a cheap validity check without exception overhead.

The design handles **odd-length hex strings** explicitly: if `strSize` is odd, the first character is decoded alone as a high nibble, making `"A"` decode to `\x0A` rather than failing. This matters in practice; the test suite verifies `"D0A"` decodes to the two-byte sequence `"\r\n"`.

Two thin wrappers, `strUnHex(std::string const&)` and `strViewUnHex(std::string_view)`, exist solely to spare callers from passing the string size and iterators by hand. Both return `std::optional<Blob>`, using `std::nullopt` to signal a malformed or invalid input — consistent with the xrpl codebase's preference for value-returning error signaling over exceptions in hot paths.

The companion `strHex.h` (included here) provides the inverse direction via `boost::algorithm::hex`, giving callers both encode and decode in a single include. `Blob` — a `std::vector<unsigned char>` — is the shared currency between them.

### SQLite Blob Literals — `sqlBlobLiteral`

`sqlBlobLiteral(Blob const&)` produces SQLite's `X'HEXDATA'` literal syntax, used when constructing raw SQL queries that embed binary ledger objects. It is called in `AcceptedLedgerTx::getEscMeta()` (which encodes raw transaction metadata for the ledger SQLite store) and in `STTx` serialization. The function pre-reserves `size * 2 + 3` characters to avoid reallocations, then uses `boost::algorithm::hex` for the hex encoding, sandwiched between the `X'` prefix and `'` suffix. The existence of this function keeps SQL-escaping concerns out of the objects that own the binary data.

### URL Parsing — `parsedURL` and `parseUrl`

`parsedURL` is a plain-data aggregate holding scheme, username, password, domain, an optional `uint16_t` port, and path. The equality operator omits `username` and `password`, which matters for connection deduplication: two endpoints with the same scheme, domain, port, and path are considered the same regardless of credentials.

`parseUrl(parsedURL&, std::string const&)` drives a static `boost::regex` against the RFC 3986 authority-form URI pattern. Several non-obvious decisions are worth noting:

- **IPv6 bracket stripping**: After the regex extracts the host segment, the result is passed through `beast::IP::Endpoint::from_string_checked` to strip the surrounding brackets from IPv6 addresses (e.g., `[::1]` becomes `::1`). Doing this via the IP endpoint parser means the bracket removal is validated rather than naïve substring manipulation.
- **Port overflow rejection**: Ports larger than 65535 cause `beast::lexicalCast` to return 0, and the function treats port 0 as a parse failure and returns `false`. This prevents silent misrouting to port 0.
- **Scheme normalization**: The scheme is converted to lowercase unconditionally, so callers can do case-insensitive scheme comparison without extra work.
- **Exception safety**: The regex match is wrapped in a bare `catch(...)` that returns `false`. This guards against `boost::regex` throwing on pathological input, which can happen with certain degenerate strings.

`parseUrl` is called in `RPCSub` (WebSocket subscription URLs) and `ValidatorSite` (validator list fetch URLs), making robustness to malformed user input essential.

### TOML Domain Validation — `isProperlyFormedTomlDomain`

`isProperlyFormedTomlDomain(std::string_view)` validates that a string looks like a plausible internet domain for the purpose of fetching TOML-based validator metadata. The header comment explicitly warns this function is **not** a strict domain validity check — it rejects obviously bad inputs but may also reject some valid internationalized domain names (IDNs). The regex in the `.cpp` enforces label-level rules (no leading/trailing hyphens, alphanumeric plus hyphen, 1–63 characters per label) and requires at least one dot with a 2–63 character alphabetic TLD. Length is gated first (4–128 characters) before regex evaluation to avoid unnecessary overhead. This function is used in `Config.cpp` and `Handshake.cpp` to validate `[validator_token]` domain fields before attempting TOML file fetches.

### Miscellaneous Helpers

`trim_whitespace(std::string)` takes its argument by value and delegates to `boost::trim` in place, returning the result. The by-value parameter communicates intent: the caller's string is not modified, but no extra copy is needed when passing a temporary.

`to_uint64(std::string const&)` wraps `beast::lexicalCastChecked` to return an `std::optional<uint64_t>`, converting the library's boolean-plus-out-parameter convention into a modern value-returning form.

---

## Design Notes

The mix of inline template functions and opaque declarations is deliberate: `strUnHex` is generic over iterator types and cannot live in a `.cpp`, while `parseUrl` and `isProperlyFormedTomlDomain` carry static `boost::regex` objects that are expensive to initialize and must be compiled once. The header's `#include` of `strHex.h` and `Blob.h` closes the encode/decode loop for callers who need both directions of hex conversion, and the `boost/format.hpp` include (present in the header but not actively used by any declared function) suggests this header accumulated dependencies over time rather than being designed from scratch.