# `include/xrpl/basics/base64.h`

This header exposes the XRPL ledger's standard-library-free Base64 codec. It is part of the `xrpl/basics` utility layer — a collection of low-level primitives that the rest of the stack depends on but that carry no ledger-specific semantics themselves.

## API Surface

The header declares three free functions in the `xrpl` namespace:

```cpp
std::string base64_encode(std::uint8_t const* data, std::size_t len);
std::string base64_encode(std::string const& s);   // inline convenience overload
std::string base64_decode(std::string_view data);
```

The primary `base64_encode` overload takes a raw byte pointer and a length, reflecting the reality that the callers — cryptographic subsystems, peer handshaking, manifest deserialisation — deal in raw binary buffers rather than `std::string`. The second overload is a thin inline forwarder that `reinterpret_cast`s a `std::string`'s data pointer, colocating the type-pun at the definition site so it doesn't leak into every call site.

`base64_decode` takes `std::string_view`, the right choice for a read-only text consumer: callers can pass a `std::string`, a string literal, or a substring view without forcing a copy.

## Implementation Details (from `src/libxrpl/basics/base64.cpp`)

The implementation is adapted from René Nyffenegger's public-domain codec (2004–2008) and lives inside an anonymous `base64` sub-namespace within `xrpl`, keeping its helper tables and low-level routines invisible to the public API.

The encode path pre-allocates the output string to `encoded_size(n) = 4 * ((n + 2) / 3)` bytes, writes directly into the string buffer, then calls `resize` a second time with the actual byte count returned by the inner `encode()` function. This double-resize idiom avoids an extra heap allocation while ensuring the string's `size()` is accurate after the fact.

The decode path does the same trick against `decoded_size(n) = ((n / 4) * 3) + 2`. The slight over-allocation (`+2`) is intentional: it accommodates the worst-case padding without needing a branch, and `decoded_size` is only used for reservation. The inner `decode()` function returns a `std::pair<size_t, size_t>` — bytes written and input characters consumed — so the public `base64_decode` trims the string to the first element of the pair.

The inverse-table approach in `decode()` is a classic O(1) lookup: a 256-element `signed char` table maps every possible byte value to its 6-bit value or `-1` for invalid characters. When a `-1` is encountered, decoding stops immediately and returns whatever has been written so far. The test suite intentionally exercises this: `base64_decode("not_base64!!")` and `base64_decode("not")` produce identical output because both stop at the first character (`n`) that maps to a valid value and then halt on subsequent invalid ones.

This silent-truncation behavior is a deliberate pragmatic choice rather than an error-throwing design. The callers — manifest deserialisation, session-signature verification, validator token loading — handle malformed input at a higher level by checking the output length or passing the result through a cryptographic verifier that will reject garbage data.

## Usage in the Codebase

The two primary consumer categories are:

**Peer handshaking** (`src/xrpld/overlay/detail/Handshake.cpp`): When two rippled nodes establish an encrypted overlay connection, the initiating side base64-encodes the raw ECDSA session signature bytes into the `Session-Signature` HTTP header, and the receiving side decodes it back to bytes before passing them to `verifyDigest`. Base64 here is transport hygiene — HTTP headers are text, signatures are binary.

**Validator infrastructure** (`src/libxrpl/server/Manifest.cpp`, `ValidatorKeys.cpp`, `ValidatorList.cpp`): Validator manifests and revocations are serialised as binary blobs and then base64-encoded for embedding in configuration files and JSON RPC responses. The config parser strips whitespace from multi-line base64 blobs, concatenates them, and passes the result directly to `base64_decode`. The `RpcCall.cpp` and `ServerHandler.cpp` paths do the same for over-the-wire RPC payloads.

The codec is intentionally RFC 4648 standard (alphabet `A–Z a–z 0–9 + /`, `=` padding) rather than the URL-safe variant, matching the expectations of existing tooling and the external validator configuration format.