# `src/libxrpl/basics/StringUtilities.cpp`

This file implements the five free functions declared in `StringUtilities.h` that constitute a focused toolkit for string and binary data manipulation across the XRPL codebase. The functions span three distinct concerns: safe binary-to-SQL encoding, full-featured URI parsing, and lightweight validation helpers. None of them belong to a class; they live directly in the `xrpl` namespace as utilities shared by ledger storage, networking, RPC, and TOML-based validator discovery.

## `sqlBlobLiteral` — SQLite Hex Encoding

`sqlBlobLiteral()` converts a `Blob` (`std::vector<unsigned char>`) into an SQLite *blob literal* of the form `X'AABBCC...'`. SQLite requires this encoding whenever binary data is embedded directly in a query string. The implementation pre-reserves `(blob.size() * 2) + 3` bytes — exactly the right capacity for the leading `X'`, the hex-encoded body, and the closing `'` — then delegates the actual hex expansion to `boost::algorithm::hex`. The `+3` is an often-missed detail that prevents a reallocation on the final `push_back('\'')`. This function appears in `AcceptedLedgerTx.cpp` and related ledger-persistence paths.

## `parseUrl` — URI Decomposition

`parseUrl()` is the most architecturally interesting function in the file. It decomposes a URL string into a `parsedURL` struct (defined in the header) carrying `scheme`, `username`, `password`, `domain`, `port`, and `path`. The function is deliberately strict: it only accepts URIs whose `hier-part` uses the `//authority` form (`scheme://...`). This is not accidental — the XRPL daemon needs to connect to WebSocket, HTTPS, and PostgreSQL endpoints and must not silently accept opaque URIs.

The regex is compiled once as a `static` local, which is important for performance given that `parseUrl` is called during config parsing, peer-handshake setup (see `Handshake.cpp`), RPC subscription setup (`RPCSub.cpp`), and validator site loading (`ValidatorSite.cpp`). The regex uses the `(?i)` inline flag for case-insensitive matching, which explains why the function then calls `boost::algorithm::to_lower` on the extracted scheme — the stored value is always lowercase regardless of what the caller supplied.

IPv6 address handling deserves special attention. The regex captures the host component into `smMatch[4]` using a pattern that can match bracketed IPv6 addresses like `[::1]`. The code then attempts `beast::IP::Endpoint::from_string_checked(domain)`: if that succeeds, the result's `address().to_string()` is used, which strips the surrounding brackets and normalizes the address. If it fails (e.g., for a plain hostname), the raw regex capture is kept as-is. This makes the function correctly round-trip IPv6 literals while remaining transparent to hostname strings.

Port validation layers two checks. `beast::lexicalCast<std::uint16_t>` converts the port string to an unsigned 16-bit integer; per the implementation notes in the source, it returns `0` for any input that doesn't fit in a `uint16_t` (e.g., `65536` or `23498765`). A subsequent explicit `if (pUrl.port == 0)` then rejects port zero as invalid. This means ports outside `[1, 65535]` all fail, which is the correct behavior for network endpoints.

The entire `boost::regex_match` call is wrapped in a bare `catch (...)` that returns `false`. This is a defensive pattern: Boost.Regex can throw `std::runtime_error` if a pathological input triggers an internal limit (which is why the test suite exercises a URL with 8,192 colons in the host).

## `trim_whitespace`

A thin wrapper around `boost::trim` that takes its argument by value and returns the trimmed copy. Taking by value rather than by reference is deliberate: the signature communicates that the caller gets a new string, avoids aliasing issues, and allows the compiler to elide a copy when the caller passes an rvalue.

## `to_uint64`

Converts a string to `std::optional<std::uint64_t>` using `beast::lexicalCastChecked`. Where the header's template `strUnHex` handles hex-encoded binary, `to_uint64` handles decimal integers — primarily used in configuration parsing where the caller needs to distinguish between `"0"` (valid) and `"abc"` (invalid). Returning `std::nullopt` on failure rather than throwing or returning a sentinel value like `-1` is idiomatic modern C++ and avoids the trap of treating `0` as an error.

## `isProperlyFormedTomlDomain`

This function validates domain strings found in XRPL TOML files, the mechanism by which validators advertise their identity. Two fast-path checks bound the length to `[4, 128]` characters before invoking the regex, avoiding unnecessary regex execution for obviously invalid inputs. The `static` regex (compiled with `boost::regex_constants::optimize`) enforces RFC-like hostname structure: each label must be `[a-zA-Z0-9-]{1,63}` with no leading or trailing hyphens, and the TLD must be pure alpha with at least two characters. The header comment is explicit that this function is not a full RFC 5891 validator — it rejects some valid edge cases (notably internationalized domain names) and does not check IANA TLD lists. Its purpose is to filter obviously malformed inputs during TOML validation, not to definitively resolve domains.

## Design Patterns and Tradeoffs

Both regexes are `static` locals, ensuring they are compiled at first call and shared across all subsequent invocations — avoiding the significant overhead of Boost.Regex compilation on every call. The `parsedURL::operator==` in the header ignores `username` and `password` in its equality comparison, which is intentional: two URLs pointing at the same endpoint are considered equivalent for connection-deduplication purposes regardless of credentials.