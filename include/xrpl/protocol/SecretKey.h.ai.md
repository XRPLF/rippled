# `include/xrpl/protocol/SecretKey.h`

This header is the gateway to XRPL's private-key cryptography. It declares the `SecretKey` value type and a suite of free functions for key generation, public-key derivation, and message signing. The two supported cryptosystems — secp256k1 and Ed25519 — share the same storage type but diverge in every algorithm detail, so this header carefully unifies them behind a common API while encoding their fundamental differences in the function signatures.

## The `SecretKey` Class

`SecretKey` is a thin, fixed-size wrapper around 32 raw bytes. Its design is shaped by two competing concerns: ergonomics (it should be copyable and passable like any value type) and security (the key material must never leak or be held longer than necessary).

The destructor is intentionally non-trivial: the implementation calls `secure_erase(buf_, sizeof(buf_))`, zeroing the backing array before the memory is released. This defends against cold-boot attacks and memory dumps. The same defensive zeroisation appears throughout the implementation: `randomSecretKey()` zeroes its stack buffer after constructing the `SecretKey`, and the intermediate derivation results in `generateSecretKey()` and `detail::Generator` are likewise wiped immediately after use.

The default constructor is deleted. A `SecretKey` must always be initialised with actual key material — either a 32-byte `std::array` or a `Slice`. The `Slice` constructor is strict: it throws a `LogicError` if the slice is not exactly 32 bytes, enforcing the invariant at construction time.

Comparison operators (`operator==` and `operator!=`) are deleted both as member functions and as free functions. This is a deliberate security decision: comparing secret keys in application code is almost always a mistake (you compare public keys or account IDs instead), and any implementation of comparison risks timing-observable branches that could leak the key material through a side channel.

`operator<<` is also intentionally absent. The comment in the header makes this explicit — streaming a secret key to a log or debug output is too easy an accident. `to_string()` exists for the rare legitimate case (e.g., CLI tooling), but the caller must explicitly request the hex representation.

## Signing

Two distinct signing entry points exist because the two cryptosystems have different security models around hashing.

`sign()` takes a raw `Slice` message. For secp256k1 it first hashes the message with SHA512-Half and signs the resulting 256-bit digest; for Ed25519 it passes the raw message bytes directly to the `ed25519_sign` primitive, which incorporates its own deterministic internal hashing. Allowing callers to pre-hash an Ed25519 message would be cryptographically unsound — Ed25519's security proof depends on how the message is hashed — so the function dispatches on key type and handles the difference internally.

`signDigest()` accepts a pre-computed `uint256` digest and is restricted to secp256k1. The signature is returned as a DER-encoded `Buffer` (up to 72 bytes). Both `sign()` and `signDigest()` have a two-argument convenience overload that accepts a `KeyType` instead of an explicit `PublicKey`, calling `derivePublicKey()` internally to identify the algorithm before signing. The ECDSA nonce is generated via RFC 6979 (`secp256k1_nonce_function_rfc6979`), making secp256k1 signatures deterministic and immune to the class of vulnerabilities caused by weak random nonces.

## Key Generation and Derivation

`randomSecretKey()` fills a 32-byte buffer from `crypto_prng()` (a CSPRNG), constructs the key, zeroes the temporary buffer, and returns. This is appropriate for Ed25519 keys or for secp256k1 keys that don't need to be re-derived from a seed.

`generateSecretKey(type, seed)` is deterministic:
- For **Ed25519**, the secret key is simply SHA512-Half of the 16-byte seed. The resulting 32 bytes are directly usable as an Ed25519 private scalar.
- For **secp256k1**, the function calls `detail::deriveDeterministicRootKey()`, which concatenates the seed with a big-endian 32-bit sequence counter, hashes with SHA512-Half, and retries with incrementing counter values until the result is a valid secp256k1 scalar (i.e., non-zero and less than the curve order). In practice this loop almost never executes more than once.

`generateKeyPair(type, seed)` is the main entry point for wallet-style key derivation:
- For **Ed25519** it is equivalent to calling `generateSecretKey` followed by `derivePublicKey`.
- For **secp256k1** it uses the `detail::Generator` class, which implements XRPL's custom two-level derivation. The generator derives a root private key from the seed, computes its compressed public key (the "generator point"), and then produces ordinal-0 output by computing a tweak via SHA512-Half of the generator concatenated with the ordinal and a sub-sequence counter, then adding that tweak to the root key with `secp256k1_ec_seckey_tweak_add`. This algorithm predates BIP-32 and is XRPL-specific; the comment in the implementation notes that third-party wallets are not required to implement it, but should to support account import.

`derivePublicKey(type, sk)` converts a secret key to its corresponding `PublicKey`. For secp256k1 it calls `secp256k1_ec_pubkey_create` and serialises the result as a 33-byte compressed point. For Ed25519 it prepends the constant byte `0xED` to the 32-byte Edwards curve public key, producing the 33-byte XRPL encoding that `publicKeyType()` uses to distinguish Ed25519 keys from secp256k1 keys at runtime.

## Serialisation

`toBase58(type, sk)` and the specialisation `parseBase58<SecretKey>(type, s)` provide the wire encoding. The `TokenType` argument (`AccountSecret`, `FamilySeed`, etc.) controls the version byte prepended during Base58Check encoding, consistent with the token system used throughout the XRPL protocol layer. `parseBase58` validates the decoded length and returns `std::nullopt` on any mismatch, never throwing.