# `include/xrpl/basics/UnorderedContainers.h`

This header is the canonical entry point for hash-based container types throughout the XRPL codebase. It defines two distinct families of type aliases — `hash_*` and `hardened_hash_*` — that differ in a single, security-critical dimension: whether the underlying hash function is seeded at runtime to resist adversarial key collisions.

## The Two-Family Design

The file's own comment states the rule plainly: use `hash_*` containers for keys that do not need a cryptographically secure hashing algorithm; use `hardened_hash_*` when they do. This is not just a style preference — it is a security boundary. In a network node like rippled, many data structures are keyed by values that arrive from untrusted peers or from the open ledger, making them candidates for **HashDoS attacks** (where an adversary crafts many keys with identical hashes to degrade a hash map to O(n) lookup). The two families exist precisely to make that threat explicit at the type level.

### `hash_*` containers

`hash_map`, `hash_multimap`, `hash_set`, and `hash_multiset` all default to `beast::uhash<>` as their hash function. `beast::uhash` is a thin wrapper: it constructs a fresh `beast::xxhasher` (seeded-free, no runtime state) and calls `hash_append` on the key. `beast::xxhasher` itself wraps the XXH3 64-bit algorithm from the `xxhash` library — an extremely fast, non-cryptographic hash that works through a 64-byte internal buffer to avoid the streaming API overhead for small keys. There is no per-container randomization, so these containers are **fast but unsuitable for keys derived from external input**.

### `hardened_hash_*` containers

`hardened_hash_map`, `hardened_hash_multimap`, `hardened_hash_set`, `hardened_hash_multiset`, and `hardened_partitioned_hash_map` all default to `hardened_hash<strong_hash>`, where `strong_hash` is an alias for `beast::xxhasher`.

The protection comes from `hardened_hash`'s constructor. Each instance generates a random `seed_pair` (two independent `uint64_t` values) using a process-wide singleton: a `std::mt19937_64` generator seeded from `std::random_device`, protected by a `std::mutex`. This happens once per `hardened_hash` instance, not once per hash call. The seed is then passed into `beast::xxhasher` as its seed parameter on every invocation. Because each container instance has its own random seed that an attacker cannot know in advance, collisions crafted against one process are useless against another — and collisions crafted against one container cannot be reused against another container of the same type in the same process.

The comment in `hardened_hash.h` explicitly warns against Murmur or CityHash as the `HashAlgorithm` template parameter, citing the SipHash vulnerability research (https://131002.net/siphash/#at). XXH3 with a secret seed is the chosen alternative — fast enough that it doesn't compromise performance for lookup-heavy paths, while offering the unpredictability the hardened family requires.

## The Partitioned Variant

`hardened_partitioned_hash_map` adds a third property beyond security: **sharded concurrency**. It wraps `partitioned_unordered_map<Key, Value, hardened_hash<strong_hash>, ...>`, which internally holds a `std::vector` of independent `std::unordered_map` sub-maps. On construction (defaulting to `std::thread::hardware_concurrency()` partitions), each key is routed to a specific sub-map by `extract(key) % partitions_`. The `extract` function is specialized for `std::string` (hashing via `beast::uhash`) and for integral keys (using them directly as shard indices).

The sharding design assumes callers take per-partition locks externally — the class itself has no synchronization. The pattern is used where the key space is large and concurrent access from multiple threads is expected, allowing thread A to read partition 3 while thread B writes to partition 7 without contention. The default of hardware concurrency as partition count reflects a deliberate choice to match OS-level parallelism without over-provisioning.

## Relationships

- **`hardened_hash.h`** defines `hardened_hash` and the `make_seed_pair()` RNG machinery. The mutex-protected singleton ensures that two `hardened_hash` objects constructed concurrently from different threads each get independent seeds without races.
- **`partitioned_unordered_map.h`** defines the sharded map template. Its forward-iterator walks all partitions sequentially, so range-for over a `hardened_partitioned_hash_map` works but is not order-stable across calls.
- **`beast/hash/uhash.h`** and **`beast/hash/xxhasher.h`** provide the underlying hash plumbing. The `hash_append` protocol means any type that implements the `hash_append` free function in its own namespace is automatically supported by all containers in this file.

In practice, `hash_map` and `hash_set` appear in performance-sensitive internal structures keyed by types the node controls (e.g., pathfinding caches, RPC result sets), while the `hardened_*` variants appear where ledger-derived or peer-supplied data forms the key.