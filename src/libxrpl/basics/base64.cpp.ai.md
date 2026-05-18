# `src/libxrpl/basics/base64.cpp`

This file implements standard Base64 encoding and decoding (RFC 4648) for the XRPL ledger library. It is derived from René Nyffenegger's public domain implementation (2004–2008) with modifications to fit the `xrpl` namespace and C++ idiom conventions. The codec is used widely throughout the ledger: most visibly in the peer overlay handshake (`Handshake.cpp`), where node session signatures are Base64-encoded into HTTP headers, and in validator list and manifest handling.

## Two-Layer API Design

The file exposes two layers. The inner `xrpl::base64` anonymous-ish namespace contains low-level, buffer-oriented primitives (`encode`, `decode`), while the outer `xrpl` namespace exposes the caller-facing `base64_encode` / `base64_decode` functions that manage `std::string` memory automatically. This split allows the hot path to avoid heap allocation entirely when callers can supply their own pre-allocated buffers — the inner functions work on raw `void*`/`char*` pointers and return byte counts rather than strings.

## Static Lookup Tables

Two `constexpr` tables drive the codec. `get_alphabet()` returns the 64-character Base64 alphabet (`A–Z`, `a–z`, `0–9`, `+`, `/`), stored as a function-local `static constexpr` array and returned as a pointer. `get_inverse()` returns a 256-element `signed char` table mapping every possible byte value to its 6-bit Base64 value, or `-1` for characters that are not part of the alphabet. Storing the inverse as a flat 256-entry array makes character validation and value extraction a single array index — O(1) with no branching per character.

## Encoding

`encode()` processes input three bytes at a time, fanning each group of 24 bits out into four 6-bit indices into the alphabet table. The main loop handles `len / 3` full groups. The `switch (len % 3)` tail handles the one- or two-byte remainder with appropriate `=` padding. The `// NOLINTNEXTLINE(bugprone-switch-missing-default-case)` suppression is intentional: `len % 3` can only be 0, 1, or 2, so there is no missing case — the suppression documents that the omission is deliberate rather than an oversight.

`encoded_size(n)` computes the exact output size as `4 * ((n + 2) / 3)`, which is the ceiling-of-thirds multiplied by four — this is always precise.

## Decoding

`decode()` reads one Base64 character at a time, accumulates four 6-bit values into `c4[]`, then recombines them into three bytes in `c3[]`. The loop exits on three conditions: input exhausted, a `=` padding character encountered, or an invalid character (one that maps to `-1` in the inverse table). When the loop exits with a partial group of 1–3 valid characters, the post-loop block reconstructs as many bytes as the partial group can produce (`i - 1` bytes, since a single accumulated Base64 character encodes no complete output byte by itself).

`decoded_size(n)` deliberately returns an *upper bound* — `(n / 4) * 3 + 2` — not an exact count. The `+2` ensures the caller's pre-allocated buffer is always large enough regardless of padding or partial trailing groups. The actual number of bytes written is returned in the first element of the `std::pair<std::size_t, std::size_t>`, and the second element reports how many input characters were consumed (useful if the caller wants to detect where decoding stopped).

## Public Wrappers and Memory Management

`base64_encode` and `base64_decode` follow a two-phase resize pattern common in the codebase: pre-allocate to the conservative maximum (`encoded_size` or `decoded_size`), invoke the low-level function, then `resize()` the string down to the actual byte count. This avoids a separate size-calculation pass while ensuring no reallocation occurs during the fill.

## Error Handling Posture

There is no exception thrown on invalid input. `decode` silently stops at the first unrecognized character and returns whatever was successfully decoded up to that point. The unit test in `src/tests/libxrpl/basics/base64.cpp` explicitly validates this: `base64_decode("not_base64!!")` must equal `base64_decode("not")`, confirming that the `_` and `!` characters both trigger early termination. Callers that need to distinguish successful from partial decodes must compare the returned length against the expected output size themselves; the API provides no status enum or error flag.

There are no concurrency concerns — all mutable state is function-local, and the lookup tables are `constexpr`, making the functions fully re-entrant.