# `AccountID.cpp` — Account Identity Encoding, Derivation, and Caching

This file implements the core operations on `AccountID`, the 160-bit identifier that represents an XRPL account. It bridges the cryptographic world (derivation from a public key) and the user-facing world (the familiar `r...` base58 address strings), while providing an optional performance cache that avoids redundant SHA-256 work during hot-path ledger processing.

## The `AccountID` Type

`AccountID` is a type alias for `base_uint<160, detail::AccountIDTag>` — a 20-byte fixed-size integer with a tag type for type safety. The tag prevents accidental interoperability with other 160-bit values in the codebase. The `.cpp` file provides the runtime behavior for this type; the header declares the interface and keeps the tag definition.

## `calcAccountID` — Why SHA-256 then RIPEMD-160?

The function `calcAccountID(PublicKey const& pk)` derives an account identifier by feeding the public key bytes through `ripesha_hasher`, which internally runs SHA-256 then RIPEMD-160. The source comment (quoting David Schwartz) explains that this is a deliberate choice to match Bitcoin's address derivation scheme, for two reasons:

1. A double-hash prevents length-extension attacks that could be possible with a single SHA-256 output.
2. RIPEMD-160 is considered secure at 160 bits, whereas simply truncating SHA-512 or SHA-256 to 160 bits is a less well-analyzed approach.

The comment is candid: "The historical reason was that in the very early days, we wanted to give people as few ways to argue that we were less secure than Bitcoin." This is a conservative engineering choice that avoids reopening a security debate, not a claim that the alternative schemes would be insecure.

The `static_assert` before the implementation confirms that `AccountID::bytes` equals the size of `ripesha_hasher::result_type`, making the cast on the return line safe and self-documented.

## `AccountIdCache` — High-Performance Direct-Mapped Cache

Converting an `AccountID` to a base58 string requires a SHA-256 hash operation (to compute the checksum), which is non-trivial. In a validator processing thousands of transactions per second, the same account IDs appear repeatedly. The `AccountIdCache` inside `namespace detail` is a direct-mapped cache designed to amortize this cost.

The cache is a flat `std::vector<CachedAccountID>` of fixed size, where each slot holds an `AccountID` (20 bytes) and a `char[40]` encoding buffer. Crucially, no slot performs heap allocation — the encoding is stored inline. An XRPL base58 account address is always at most 34 characters plus a null terminator; the buffer is sized to 40 bytes with a comment and `XRPL_ASSERT` in `toBase58` confirming that the encoding never exceeds 38 characters.

Cache lookup uses a `hardened_hash<>` to map an `AccountID` to a slot index. The `hardened_hash` uses xxHash seeded with a random value at startup, which prevents adversarial inputs from causing systematic hash collisions (a denial-of-service vector if a predictable hash were used).

### Packed Spinlock Sharding

Rather than a single mutex for the entire cache, `AccountIdCache` uses a single `std::atomic<std::uint64_t> locks_` that encodes 64 independent spinlocks via `packed_spinlock`. Each cache slot acquires one of 64 spinlocks determined by `index % 64`. This is a space-efficient form of lock sharding: 64 distinct cache slots can be written concurrently without blocking each other, while the entire lock state fits in a single 64-bit word that lives on one cache line.

The `packed_spinlock` implementation uses `fetch_or` with `memory_order_acquire` to claim a bit and `fetch_and` with `memory_order_release` to release it — standard acquire/release semantics for a spinlock. It also calls `detail::spin_pause()` (`_mm_pause` on x86, `yield` on AArch64) during the spin loop to avoid pipeline misprediction penalties from tight compare-branch loops.

### The All-Zero Account Edge Case

The cache check `cache_[index].encoding[0] != 0 && cache_[index].id == id` handles a subtle initialization problem. A freshly default-constructed `CachedAccountID` has all bytes zeroed, including both the `id` field and the `encoding` field. The `AccountID` for XRP (the special `xrpAccount()` sentinel) is also all zeros. Without the `encoding[0] != 0` guard, an uninitialized slot whose `id` happens to be zero would appear to be a cache hit for `xrpAccount()`, returning an empty string instead of the correct base58 encoding. The first-byte check is an elegant way to distinguish "never written" from "legitimately cached the all-zero account."

## Special Sentinel Accounts

`xrpAccount()` returns a statically initialized `AccountID` of `beast::zero` (all bits zero). It serves as the canonical issuer for native XRP — XRP balances use this value in the issuer field where a real account address would otherwise appear.

`noAccount()` returns `AccountID(1)` — a value that cannot be derived from any real public key and is used as a placeholder when no account is applicable. Both are returned as `const&` to static locals, the standard Meyer's singleton pattern.

## Cache Initialization and Singleton Lifecycle

`initAccountIdCache(std::size_t count)` is intentionally one-shot: the `if (!accountIdCache && count != 0)` guard means only the first call has any effect. The caller at application startup decides the cache size; later calls (e.g., from tests or reconfiguration code) are silently ignored. Passing `count == 0` disables the cache entirely. The `toBase58` free function checks for the cache's existence and falls through to the uncached `encodeBase58Token` path if it was never initialized — so the cache is entirely optional and the API contract is unchanged without it.

## `parseBase58` and `to_issuer`

The `parseBase58<AccountID>` specialization decodes a base58 string via `decodeBase58Token`, then validates that the result is exactly 20 bytes (`AccountID::bytes`). Any mismatch — wrong token type prefix, invalid characters, wrong length — causes `decodeBase58Token` to return an empty or wrong-sized result, and the function returns `std::nullopt`. There are no exceptions in this path.

`to_issuer()` is a legacy dual-format parser: it first tries to interpret the string as raw hex (via `parseHex`), then falls back to base58. It is marked `DEPRECATED` in the header, since newer code should prefer the explicit `parseBase58<AccountID>` path and avoid mixing formats at the call site.