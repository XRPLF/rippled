# Cryptography

XRPL supports secp256k1 (ECDSA) and ed25519 key types. All crypto uses OpenSSL + dedicated libs (libsecp256k1, ed25519-donna). The `xrpl::crypto` layer provides three foundational utilities — a CSPRNG, secure memory erasure, and RFC 1751 mnemonic encoding — that underpin all key/seed handling.

## Module Layout

Three small, focused TUs form the foundation; protocol-level types (`SecretKey`, `PublicKey`, `Seed`) in `src/libxrpl/protocol/` consume them.

| File | Purpose |
|------|---------|
| `include/xrpl/crypto/csprng.h` / `src/libxrpl/crypto/csprng.cpp` | `csprng_engine` + `crypto_prng()` singleton; wraps OpenSSL `RAND_bytes`/`RAND_add`/`RAND_poll` |
| `include/xrpl/crypto/secure_erase.h` / `src/libxrpl/crypto/secure_erase.cpp` | One-line delegation to `OPENSSL_cleanse`; canonical wipe primitive |
| `include/xrpl/crypto/RFC1751.h` / `src/libxrpl/crypto/RFC1751.cpp` | Static class; 2048-word mnemonic codec + `getWordFromBlob` utility |

## Key Invariants

- `SecretKey` and `Seed` destructors call `secure_erase` on their internal buffer as the very first action; any new sensitive type must follow this pattern (covers exception unwind paths too)
- ed25519 public keys are prefixed with `0xED` (33 bytes total); secp256k1 keys are 33-byte compressed
- `sha512Half` (first 32 bytes of SHA-512) is the standard hash used throughout XRPL for node hashing, signing, etc.
- `RIPEMD-160(SHA-256(x))` is used for account ID derivation (`ripesha_hasher`)
- Base58 encoding includes a type byte prefix and 4-byte checksum (double SHA-256)
- All randomness for cryptographic material flows through `crypto_prng()`; never call OpenSSL's `RAND_bytes` directly and never use `std::rand`/`rand()`
- `csprng_engine` is non-copyable and non-movable (deleted ops); the singleton must be accessed by reference via `crypto_prng()`
- `csprng_engine` satisfies the C++ *UniformRandomNumberEngine* named requirement (`result_type` = `std::uint64_t`, `operator()()`, `constexpr min()`/`max()`) — plugs into `std::uniform_int_distribution`, `beast::rngfill`, etc.
- RFC 1751 dictionary has exactly 2^11 = 2048 entries; indices 0–570 are words ≤ 3 chars, 571–2047 are exactly 4 chars (exploited in `wsrch` to halve binary search range)
- Each RFC 1751 word encodes exactly 11 bits; a 64-bit block uses 6 words (66 bits = 64 data + 2 parity); a 128-bit key uses two such blocks → 12 words total

## Common Bug Patterns

- Mixing up key types: secp256k1 signing hashes the message with `sha512Half` first; ed25519 signs the raw message
- `signDigest` only works with secp256k1; calling it with ed25519 throws a logic error
- Signature canonicality: ed25519 `verify` checks canonicality before calling `ed25519_sign_open`; non-canonical signatures are rejected
- Overlay handshake uses `signDigest` to sign the session fingerprint (`sharedValue`); the signature binds the TLS session to the node identity
- Relying on a naive `memset` to wipe key material — optimizer will eliminate it as a dead store; must use `secure_erase`
- Forgetting to wipe *intermediate* derivation buffers (SHA-512 halves, scratch arrays) after the final `SecretKey` has taken its copy
- Constructing a second `csprng_engine` instance: forbidden by deleted ctors; sharing one OpenSSL pool through the singleton is required
- Passing `mix_entropy` a buffer and assuming OpenSSL credits it as entropy — the entropy estimate passed to `RAND_add` is always `0` (deliberately conservative; `std::random_device` may be weak on some platforms)
- RFC 1751 decode: distinguish `1` (success), `0` (unknown word), `-1` (malformed input), `-2` (parity failure) — do not collapse all failures into a single error
- `insert()` in RFC 1751 uses bitwise OR, not assignment — output buffer must start zero-initialized
- Treating RFC 1751 parity as cryptographic integrity — it's a 2-bit transcription check, not a MAC
- Using `getWordFromBlob` for anything cryptographic — it's a Jenkins one-at-a-time hash and explicitly insecure

## Review Checklist

- New crypto code must use `crypto_prng()` singleton for randomness, never raw `rand()` or direct OpenSSL `RAND_*`
- Secret key buffers must be `secure_erase`d after use (destructors *and* intermediate scratch buffers)
- Verify that key type dispatch handles both secp256k1 and ed25519 (or explicitly rejects one with a clear error)
- Any new sensitive type should follow the `SecretKey`/`Seed` pattern: destructor calls `secure_erase` as its first/only action
- New OpenSSL touchpoints should respect the `OPENSSL_VERSION_NUMBER < 0x10100000L` thread-safety guard pattern used in `csprng.cpp`
- CSPRNG failures (`RAND_bytes`/`RAND_poll` ≠ 1) must propagate via `Throw<>` (logs stack trace) — never silently fall back
- `RAND_cleanup()` must only be called for OpenSSL `< 1.1.0`; modern versions handle cleanup via `atexit`

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

`secure_erase` delegates to `OPENSSL_cleanse`, which uses volatile writes / opaque function-pointer calls to defeat dead-store elimination. Lives in a separate TU (`secure_erase.cpp`) so the call site cannot inline it away — the out-of-line call forces the compiler to treat it as an opaque side effect. It does **not** clear CPU registers or caches — best-effort for heap/stack only (see Percival 2014). Takes raw `void*` + byte count with no null/zero guards; callers must supply valid arguments.

Wrapping behind `xrpl::secure_erase` provides one auditable choke point if the underlying strategy ever changes (e.g., switching to `explicit_bzero`). `OPENSSL_cleanse` is preferred over platform-specific alternatives (`memset_s`, `explicit_bzero`, `SecureZeroMemory`) because OpenSSL already centralizes cross-platform portability for the rest of the crypto stack.

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

Constructor calls `RAND_poll()` eagerly to surface entropy failures at startup rather than at first key gen. Failure throws `std::runtime_error("CSPRNG: Insufficient entropy")` via `Throw<>`; callers generally do not catch — propagation halts the operation, which is correct.

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
// rc: 1=success, 0=unknown word, -1=malformed, -2=parity mismatch
```

`Seed.cpp` reverses the 16 bytes before/after RFC 1751 encoding to match the RFC's big-endian convention. `standard()` normalizes input by uppercasing and applying visual substitutions `1→L`, `0→O`, `5→S` for handwritten/OCR tolerance. The 2-bit parity per 8-byte half is a transcription check, **not** a cryptographic integrity check.

`getKeyFromEnglish` uses `boost::algorithm::split` with `token_compress_on` for whitespace tolerance. Encoder (`getEnglishFromKey`) has no return code — encoding is lossless and cannot fail on valid 16-byte input. Decoder (`getKeyFromEnglish`) has a 4-valued return code — it must validate user-supplied strings.

`getWordFromBlob` is a separate utility: Jenkins one-at-a-time hash → `% 2048` → one dictionary word. Explicitly **not** cryptographically secure; used in `NetworkOPs.cpp` for `shroudedHostId` (privacy-preserving node label in logs/RPC). Reuses the RFC 1751 dictionary purely for its vetted set of short, pronounceable words.

## CSPRNG Internals

- Constructor calls `RAND_poll()` eagerly; destructor calls `RAND_cleanup()` only for OpenSSL `< 1.1.0` (modern versions clean up via `atexit`)
- Thread-safety mutex is compile-time gated: `#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || !defined(OPENSSL_THREADS)` — modern builds elide the lock on the hot path (`RAND_bytes` is internally thread-safe in OpenSSL ≥ 1.1.0). The mutex is *always* held around `RAND_add` in `mix_entropy` regardless of version
- `mix_entropy` reads 128 values from `std::random_device` *before* locking (independently thread-safe), then locks for `RAND_add`
- `mix_entropy` passes entropy estimate `0` to `RAND_add` — never claim entropy for `std::random_device` or caller-supplied buffers (conservative accounting prevents prematurely satisfying OpenSSL's seeding threshold)
- `mix_entropy` is called on a timer from `Application.cpp` to stir fresh OS entropy during the node's lifetime
- Singleton is a function-local `static` (Meyers singleton); C++11 guarantees thread-safe one-time init
- Scalar `operator()()` delegates to buffer-fill overload with `sizeof(result_type)` (8 bytes) — both paths share validation/error handling

## RFC 1751 Internals

- `extract(s, start, length)` / `insert(s, x, start, length)`: read/write `length ≤ 11` bits at arbitrary offset across a 9-byte buffer; guarded by `XRPL_ASSERT` (stripped in release). Both work across byte boundaries by assembling 2–3 adjacent bytes into a 24-bit window
- `insert` uses bitwise OR (not assignment) — output buffer must start zero-initialized; partial writes accumulate safely
- `btoe` appends a 9th byte for 2-bit parity computed by summing all 32 bit-pairs across the 64-bit payload; parity occupies bit positions 64–65
- `etob` validates: exactly 6 words, each 1–4 chars, all in dictionary, parity matches — distinct error codes per failure mode (`0` unknown, `-1` malformed, `-2` parity)
- `wsrch` halves the binary search range based on input word length: `[0, 571)` for ≤3-char words, `[571, 2048)` for 4-char words
- No exceptions used anywhere in RFC 1751 — all errors are integer return codes
- All methods are static; `RFC1751` is a pure stateless utility class — instantiation is never needed

## Key Files

- `include/xrpl/protocol/SecretKey.h` / `PublicKey.h` — key types
- `src/libxrpl/protocol/SecretKey.cpp` — signing, key generation; canonical example of CSPRNG + `secure_erase` discipline
- `src/libxrpl/protocol/PublicKey.cpp` — verification
- `src/libxrpl/protocol/Seed.cpp` — 128-bit seed; uses RFC 1751 for mnemonic encoding (reverses bytes for big-endian convention)
- `include/xrpl/protocol/digest.h` — hash functions (`sha512Half`, `ripesha_hasher`, etc.)
- `include/xrpl/crypto/csprng.h` + `src/libxrpl/crypto/csprng.cpp` — CSPRNG engine and singleton
- `include/xrpl/crypto/secure_erase.h` + `src/libxrpl/crypto/secure_erase.cpp` — memory wipe primitive
- `include/xrpl/crypto/RFC1751.h` + `src/libxrpl/crypto/RFC1751.cpp` — mnemonic codec
- `src/xrpld/overlay/detail/Handshake.cpp` — overlay handshake crypto
- `src/xrpld/app/main/Application.cpp` — periodic `mix_entropy` calls
- `src/xrpld/app/misc/NetworkOPs.cpp` — uses `getWordFromBlob` for `shroudedHostId`
