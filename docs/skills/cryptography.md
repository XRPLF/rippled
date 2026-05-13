# Cryptography

XRPL supports secp256k1 (ECDSA) and ed25519 key types. All crypto uses OpenSSL + dedicated libs (libsecp256k1, ed25519-donna). The `xrpl::crypto` layer provides three foundational utilities — a CSPRNG, secure memory erasure, and RFC 1751 mnemonic encoding — that underpin all key/seed handling.

## Key Invariants

- `SecretKey` and `Seed` destructors call `secure_erase` on their internal buffer; any code handling secret keys/seeds must follow this pattern
- ed25519 public keys are prefixed with `0xED` (33 bytes total); secp256k1 keys are 33-byte compressed
- `sha512Half` (first 32 bytes of SHA-512) is the standard hash used throughout XRPL for node hashing, signing, etc.
- `RIPEMD-160(SHA-256(x))` is used for account ID derivation (`ripesha_hasher`)
- Base58 encoding includes a type byte prefix and 4-byte checksum (double SHA-256)
- All randomness for cryptographic material flows through `crypto_prng()`; never call OpenSSL's `RAND_bytes` directly and never use `std::rand`/`rand()`
- `csprng_engine` is non-copyable and non-movable by deleted ops; the singleton must be accessed by reference via `crypto_prng()`
- RFC 1751 dictionary has exactly 2^11 = 2048 entries; entries 0–570 are 1–3 char words, 571–2047 are exactly 4 chars (used to split binary search range in `wsrch`)

## Common Bug Patterns

- Mixing up key types: secp256k1 signing hashes the message with sha512Half first, ed25519 signs the raw message
- `signDigest` only works with secp256k1; calling it with ed25519 throws a logic error
- Signature canonicality: ed25519 `verify` checks signature canonicality before calling `ed25519_sign_open`; non-canonical signatures are rejected
- Overlay handshake uses `signDigest` to sign the session fingerprint (`sharedValue`); the signature binds the TLS session to the node identity
- Relying on a naive `memset` to wipe key material — optimizer will eliminate it as a dead store. Must use `secure_erase`
- Forgetting to wipe *intermediate* derivation buffers (SHA-512 halves, scratch arrays) after the final `SecretKey` has taken its copy
- Constructing a second `csprng_engine` instance: forbidden by deleted ctors; sharing one OpenSSL pool through the singleton is required
- Passing `mix_entropy` a buffer and assuming OpenSSL credits it as entropy — the entropy estimate is always 0 (deliberately conservative)
- RFC 1751 decode: distinguish `0` (unknown word), `-1` (malformed input), `-2` (parity failure) — don't collapse all failures into a single error

## Review Checklist

- New crypto code must use `crypto_prng()` singleton for randomness, never raw `rand()` or direct OpenSSL `RAND_*`
- Secret key buffers must be `secure_erase`d after use (destructors *and* intermediate scratch buffers)
- Verify that key type dispatch handles both secp256k1 and ed25519 (or explicitly rejects one with a clear error)
- Any new sensitive type should follow the `SecretKey`/`Seed` pattern: destructor calls `secure_erase` as its first/only action
- New OpenSSL touchpoints should respect the `OPENSSL_VERSION_NUMBER < 0x10100000L` thread-safety guard pattern used in `csprng.cpp`

## Key Patterns

### Secure Erasure
```cpp
// REQUIRED: destructor must erase secret material
SecretKey::~SecretKey()
{
    secure_erase(buf_, sizeof(buf_));
}

// REQUIRED: erase intermediate buffers after use
beast::rngfill(buf, sizeof(buf), crypto_prng());
SecretKey sk(Slice{buf, sizeof(buf)});
secure_erase(buf, sizeof(buf));  // MUST erase raw buffer
```

`secure_erase` delegates to `OPENSSL_cleanse`, which uses volatile writes / opaque function-pointer calls to defeat dead-store elimination. Lives in a separate TU (`secure_erase.cpp`) so the call site cannot inline it away. It does **not** clear CPU registers or caches — it is best-effort for heap/stack only (see Percival 2014).

### CSPRNG Usage
```cpp
// Singleton access; never copy/store by value
auto& rng = crypto_prng();

// Bulk fill — preferred for key material
std::uint8_t buf[32];
rng(buf, sizeof(buf));            // operator()(void*, size_t)

// Or via beast adapter satisfying UniformRandomNumberEngine
beast::rngfill(buf, sizeof(buf), crypto_prng());
```

`csprng_engine` satisfies the C++ *UniformRandomNumberEngine* named requirement, so it plugs directly into `std::uniform_int_distribution` and similar. Failure (insufficient entropy) throws `std::runtime_error` via `Throw<>`; callers generally do not catch — propagation halts the operation, which is correct.

### Key Type Dispatch
```cpp
// REQUIRED: handle both key types or explicitly reject
if (type == KeyType::ed25519)
{   /* ed25519 path */ }
else if (type == KeyType::secp256k1)
{   /* secp256k1 path */ }
else
    LogicError("unknown key type");  // MUST NOT fall through silently
```

### RFC 1751 Mnemonic Encoding
```cpp
// 16-byte (128-bit) seed <-> 12-word mnemonic
std::string words;
RFC1751::getEnglishFromKey(words, std::string{seedBytes, 16});

std::string roundTrip;
int rc = RFC1751::getKeyFromEnglish(roundTrip, words);
// rc == 1 success; 0 unknown word; -1 malformed; -2 parity mismatch
```

`Seed.cpp` reverses the 16 bytes before/after RFC 1751 encoding to match the RFC's big-endian convention. `standard()` normalizes input by uppercasing and applying visual substitutions `1→L`, `0→O`, `5→S` for handwritten/OCR tolerance. The 2-bit parity per 8-byte half is a transcription check, **not** a cryptographic integrity check.

`getWordFromBlob` is a separate utility: Jenkins one-at-a-time hash → `% 2048` → one dictionary word. Explicitly **not** cryptographically secure; used in `NetworkOPs.cpp` for `shroudedHostId` (privacy-preserving node label in logs/RPC).

## Module Layout

The `xrpl::crypto` foundation has three small, focused TUs:

| File | Purpose |
|------|---------|
| `csprng.cpp/.h` | `csprng_engine` + `crypto_prng()` singleton; wraps OpenSSL `RAND_bytes`/`RAND_add`/`RAND_poll` |
| `secure_erase.cpp/.h` | One-line delegation to `OPENSSL_cleanse`; the canonical wipe primitive |
| `RFC1751.cpp/.h` | Static class; 2048-word mnemonic codec + `getWordFromBlob` utility |

These three are used together by the protocol-level key/seed types (`SecretKey`, `PublicKey`, `Seed`) which live in `src/libxrpl/protocol/`.

### CSPRNG Internals Worth Knowing

- Constructor calls `RAND_poll()` eagerly to surface OS entropy failures at startup, not at first key gen
- Destructor calls `RAND_cleanup()` only for OpenSSL `< 1.1.0` (modern versions clean up via `atexit`)
- Thread-safety mutex is compile-time gated: `#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)` — modern builds elide the lock on the hot path because `RAND_bytes` is internally thread-safe
- `mix_entropy` always holds the mutex around `RAND_add`; reads from `std::random_device` happen before locking (independently thread-safe)
- `mix_entropy` passes entropy estimate `0` to `RAND_add` — never claim entropy for `std::random_device` or caller-supplied buffers (they may be weak on some platforms)
- Called on a timer from `Application.cpp` to stir fresh OS entropy during the node's lifetime
- Singleton is a function-local `static` (Meyers singleton); C++11 guarantees thread-safe one-time init

### RFC 1751 Internals Worth Knowing

- `extract(s, start, length)` / `insert(s, x, start, length)`: read/write `length ≤ 11` bits at arbitrary offset across a 9-byte buffer; guarded by `XRPL_ASSERT` (stripped in release)
- `insert` uses bitwise OR (not assignment), so the output buffer must start zero-initialized; partial writes accumulate safely
- `btoe` adds a 9th byte for the 2-bit parity computed by summing all 32 pairs of bits across the 64-bit payload; parity occupies bit positions 64–65
- `etob` validates: exactly 6 words, each 1–4 chars, all in dictionary, parity matches — distinct error codes per failure mode
- `getKeyFromEnglish` uses `boost::algorithm::split` with `token_compress_on` for whitespace tolerance

## Key Files

- `include/xrpl/protocol/SecretKey.h` / `PublicKey.h` — key types
- `src/libxrpl/protocol/SecretKey.cpp` — signing, key generation; canonical example of CSPRNG + `secure_erase` discipline
- `src/libxrpl/protocol/PublicKey.cpp` — verification
- `src/libxrpl/protocol/Seed.cpp` — 128-bit seed; uses RFC 1751 for mnemonic encoding
- `include/xrpl/protocol/digest.h` — hash functions (`sha512Half`, `ripesha_hasher`, etc.)
- `include/xrpl/crypto/csprng.h` + `src/libxrpl/crypto/csprng.cpp` — CSPRNG engine and singleton
- `include/xrpl/crypto/secure_erase.h` + `src/libxrpl/crypto/secure_erase.cpp` — memory wipe primitive
- `include/xrpl/crypto/RFC1751.h` + `src/libxrpl/crypto/RFC1751.cpp` — mnemonic codec
- `src/xrpld/overlay/detail/Handshake.cpp` — overlay handshake crypto
- `src/xrpld/app/main/Application.cpp` — periodic `mix_entropy` calls
- `src/xrpld/app/misc/NetworkOPs.cpp` — uses `getWordFromBlob` for `shroudedHostId`
