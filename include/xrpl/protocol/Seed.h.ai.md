# `include/xrpl/protocol/Seed.h` — Cryptographic Seed Abstraction

`Seed.h` defines the `Seed` class and its family of construction and encoding helpers. A seed in XRPL is the 128-bit secret from which all downstream key material — private keys, public keys, and account addresses — is deterministically derived. This file is the gateway between human-readable or randomly generated entropy and the protocol's signing infrastructure.

## The `Seed` Class

`Seed` wraps a `std::array<uint8_t, 16>` — exactly 128 bits — with two deliberate design constraints that reflect the sensitivity of the data it holds.

First, the default constructor is deleted (`Seed() = delete`). A zero seed is dangerous because it could be mistaken for valid entropy, so the class enforces that every `Seed` object is initialized from real material — either a `Slice` or a `uint128`. The constructor validates the size and raises a `LogicError` if mismatched, catching integration bugs at development time.

Second, the destructor calls `secure_erase()` on the internal buffer. The `secure_erase` implementation takes deliberate steps — including using volatile writes and memory barriers — to prevent the compiler from optimizing away the zeroing pass. The comment in `secure_erase.h` acknowledges honestly that this is best-effort: CPU caches and registers may still hold remnants, but heap/stack memory is overwritten. This mirrors industry practice (e.g., OpenSSL's `OPENSSL_cleanse`).

Copy construction and copy assignment are explicitly defaulted. This is intentional: seeds must be passable by value across function boundaries (e.g., into key-derivation functions), and preventing copies would force raw pointer passing, which is more error-prone. The tradeoff is that callers must be mindful of how many copies exist in memory at once.

Only read access is exposed externally: `data()` returns a `const uint8_t*`, and the iterator pair exposes `const_iterator` only. This prevents any code outside the class from accidentally mutating live key material.

## Construction Paths

`randomSeed()` fills a temporary local buffer using `beast::rngfill()` backed by `crypto_prng()` (the global CSPRNG), constructs a `Seed` from it, and then immediately calls `secure_erase()` on the local buffer before returning. This two-step pattern is important: the `Seed` destructor handles its own cleanup, but the transient staging buffer would otherwise linger on the stack until its scope ends — so it is explicitly zeroed first.

`generateSeed(passPhrase)` implements the XRPL-specific passphrase-to-seed algorithm: compute SHA-512-Half of the raw passphrase bytes (no null terminator), then take the first 128 bits. The SHA-512-Half (`sha512_half_hasher_s`) uses a streaming hasher that writes to a stack-local buffer. This function deliberately does not attempt to detect whether the string is hex or Base58 — that disambiguation is the job of `parseGenericSeed()`. The suffix `_s` on the hasher type indicates it performs a secure erase of internal state on destruction.

## Parsing: `parseGenericSeed()` and `parseBase58<Seed>()`

`parseBase58<Seed>` is a template specialization (the primary template lives in `tokens.h`) that decodes a Base58Check-encoded string carrying the `TokenType::FamilySeed` prefix byte (value 33). A successful decode must produce exactly 16 bytes; anything else returns `std::nullopt`.

`parseGenericSeed()` is the more interesting function. It operates as a cascading fallback parser designed to accept whatever format a caller might supply:

1. **Reject other key types first.** Before attempting any format, it checks whether the string parses successfully as an `AccountID`, `NodePublic`, `AccountPublic`, `NodePrivate`, or `AccountSecret`. If it does, `parseGenericSeed` returns `std::nullopt` — this is a deliberate security guard preventing accidental use of an address or public key as a seed.

2. **Try hex.** A 32-character hex string maps directly to a 128-bit seed via `uint128::parseHex()`.

3. **Try Base58 family seed.** Standard encoded XRPL wallet seeds (the "s…" strings in the XRPL alphabet).

4. **Try RFC1751 mnemonic** (when `rfc1751 = true`). RFC1751 encodes 64-bit keys as sequences of short English words. XRPL adopted this format early and maintains backward compatibility — note that the implementation reverses the byte order when converting between RFC1751 encoding and the internal buffer, which matches the historical XRPL convention.

5. **Treat as passphrase.** The ultimate fallback is to run `generateSeed(str)`, turning an arbitrary string into a deterministic seed via SHA-512-Half. This ensures `parseGenericSeed` never returns `std::nullopt` for non-empty input (unless the string was recognized as another key type).

## Encoding

`toBase58()` is an inline function that delegates to `encodeBase58Token(TokenType::FamilySeed, ...)`. Seeds always use the `FamilySeed` token type in XRPL's Base58Check scheme, which produces the well-known "s"-prefixed wallet seed strings displayed to users.

`seedAs1751()` encodes a seed in RFC1751 format. The implementation reverses the 16 bytes before passing them to `RFC1751::getEnglishFromKey()` — this byte-reversal is baked into the XRPL convention and must be matched symmetrically in `parseGenericSeed` when reading RFC1751 input. RFC1751 output is treated as deprecated; `parseGenericSeed` accepts it by default for backward compatibility but exposes an `rfc1751` flag so callers can disable the fallback when strict format enforcement is required.

## Relationship to Key Derivation

`Seed.h` is intentionally decoupled from the actual key-derivation step. The `Seed` object simply holds 128 bits; how those bits are used to derive a `SecretKey` depends on the key type (secp256k1 family-seed derivation, ed25519, etc.) and is handled in `SecretKey.h` / `PublicKey.h`. This separation means the seed representation is stable and format-agnostic even as cryptographic algorithms evolve.