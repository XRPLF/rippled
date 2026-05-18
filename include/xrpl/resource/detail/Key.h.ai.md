# `include/xrpl/resource/detail/Key.h`

## Role in the Resource Management System

`Key` is the lookup key used by the XRPL resource manager to uniquely identify and track every network consumer — a peer making an inbound connection, a node on an outbound connection, or an administratively unlimited caller. Within `Logic`, the central consumer-tracking table is declared as:

```cpp
using Table = hash_map<Key, Entry, Key::hasher, Key::key_equal>;
```

`Key` exists precisely to satisfy the requirements of that `hash_map`: it pairs the two dimensions that distinguish consumers (`Kind` and IP endpoint) and provides the associated `hasher` and `key_equal` types inline as nested structs, following the convention expected by `hash_map` template arguments.

## Structure and Invariants

```cpp
struct Key {
    Kind kind;
    beast::IP::Endpoint address;
};
```

The two fields together form the logical identity of a resource consumer. `Kind` (defined in `Kind.h`) is a three-valued enum — `kindInbound`, `kindOutbound`, `kindUnlimited` — representing the *class* of connection, while `beast::IP::Endpoint` carries the peer's IP address and port. Deleting the default constructor enforces that every `Key` is always fully initialized; a `Key` with a default-constructed (meaningless) endpoint and kind should never exist in the table.

## Hashing vs. Equality: A Subtle Asymmetry

The most architecturally interesting aspect of this file is that `hasher` and `key_equal` are *not* symmetric in what they examine:

- `hasher::operator()` hashes **only on `address`**, ignoring `kind`.
- `key_equal::operator()` compares **both `kind` and `address`**.

This is valid for an unordered container — hash equality is a necessary but not sufficient condition for key equality — but it has a concrete implication: two consumers at the same IP address but with different `Kind` values (e.g., one inbound and one outbound) will hash to the same bucket but resolve to different entries. The design deliberately treats the same IP address appearing in different roles as separate, independently-tracked consumers, while accepting the mild cost of occasional same-bucket collisions between inbound and outbound entries for the same host.

Hashing solely on address also keeps the hash computation cheap: `beast::uhash<>` on a `beast::IP::Endpoint` is a well-known, fast path, and adding a branch on `kind` would provide negligible spread benefit since most entries share the dominant kind at any given time.

## Relationship to `Entry` and `Logic`

`Entry` (in `Entry.h`) stores a back-pointer `Key const* key` into the map's own key storage. This is acknowledged as "a bit of a hack" in the source but exists for a practical reason: `Entry` needs to query its own `kind` (via `isUnlimited()`) and compose its string fingerprint (via `to_string()`) without a separate copy of the key fields. Because the `Key` lives as the `hash_map`'s key — stable in memory for the lifetime of the entry — the raw pointer is safe, but the coupling is tight: `Entry` cannot outlive the `Table` that owns its `Key`.

`Logic` uses the `Table` as the single source of truth for all active consumers. When a new peer connects, a `Key` is constructed from the peer's endpoint and connection direction, and `table_.emplace` either finds an existing entry or creates one. The nested `hasher` and `key_equal` types on `Key` mean callers never need to pass custom comparators separately — the types are self-contained, keeping instantiation of the table concise.