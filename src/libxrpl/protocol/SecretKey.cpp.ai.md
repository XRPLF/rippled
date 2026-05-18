# `SecretKey.cpp` — Cryptographic Key Generation, Derivation, and Signing

## Role in the System

This file is the core cryptographic engine for the XRP Ledger's key management. It implements everything needed to go from a raw seed or randomness to usable signing keys: secret key construction, deterministic key derivation for both `secp256k1` and `ed25519`, public key derivation, message signing, and Base58-encoded key parsing. It sits at the lowest layer of the `libxrpl` protocol stack, and most higher-level wallet, transaction-submission, and account-management code ultimately calls into this file.

## `SecretKey`: Lifecycle and Intentional Restrictions

`SecretKey` stores exactly 32 bytes in a plain `uint8_t buf_[]`. Two design choices stand out immediately. First, the destructor unconditionally calls `secure_erase(buf_, sizeof(buf_))`, which attempts to overwrite key material before the memory is released. This guards against secret bytes lingering in freed pages or stack frames and is applied consistently to all intermediate key-material buffers throughout the file.

Second, `operator==`, `operator!=`, and `operator<<` are explicitly deleted. The equality deletions prevent callers from comparing secret keys in ways that might leak timing information. The absence of `operator<<` closes off accidental logging — a developer cannot inadvertently stream a `SecretKey` to a logger or output stream. `to_string()` exists as an intentional, explicit escape hatch, named in a way that signals conscious intent.

## XRPL's Custom secp256k1 Key Derivation

The XRPL predates BIP-32 and uses its own deterministic derivation algorithm, implemented through the `detail::Generator` class and the `detail::deriveDeterministicRootKey` helper.

`deriveDeterministicRootKey` accepts a 128-bit `Seed` and computes a valid secp256k1 scalar by hashing the seed concatenated with a big-endian 32-bit counter: `sha512Half(seed[0..15] || seq[0..3])`. The result is validated via `secp256k1_ec_seckey_verify`, which checks that it is nonzero and less than the curve's group order. Failure (statistically negligible — fewer than one in `2^128` seeds) causes a retry up to 128 times, after which a `std::runtime_error` is thrown. The intermediate buffer is always `secure_erase`d regardless of outcome.

`Generator` builds on this root key to produce a whole *family* of key pairs. During construction, it derives the compressed 33-byte root public key. For each ordinal, `calculateTweak` hashes `(rootPublicKey || ordinal || subseq)` to produce a scalar tweak. The final secret key is computed as `root + tweak (mod n)` using `secp256k1_ec_seckey_tweak_add`. This mirrors a simplified form of BIP-32 child key derivation but is XRPL-specific and incompatible with standard HD wallets. The comment in the source explicitly warns implementers: third-party tools do not need to replicate this derivation, but should support it if they need to import existing XRPL accounts.

`generateKeyPair` uses `Generator` for secp256k1, always requesting ordinal 0, which is the single-key case used almost universally. The generator pattern exists to support the older "family" concept where multiple addresses could be derived from one seed.

## Ed25519 Key Derivation

Ed25519 derivation is dramatically simpler. `generateSecretKey` for `KeyType::ed25519` simply computes `sha512Half_s(seed)` — the `_s` suffix indicates the variant that uses a `Slice` directly. There is no counter loop, no curve-order validation (Ed25519's scalar space is much larger relative to the hash output), and no multi-level structure. The public key is computed by `ed25519_publickey(sk.data(), &buf[1])` and prefixed with the byte `0xED` at position zero. This one-byte prefix is how the XRPL distinguishes compressed secp256k1 public keys (33 bytes, starting with `0x02` or `0x03`) from Ed25519 keys (also 33 bytes on the wire, but starting with `0xED`).

## Signing

The `sign` function dispatches on `KeyType`. For `ed25519`, it calls `ed25519_sign` directly on the raw message bytes — by design, Ed25519 hashes the message internally, and its security properties depend on that specific hash. Bypassing the internal hash (as `signDigest` does for secp256k1) is not supported for Ed25519. The header comment for `signDigest` makes this constraint explicit.

For secp256k1, `sign` applies `SHA-512/Half` to the message before calling into libsecp256k1, producing a `uint256` digest that is then passed to `secp256k1_ecdsa_sign`. Both `sign` and `signDigest` use `secp256k1_nonce_function_rfc6979` as the nonce function. RFC 6979 deterministic nonce generation is a critical choice: it eliminates the risk of nonce reuse (which would catastrophically expose the private key) and makes signatures reproducible, which simplifies testing and auditing. The resulting DER-encoded signature is returned in a `Buffer` of up to 72 bytes.

## The secp256k1 Context

`secp256k1Context()` is a function template in `detail/secp256k1.h` that returns a pointer to a `static`-local `secp256k1_context*` initialized with both `SECP256K1_CONTEXT_VERIFY` and `SECP256K1_CONTEXT_SIGN` flags. In C++11 and later, static-local initialization is thread-safe, so this effectively provides a lazily initialized, process-global secp256k1 context with no locking overhead after first use. This is appropriate for a library where the context never changes after startup.

## Random Key Generation

`randomSecretKey` fills a 32-byte stack buffer with output from `crypto_prng()` (the CSPRNG) via `beast::rngfill`, wraps it in a `SecretKey`, and then `secure_erase`s the stack buffer. The CSPRNG itself is a separate concern handled by `xrpl/crypto/csprng.h`. `randomKeyPair` simply combines `randomSecretKey` with `derivePublicKey` — the random path does not use the deterministic `Generator` at all, making it suitable for one-off key generation where wallet recovery from a seed is not needed.

## Base58 Parsing

`parseBase58<SecretKey>` is a template specialization that decodes a Base58-encoded token (typically a `TokenType::FamilySeed` or similar) and validates that the decoded payload is exactly 32 bytes. This is the entry point for loading persisted or user-supplied secret keys from their wire representation.