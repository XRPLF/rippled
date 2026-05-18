# `beast/hash/xxhasher.h` — XXH3-backed Hasher for the Beast Framework

## Role in the System

`xxhasher` is the default 64-bit hash engine powering XRPL's unordered containers. It sits at the base of a small but carefully designed hashing stack: `uhash<xxhasher>` is the `std`-compatible functor used for hash maps and sets throughout the codebase, and `hardened_hash` layers random seeding on top to resist hash-flooding attacks. The class wraps the [xxHash](https://github.com/Cyan4973/xxHash) library's XXH3 algorithm, chosen for its exceptional throughput and distribution quality.

## The Two-Path Design: Buffer vs. Streaming State

The most significant implementation decision is the split between a small fixed buffer and a lazily allocated streaming state. The class holds a 64-byte, cache-line-aligned internal buffer (`buffer_`, `alignas(64)`) and only allocates an `XXH3_state_t` on the heap when the total accumulated input exceeds that capacity.

When `operator()(key, len)` is called and the incoming data fits within the remaining write window, `updateHash()` simply `memcpy`s into the buffer and advances the write pointer — zero heap allocation, zero xxHash API call. Only when an update would overflow the buffer does `flushToState()` trigger: it lazily creates the `XXH3_state_t` via `allocState()`, initializes it (with or without seed), pushes whatever was already sitting in the read buffer into the streaming state, then processes the new oversized chunk.

This matters because the vast majority of types hashed by XRPL — integer keys, account IDs, small structs — produce only a handful of `hash_append` calls totalling well under 64 bytes. Those cases never touch the heap. The 64-byte alignment of `buffer_` is deliberate: it keeps the structure on a single cache line to avoid false sharing and to facilitate potential SIMD processing by XXH3 itself.

## Finalization and Idempotency

`operator result_type()` calls `retrieveHash()`, which branches on whether a streaming state was ever allocated. If not (the fast path for small inputs), it calls `XXH3_64bits` or `XXH3_64bits_withSeed` directly on the buffered bytes — a single-shot hash of the read buffer with no state overhead. If a streaming state exists, it flushes any remaining buffer content into the state and calls `XXH3_64bits_digest`.

A subtlety: the tests explicitly verify that calling `operator result_type()` twice in succession returns the same value. Tracing through `flushToState(nullptr, 0)` reveals why: after flushing the read buffer into the streaming state, `resetBuffers()` resets both spans — so the read buffer becomes empty, and a second `flushToState(nullptr, 0)` call simply pushes zero bytes into the already-updated state before `XXH3_64bits_digest` is called again, producing the same digest. This idempotency is not accidental; the test `testOperatorResultTypeDoesNotChangeInternalState` guards it explicitly.

## Seeding

Two constructor overloads accept unsigned seed values. Both are SFINAE-constrained to `std::is_unsigned<Seed>` to prevent accidental signed-integer conversions. The two-argument form (`xxhasher(Seed seed, Seed)`) accepts but discards the second seed, retaining only the first. This two-argument signature satisfies the interface expected by `hardened_hash`, which generates a random `seed_pair` (two `uint64_t` values) via `make_seed_pair()` — but since XXH3's seeded variant only accepts a single 64-bit seed, the second value is silently dropped. The seed is stored as `std::optional<XXH64_hash_t>` so that zero-seed and no-seed cases remain semantically distinct.

## Endian Contract

The public `static constexpr auto const endian = boost::endian::order::native` member is not decorative — it is part of the `hash_append` protocol defined in `hash_append.h`. The `maybe_reverse_bytes` helper checks whether a hasher's `endian` matches `boost::endian::order::native`; if it doesn't, bytes are reversed before being fed to the hasher so that the hash value is independent of the host's byte order. By advertising native endian, `xxhasher` signals that no reversal is needed, keeping the feed path as cheap as possible on any platform.

## Resource Management

`xxhasher` is non-copyable (copy constructor and assignment deleted) because it owns a raw `XXH3_state_t*` pointer. The destructor frees the state via `XXH3_freeState` only when it was actually allocated, keeping the destructor effectively free for the common small-input case. Move semantics are not implemented, which is acceptable here since hashers are typically constructed, used, and destroyed in a single expression via `uhash::operator()`.

The `static_assert(sizeof(std::size_t) == 8)` guards against 32-bit platforms where a 64-bit `result_type` would silently truncate when returned as `std::size_t`, catching misconfigured build environments at compile time rather than producing subtle hash collisions at runtime.