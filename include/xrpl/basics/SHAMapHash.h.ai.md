# `SHAMapHash.h` — Strongly-Typed Node Hash for SHAMap

## Role in the System

`SHAMapHash` is a thin but intentional strong-typedef wrapper around `uint256` that represents the cryptographic hash of either a single node in the SHAMap radix tree or the hash of the entire map (which is, by definition, the hash of its root node). The XRP Ledger uses `uint256` for many semantically distinct purposes — transaction IDs, account state keys, ledger hashes, and node hashes — so wrapping the SHAMap-specific case in a distinct type lets the compiler enforce that a raw object identifier is never accidentally passed to an API that expects a node hash, or vice versa.

## Class Design

The class holds a single private member `hash_` of type `uint256` and provides precisely two construction paths: the default constructor (producing a zero hash), and an `explicit` single-argument constructor from `uint256 const&`. The `explicit` keyword is load-bearing here: it prevents implicit conversions from arbitrary `uint256` values into a `SHAMapHash`, which is the primary value of the wrapper type. Downstream code that wants to create a `SHAMapHash` must state that intention clearly.

Access to the raw `uint256` value is provided through `as_uint256()`, offered in both const and mutable overloads. The mutable overload exists because certain serialization paths need to compute the hash in place. State-query helpers — `isZero()`, `isNonZero()`, `signum()`, and `zero()` — delegate directly to the corresponding `uint256` methods rather than reimplementing them, keeping the wrapper thin.

The comparison operators `operator==` and `operator<`, along with `operator<<`, `to_string()`, and the `hash_append()` template, round out the set of operations needed to use `SHAMapHash` in containers, ordered structures, and diagnostic output. These are defined as hidden-friend functions inside the class body, which means they participate in argument-dependent lookup only when one of their arguments is a `SHAMapHash`, preventing accidental overload resolution with other types. `operator!=` is defined as a non-member inline outside the class, delegating to `operator==`.

## The `extract()` Specialization

The most architecturally significant piece of this header is the explicit specialization of the `extract()` function template at the bottom of the file:

```cpp
template <>
inline std::size_t
extract(SHAMapHash const& key)
{
    return *reinterpret_cast<std::size_t const*>(key.as_uint256().data());
}
```

This specialization integrates `SHAMapHash` with `partitioned_unordered_map` — a concurrent-access-friendly container that splits its buckets across multiple independent maps, one per hardware thread. The `partitioned_unordered_map::partitioner()` method calls `extract(key) % partitions_` to decide which sub-map owns a given entry. For `SHAMapHash`, the partition selector is simply the first `sizeof(std::size_t)` bytes of the underlying 256-bit hash, reinterpreted as a native integer. Because SHA-512 half-digests (which generate SHAMap node hashes) have uniform bit distribution, this naive prefix extraction yields an even spread across partitions without any additional hashing. This is why the header includes `partitioned_unordered_map.h` at all — not for the map itself, but to place the `extract` specialization in the same translation unit that sees the primary template declaration.

It is worth noting that the analogous `extract<uint256>` specialization in `base_uint.h` uses `std::memcpy` to avoid potential undefined behavior from unaligned pointer access, whereas this specialization uses a direct `reinterpret_cast`. Both produce the same result on common architectures, but the `memcpy` form is more strictly correct under the C++ aliasing rules.

## Relationship to the SHAMap Subsystem

`SHAMapTreeNode` declares a protected `SHAMapHash hash_` member that holds the cached content hash for each tree node. Derived types — `SHAMapInnerNode`, `SHAMapLeafNode`, and their concrete variants — inherit this field and set it during construction or when their content is modified. `SHAMapInnerNode` additionally stores an array of 16 child `SHAMapHash` values inside its `TaggedPointer hashesAndChildren_` structure, enabling hash-based validity checks on child branches without requiring the child node to be loaded into memory.

At the `SHAMap` level, `SHAMapHash` appears as the key type for cache lookups (`cacheLookup`, `canonicalize`), for fetching missing nodes from the database or peer-provided data (`fetchNodeNT`, `fetchNodeFromDB`), and as the return type of `getHash()` which exposes the map's current root hash. The `Delta` structure tracks nodes that were modified between two map versions, and the missing-node set inside `SHAMap::DeltaFinder` is typed as `std::set<SHAMapHash>`, again relying on `operator<` for ordering. `SHAMapMissingNode` stores a `SHAMapHash` to report which hash was absent when a synchronization fetch failed, making it useful for targeted peer requests during ledger sync.

## Summary

`SHAMapHash` is a compact but effective type-safety boundary: it costs nothing at runtime — no vtable, no additional storage, no indirection — while preventing the kind of silent `uint256` category confusion that would otherwise be possible in a codebase that uses the same underlying 32-byte type for many distinct protocol-level concepts. Its `extract()` specialization is a deliberate hook into the partitioned hash map infrastructure, enabling lock-striped concurrent node caching without requiring any changes to the container itself.