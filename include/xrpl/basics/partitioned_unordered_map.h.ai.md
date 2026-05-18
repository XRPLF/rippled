# `partitioned_unordered_map.h`

## Role and Purpose

This header introduces `partitioned_unordered_map`, a sharded hash map whose primary purpose is to reduce lock contention in multi-threaded workloads. Rather than wrapping a single `std::unordered_map` with one coarse-grained lock, the container holds a `std::vector` of independent `std::unordered_map` instances ("partitions"). Each key deterministically belongs to exactly one partition, so callers who know the key can lock only that partition — leaving every other partition free for concurrent access.

The container itself provides **no synchronization primitives**. That is an intentional design choice: the container exposes its raw `partition_map_type` (the vector of maps) through `map()`, and callers take responsibility for locking individual entries in that vector. The split between "partition selection" and "mutual exclusion" allows each consumer to use whatever locking mechanism suits it — `std::recursive_mutex`, `packed_spinlock`, or anything else.

## Partition Selection and the `extract()` Hook

Keys are mapped to partitions by `extract(key) % partitions_`. The free-function template `extract()` is a customization point separate from the map's `Hash` template parameter. The general template simply casts the key to `std::size_t`, suiting integer-like types. Two specializations in this header and two in sibling headers override it for richer types:

- `std::string` — hashes via `beast::uhash<>{}`.
- `uint256` — reads the first `sizeof(std::size_t)` bytes of the 256-bit value via `memcpy` (avoiding UB from unaligned access).
- `SHAMapHash` — same byte-extraction trick applied to its inner `uint256`.

Separating the partition key from the hash function matters because the hash function may be cryptographically randomized (as in `hardened_hash`) while the partition index must be **stable across calls** to ensure a key always lands in the same shard. The `extract()` convention achieves that stability without coupling partitioning logic to the `Hash` template.

## Constructor and Partition Count

```cpp
partitioned_unordered_map(std::optional<std::size_t> partitions = std::nullopt)
```

When `partitions` is omitted or zero, the container defaults to `std::thread::hardware_concurrency()`, aligning the shard count with the number of logical CPU cores. This is a pragmatic default: one thread per core can own one shard, eliminating contention under ideally scheduled workloads. An `XRPL_ASSERT` guards against the edge case where `hardware_concurrency()` returns zero.

## Iterator Design

The nested `iterator` and `const_iterator` structs navigate a two-level structure using two sub-iterators:

- `ait_` — a `partition_map_type::iterator` that points to the current inner `unordered_map`.
- `mit_` — a `map_type::iterator` that points to the current element within that inner map.

The `inc()` helper advances `mit_`; when it exhausts a partition, it steps `ait_` forward until it finds a non-empty partition or reaches the end of the vector. `begin()` applies the same logic to skip leading empty partitions. `end()` points `ait_` past the vector's end and `mit_` to the last partition's `end()`.

Two points are worth noting. First, equality comparison checks all three of `map_`, `ait_`, and `mit_`, so iterators from different `partitioned_unordered_map` instances correctly compare unequal. Second, `const_iterator` holds a non-const `map_type::iterator` internally (matching the non-const `partition_map_type*`); `const`-correctness at the element level is enforced by returning `const_reference` from `operator*()`.

## Operations

`find()` computes the partition index from the key, then delegates to the underlying `unordered_map::find()` within that shard. A miss returns `end()`. Both `emplace()` overloads — piecewise-construction and key-value forwarding — follow the same pattern: select the partition from the key, emplace into it, and wrap the returned iterator.

`erase()` removes an element and then advances the iterator through any trailing empty partitions, so the returned iterator is valid for continued forward traversal.

`size()` is O(N) — it accumulates counts across all partitions. This is a deliberate tradeoff; maintaining an atomic counter would add write contention on every insert and erase, undermining the sharding benefit.

`operator[]` provides straightforward subscript access, routing through `map_[partitioner(key)]`.

## Usage in `TaggedCache`

The only consumer in the codebase is `TaggedCache`, which instantiates the container via the `hardened_partitioned_hash_map` alias defined in `UnorderedContainers.h`:

```cpp
using hardened_partitioned_hash_map = partitioned_unordered_map<Key, Value, hardened_hash<xxhasher>, ...>;
```

`TaggedCache` exposes a `sweepHelper()` that receives an individual `partition_map_type` entry (one `unordered_map`) along with a held `std::lock_guard`, and processes that shard independently. The sweep can therefore spawn one thread per partition, working in parallel while each thread holds only its own per-partition lock — exactly the concurrency pattern the container's design enables.