# `include/xrpl/protocol/digest.h` — Cryptographic Digest Primitives

This header is the single authoritative source of cryptographic hash types for the XRPL protocol layer. It defines the concrete hasher structs used everywhere ledger object identifiers, transaction IDs, account addresses, and signing payloads are computed. Every type here is modeled to satisfy the `Hasher` concept from N3980 ("Types Don't Know #"), which is the `hash_append` interface used throughout the `beast` utility layer.

## The Hasher Contract

All structs in this file conform to the same interface pattern: a `static constexpr endian` member, a `result_type` typedef, an `operator()(void const*, size_t) noexcept` for feeding data, and an `explicit operator result_type() noexcept` for extracting the digest. This uniformity is not cosmetic — it allows `beast::hash_append` to drive any of these hashers generically, and it's what makes `sha512Half()` a single variadic template rather than dozens of overloads.

## OpenSSL-Backed Primitives

`openssl_ripemd160_hasher`, `openssl_sha256_hasher`, and `openssl_sha512_hasher` are thin wrappers around the corresponding OpenSSL EVP context state. The design choice to store the context as an opaque `char` array of fixed size (`char ctx_[96]`, `char ctx_[112]`, `char ctx_[216]`) is deliberate: it avoids including any OpenSSL headers in this widely-included protocol header, while still allocating context storage inline on the stack without heap overhead. The sizes correspond to the actual sizes of `EVP_MD_CTX` (or equivalent internal structs) for each algorithm. All three declare `endian = boost::endian::order::native`, since raw byte feeds don't carry endian meaning at the OpenSSL level.

Type aliases `ripemd160_hasher`, `sha256_hasher`, and `sha512_hasher` abstract away the `openssl_` prefix. This naming layer exists so that an alternative implementation (e.g., a different crypto library) could be swapped in by changing only the alias definitions, leaving all call sites untouched.

## `ripesha_hasher` — Account ID Derivation

`ripesha_hasher` computes SHA-256 over the input, then RIPEMD-160 over that SHA-256 digest — the classic Bitcoin-lineage construction used in XRPL to derive a 160-bit account identifier from a public key. The comment in the struct explicitly states the design goal: account IDs are algorithm-agnostic. Whether the underlying key is secp256k1 or ed25519, the same `ripesha_hasher` formula yields the `AccountID`. This insulates the account identifier from the cryptographic scheme, allowing future key types to be added without changing the address format.

Internally `ripesha_hasher` composes the two underlying hashers: data feeds into a `sha256_hasher`, and only when `operator result_type()` is called does it finalize SHA-256 and immediately feed the result into a fresh `ripemd160_hasher`. No intermediate buffer escapes the function.

## `basic_sha512_half_hasher<bool Secure>` — The Workhorse Hasher

SHA-512-Half is the dominant hash construction in XRPL's protocol layer. It computes a full SHA-512 digest and returns only the first 256 bits as a `uint256`. The rationale for truncating SHA-512 rather than using SHA-256 directly is performance: SHA-512 is faster than SHA-256 on 64-bit hardware due to wider register operations, and 256 bits of output from SHA-512 still provides strong security guarantees.

A critical difference from the OpenSSL wrappers is that `basic_sha512_half_hasher` declares `endian = boost::endian::order::big`. This matters when `hash_append` serializes multi-byte integers before feeding them to the hasher — big-endian is the canonical on-wire byte order for XRPL, matching the protocol's network representation.

The `bool Secure` template parameter controls whether the destructor zeros internal state via `secure_erase()`. This is implemented via two overloads of a private `erase()` method dispatched through `std::integral_constant<bool, Secure>{}`. When `Secure = false`, `erase(std::false_type)` is an empty inline no-op with zero overhead. When `Secure = true`, `erase(std::true_type)` calls `secure_erase(&h_, sizeof(h_))` to clear the embedded SHA-512 context — important when the hasher has processed sensitive key material that must not linger in memory.

Two public aliases expose this:
- `sha512_half_hasher` — the standard fast variant, used for most ledger computations.
- `sha512_half_hasher_s` — the secure variant, used when hashing private keys or seed material.

## `sha512Half()` and `sha512Half_s()` — Convenience Entry Points

These variadic function templates are the primary call sites in the rest of the codebase. They construct a `sha512_half_hasher` (or `_s`), drive it with `beast::hash_append` over all supplied arguments, and return the `uint256` result. Because `hash_append` is overloaded for all XRPL protocol types (including `HashPrefix`, `STObject`, `Serializer`, and primitive integers), a single `sha512Half(HashPrefix::transactionID, txSerializer)` call correctly serializes and hashes a complete transaction object. The `HashPrefix` enum (defined in `HashPrefix.h`) uses this mechanism to enforce domain separation — every distinct hash purpose (transaction IDs, ledger node hashes, signing payloads, manifests, payment channel claims) is distinguished by prepending a unique 4-byte prefix, preventing cross-context hash collisions even when the underlying data bytes are identical.