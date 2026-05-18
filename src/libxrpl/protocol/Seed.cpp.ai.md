# `src/libxrpl/protocol/Seed.cpp`

## Role and Purpose

This file implements the `Seed` class and the suite of factory and parsing functions that feed into XRPL's deterministic key-derivation pipeline. A seed is the 128-bit root secret from which both secp256k1 and ed25519 key pairs are derived via `generateSecretKey` / `generateKeyPair`. Every XRPL account or validator node traces back to one of these 16-byte values, making its construction and parsing the most security-sensitive path in the key management subsystem.

## The `Seed` Class

`Seed` is deliberately simple: a fixed-size `std::array<uint8_t, 16>` with no default constructor, no mutable accessors, and a destructor that calls `secure_erase`. The absence of a default constructor is a meaningful invariant — it prevents any code path from holding an uninitialized seed. The copy constructor and copy-assignment operator are explicitly defaulted, which is correct because copies of keys are sometimes intentional (e.g., passing through function boundaries), but the destructor guarantees every copy clears its buffer independently on destruction.

The two explicit constructors accept either a `Slice` (a non-owning span) or a `uint128`. Both validate size with `LogicError` rather than returning an error code or `std::optional`. This is intentional: callers constructing a `Seed` from an already-typed value (a decoded blob or a parsed integer) must have already verified the size before calling the constructor. A mismatched size is a programming error, not a user input error, so a hard abort through `LogicError` is appropriate.

## Secure Erasure

The destructor calls `secure_erase(buf_.data(), buf_.size())`, which in turn wraps OpenSSL's `OPENSSL_cleanse`. This is necessary because a naive `memset` or zeroing loop can be optimized away by the compiler when the buffer goes out of scope — `OPENSSL_cleanse` uses strategies specifically designed to resist dead-store elimination. In `generateSeed`, the same pattern is applied to the temporary stack buffer before returning: `randomSeed` fills a local `std::array<uint8_t, 16>`, constructs the `Seed` from it, and then calls `secure_erase` on the local array before returning the `Seed` by value. This prevents the raw entropy from lingering in the stack frame after the call returns.

`generateSeed` similarly uses `sha512_half_hasher_s` rather than the non-secure variant `sha512_half_hasher`. The `_s` variant is a template specialization of `basic_sha512_half_hasher<true>` that zeroes its internal SHA-512 state in its destructor. Without this, the passphrase's hash state would remain in stack memory after the hasher goes out of scope.

## Seed Generation

`randomSeed()` fills 16 bytes from `crypto_prng()`, XRPL's cryptographically secure pseudo-random number generator. `generateSeed(passPhrase)` computes the SHA-512 half (first 256 bits of SHA-512) of the passphrase and takes only the first 16 bytes of the 32-byte digest as the seed. This XRPL-specific algorithm is documented in `Seed.h`: the passphrase bytes are hashed without any normalization, null terminator, or length prefix. This deterministic derivation is intentional for usability — the well-known passphrase `"masterpassphrase"` always produces `snoPBrXtMeMyMHUVTgbuqAfg1SUTb`, as the test suite verifies. The obvious risk — weak passphrases map to weak seeds — is a known and accepted design tradeoff.

## Parsing: `parseBase58<Seed>` and `parseGenericSeed`

`parseBase58<Seed>` is a template specialization that decodes a Base58Check string tagged with `TokenType::FamilySeed`. The token type acts as a version byte in XRPL's Base58 encoding, so a string that decodes successfully but yields a size other than 16 bytes is rejected with `std::nullopt` rather than `LogicError`. This is correct: the caller supplied an externally-sourced string, so a mismatch is an input error rather than a logic violation.

`parseGenericSeed` is the most architecturally interesting function. It implements a cascading format-detection strategy across five possible representations:

1. **Rejection guard**: If the string decodes as a valid `AccountID`, `NodePublic`, `AccountPublic`, `NodePrivate`, or `AccountSecret`, it immediately returns `std::nullopt`. This is a critical safety check — without it, a valid node public key string could be silently reparsed as a seed via the passphrase hash fallback at the end of the function. This guard prevents key-type confusion attacks and accidental misuse in RPC calls.
2. **Hex**: Attempts to parse the string as a 128-bit hexadecimal value via `uint128::parseHex`.
3. **Base58**: Attempts `parseBase58<Seed>` for the standard `sXXXX` format.
4. **RFC1751** (optional, gated by the `rfc1751` parameter): Decodes a mnemonic English word sequence using `RFC1751::getKeyFromEnglish`. A subtlety here is byte-order: the RFC1751-decoded key string is constructed with reversed bytes (`key.rbegin(), key.rend()`), matching the reversal performed by `seedAs1751` when encoding. The `rfc1751` parameter defaults to `true` but is marked deprecated in the header comment, reflecting the format's age.
5. **Passphrase fallback**: Any string that passes none of the above is treated as a passphrase and hashed via `generateSeed`. This fallback is a significant compatibility concern — it means `parseGenericSeed` never returns `std::nullopt` for a non-empty string that isn't another key type. Callers who want strict parsing should use `parseBase58<Seed>` directly.

## `seedAs1751` and Byte-Order

`seedAs1751` encodes a seed as an RFC1751 mnemonic by first reversing the 16 seed bytes into a `std::string` via `std::reverse_copy`, then passing them to `RFC1751::getEnglishFromKey`. The reversal is paired with the corresponding reversal in `parseGenericSeed`'s RFC1751 decode path, forming a symmetric encode/decode pair. This byte reversal is not documented inline and is easy to miss — it appears to be a historical artifact of how XRPL originally adopted RFC1751, where the endianness convention differed from the RFC's standard interpretation.

## Error Handling Philosophy

The file uses two distinct error modes that map cleanly to their contexts. Constructors use `LogicError` (a hard abort) because size invariants on already-typed values are programmer errors. All parsing functions return `std::optional<Seed>` (soft failure) because they consume unvalidated external strings. This boundary is precise and consistent throughout the implementation.