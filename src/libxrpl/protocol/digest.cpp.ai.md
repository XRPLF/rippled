# `src/libxrpl/protocol/digest.cpp`

## Role in the System

This file provides the concrete implementations of XRPL's three foundational cryptographic hashers: RIPEMD-160, SHA-256, and SHA-512. These primitives underpin nearly every cryptographic operation in the ledger — from deriving account IDs to computing transaction hashes and validating signatures. The implementation lives in `libxrpl` rather than application code because these hashers are part of the protocol's public API surface, shared across both the server and any downstream SDK consumers.

## The Opaque-Buffer Design

The most architecturally significant detail here is not in the `.cpp` at all — it is the private member declaration in the header. Each struct stores its OpenSSL context in a raw `char ctx_[]` array (96 bytes for `RIPEMD160_CTX`, 216 bytes for `SHA512_CTX`, 112 bytes for `SHA256_CTX`) rather than directly declaring the OpenSSL type. This is a deliberate header-isolation technique: the public `digest.h` header does not need to `#include` any OpenSSL header, keeping OpenSSL a private dependency of the library rather than a transitive include for every file that computes a hash.

The cost of this design is that the `.cpp` must recover the real OpenSSL type through `reinterpret_cast`:

```cpp
auto const ctx = reinterpret_cast<RIPEMD160_CTX*>(ctx_);
```

The `reinterpret_cast` is only safe if `ctx_` is exactly the right size and alignment for the target type. Each constructor therefore opens with a `static_assert`:

```cpp
static_assert(sizeof(decltype(openssl_ripemd160_hasher::ctx_)) == sizeof(RIPEMD160_CTX), "");
```

This is a compile-time firewall: if an OpenSSL upgrade ever changes the size of `RIPEMD160_CTX`, `SHA512_CTX`, or `SHA256_CTX`, the build breaks loudly at the only point in the code where the cast is made, rather than silently corrupting memory at runtime. The sizes are hardcoded in the header (`char ctx_[96]{}`) and must be kept in sync with the OpenSSL ABI — the `static_assert` enforces that invariant without requiring OpenSSL headers in `digest.h`.

## The Hasher Interface Contract

The three classes follow the **N3980 `Hasher` concept** ("Types Don't Know #"), a C++ proposal for a uniform hashing interface. Each class exposes:

- `operator()(void const* data, std::size_t size) noexcept` — feeds a chunk of bytes into the running digest.
- `explicit operator result_type() noexcept` — finalises the digest and returns it as a fixed-size `std::array<uint8_t, N>`.
- A `static constexpr endian` field indicating byte-order expectations for the `hash_append` machinery.

The `noexcept` on both operators is intentional: OpenSSL's low-level `*_Update` and `*_Final` functions do not throw, and propagating exceptions from a hashing call site would be both surprising and unnecessary. Callers relying on this interface in `beast::hash_append` chains can therefore hash safely inside destructors or other `noexcept` contexts.

## Composed Hashers Defined in the Header

The `.cpp` only covers the three primitives, but `digest.h` builds two higher-level hashers directly on top of them:

**`ripesha_hasher`** chains SHA-256 into RIPEMD-160. Its `operator result_type()` calls `sha256_hasher`'s conversion operator to get the 32-byte intermediate digest, then feeds that into a fresh `ripemd160_hasher` and finalises it. This RIPEMD-160(SHA-256(*)) construction is exactly how XRPL derives the 160-bit account identifier from a public key — it applies regardless of whether the key is secp256k1 or Ed25519.

**`basic_sha512_half_hasher<Secure>`** wraps `sha512_hasher` and truncates its 64-byte output to the first 32 bytes, producing the `uint256` type that XRPL calls the *SHA-512 Half*. This is used pervasively for ledger object IDs, transaction hashes, and signing digests. The `Secure` template parameter controls whether the destructor calls `secure_erase` on the internal `sha512_hasher` state — the `sha512_half_hasher_s` alias selects the secure variant for key-material contexts where leaving hash state in memory is a security risk.

The convenience function `sha512Half(args...)` (and its secure twin `sha512Half_s`) rounds out the API: it constructs a `sha512_half_hasher`, feeds all arguments through `beast::hash_append`, and returns the resulting `uint256`.

## Error Handling and Resource Management

There is no dynamic allocation and no explicit error handling for OpenSSL failures. The OpenSSL `*_Init`, `*_Update`, and `*_Final` functions operate entirely on stack-allocated context structs and return error codes that this wrapper ignores. In practice these calls are infallible for valid, correctly-sized context buffers — the only realistic failure mode (a corrupt context) is already precluded by the `static_assert` size check at construction. No RAII guard beyond the `static_assert` is needed, since `char ctx_[N]{}` is zero-initialised at construction and lives on the stack for the duration of the hash operation.