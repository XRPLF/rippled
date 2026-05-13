# SHAMap

Merkle radix trie (radix 16) enabling O(1) subtree comparison via hash. Used for both state tree and transaction tree. Root is always a `SHAMapInnerNode`.

## Key Invariants

- Mutable SHAMaps have non-zero `cowid`; immutable have `cowid=0`. Once immutable, nodes persist for the map's lifetime with NO mechanism to remove them
- Copy-on-write: `unshareNode` must be called before mutating any node in a mutable SHAMap; failing this corrupts shared snapshots
- Inner nodes have up to 16 children; hash is computed from children's hashes. Leaf hash is computed from data + type-specific prefix
- `canonicalize` ensures only one instance per hash in the cache; prevents races between threads
- `SHAMapInnerNode` uses atomic operations + locking (`std::atomic<std::uint16_t> lock_`) for concurrent child access

## Common Bug Patterns

- Modifying a node without calling `unshareNode` first corrupts the snapshot that shares it; this is the #1 SHAMap bug class
- `getMissingNodes` uses deferred async reads; processing completions out of order causes incorrect "full below" marking
- Inner node serialization has two formats (compressed vs full) chosen by branch count; mismatched deserializer causes corruption
- `addKnownNode` traverses toward target; if branch is empty or hash mismatches, returns "invalid" -- callers must handle this gracefully
- Proof path verification walks leaf-to-root; incorrect key at any level causes false negative

## Serialization Formats

- **Compressed**: only non-empty branches serialized (saves space for sparse nodes)
- **Full**: all 16 branches including empty ones (used for dense nodes)
- Choice is automatic in `serializeForWire` based on branch count

## Leaf Node Types

- `SHAMapAccountStateLeafNode` - account state entries
- `SHAMapTxLeafNode` - transactions
- `SHAMapTxPlusMetaLeafNode` - transactions with metadata
- Each uses a different hash prefix for domain separation

## Key Patterns

### State Machine
```cpp
enum class SHAMapState {
    Modifying = 0,  // can add/remove objects
    Immutable = 1,  // FROZEN — no changes allowed
    Synching  = 2,  // hash fixed, missing nodes can be added
    Invalid   = 3,  // corrupt — do not use
};
// VERIFY: no peek()/insert()/erase() calls on Immutable maps
```

### COW Discipline (#1 Bug Class)
```cpp
// REQUIRED before mutating any shared node:
auto node = unshareNode(branch, key);  // copies if shared
node->setChild(index, child);          // now safe to modify
// BUG: skipping unshareNode corrupts snapshots sharing the node
```

## Key Files

- `include/xrpl/shamap/SHAMap.h` - main class
- `include/xrpl/shamap/SHAMapInnerNode.h` - inner node (COW, threading)
- `include/xrpl/shamap/SHAMapLeafNode.h` - leaf node base
- `src/libxrpl/shamap/SHAMapSync.cpp` - sync, missing nodes, proofs
- `src/libxrpl/shamap/SHAMapDelta.cpp` - walkMap, parallel traversal
