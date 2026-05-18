# `hardened_hash.h` — Per-Instance Seeded Hash Functor

This header addresses a concrete security problem for any server that accepts external input and stores it in hash-based containers: **hash-flooding attacks**. An adversary who knows the hash algorithm can craft keys that all map to the same bucket, degrading `O(1)` container operations to `O(n)`. For XRPL — which receives transactions, peer messages, and ledger objects from untrusted sources — storing such data in standard `std::unordered_*` containers without mitigation would expose the node to denial-of-service.

`hardened_hash<HashAlgorithm>` solves this by generating a fresh pair of 64-bit random seeds at construction time and injecting them into the hash algorithm. An attacker who cannot predict the seeds cannot craft collisions.

## Seed Generation: `make_seed_pair()`

The private `detail::make_seed_pair()` function holds a function-local static `state_t` that owns a `std::random_device` (OS entropy), a `std::mt19937_64` PRNG seeded from it at startup, and a uniform distribution over `uint64_t`. A `std::mutex` guards the shared mutable state so that concurrent construction of `hardened_hash` instances from multiple threads is safe. The C++11 guarantee of thread-safe static initialization handles the one-time construction of `state_t` itself; the mutex handles every subsequent call to draw two random seeds.

The `bool = true` non-type template parameter on `make_seed_pair` is a deliberate extensibility hook — it allows test code to explicitly specialize the function template and inject deterministic seeds, without changing the default behavior in production.

## `hardened_hash<HashAlgorithm>` Class

The class stores a single `detail::seed_pair` (two `uint64_t` values) generated at construction. Its `operator()` builds a fresh `HashAlgorithm` seeded with those values, then dispatches through `beast::hash_append` to feed the object being hashed into it. The result is extracted via the hasher's `explicit operator result_type()` conversion.

Because seeds are stored per-instance rather than globally, each container gets its own independent randomization. A hash-set and a hash-map holding the same key type will produce different bucket distributions for the same input — this reduces the blast radius if any single seed is ever somehow leaked or guessed.

## The `hash_append` Protocol

Types must be made hashable by providing a free function `hash_append(Hasher&, T const&)` discoverable via ADL. The function is expected to forward-append each constituent field of `T` into the hasher, recursively. This is the composable, algorithm-agnostic hashing design from the N3333/P0029 proposal family. It cleanly decouples the hash algorithm from the type being hashed: the same `T` can be hashed with `xxhasher`, a BLAKE variant, or any future algorithm without modifying `T`.

## Default Algorithm: `beast::xxhasher`

The default `HashAlgorithm` is `beast::xxhasher`, a wrapper around XXH3-64 with seed support. The implementation maintains a 64-byte internal buffer to avoid the streaming API for small inputs, only spilling to an `XXH3_state_t` when data exceeds that buffer. The two-seed constructor `xxhasher(Seed seed, Seed)` accepts both values but only uses the first as the XXH3 seed — the second parameter is effectively ignored, leaving room for a future extension without breaking the two-seed interface that `hardened_hash` provides.

The header comment explicitly prohibits using Murmur or CityHash as the `HashAlgorithm`, citing the SipHash paper (https://131002.net/siphash/#at). Both of those algorithms are known to be trivially attackable via differential cryptanalysis once an attacker can observe output. XXH3 with a secret seed provides practical resistance to this class of attack for non-cryptographic use cases.

## Integration via `UnorderedContainers.h`

`UnorderedContainers.h` consumes `hardened_hash` to define the `hardened_hash_map`, `hardened_hash_set`, `hardened_hash_multimap`, `hardened_hash_multiset`, and `hardened_partitioned_hash_map` type aliases — all defaulting to `hardened_hash<beast::xxhasher>` (`strong_hash`). That file explicitly documents the split: use plain `hash_*` aliases (backed by `beast::uhash`) for internal data unreachable by adversaries; use `hardened_hash_*` aliases anywhere external data lands. This makes the security intent visible at the call site rather than buried in template arguments.

Callers like `AccountID.cpp`, `HashRouter`, `CachedView`, and `AssetCache` all use the hardened variants for data structures keyed on ledger objects or peer-supplied identifiers — the exact scenarios where hash-flooding is a realistic threat.