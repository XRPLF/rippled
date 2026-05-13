# NodeStore

Persistent key-value store for `NodeObject`s (ledger entries). All ledger state is stored here between launches. Keys are 256-bit hashes.

## Key Invariants

- `NodeObject` types: `hotLEDGER` (1), `hotACCOUNT_NODE` (3), `hotTRANSACTION_NODE` (4), `hotDUMMY` (512, cache marker for missing entries)
- Preferred backends: NuDB (append-only) and RocksDB; LevelDB/HyperLevelDB are deprecated
- `TaggedCache` evicts by both `cache_size` (max items) and `cache_age` (max minutes)
- `DatabaseRotatingImp` uses two backends (writable + archive) for online deletion; rotation moves writable to archive, creates new writable, deletes old archive
- Corrupt data triggers fatal logging; unknown/backend errors logged with appropriate severity

## Common Bug Patterns

- `fetchNodeObject` with `duplicate=true` copies from archive to writable backend; forgetting this in rotating mode means objects disappear after rotation
- `hotDUMMY` objects in cache mark missing entries; code that checks cache hits must distinguish real objects from dummies
- Batch write limit is 65536 objects; exceeding this silently truncates or fails depending on backend
- `fdRequired()` must be called during resource planning; running out of file descriptors causes silent backend failures

## Review Checklist

- Config changes: verify `[node_db]` section has valid `type`, `path`, and `compression` settings
- Online deletion: ensure `SHAMapStoreImp` coordinates rotation with the application lifecycle
- New backend types: implement the full `Backend` interface including `fdRequired()`

## Key Patterns

### Cache Lookup — Distinguish Real vs Dummy
```cpp
// REQUIRED: hotDUMMY marks "confirmed missing" — not a real object
auto obj = cache_.fetch(hash);
if (obj && obj->getType() == hotDUMMY)
    return nullptr;  // not found, just cached as missing
return obj;
```

### Backend File Descriptor Reporting
```cpp
// REQUIRED: every backend must accurately report FD needs
int fdRequired() const override
{
    return fdLimit_;  // inaccurate values cause silent failures
}
```

## Key Files

- `include/xrpl/nodestore/NodeObject.h` - object types
- `include/xrpl/nodestore/Backend.h` - backend interface
- `include/xrpl/nodestore/detail/DatabaseNodeImp.h` - standard implementation
- `src/libxrpl/nodestore/DatabaseRotatingImp.cpp` - rotating/online deletion
- `src/xrpld/app/misc/SHAMapStoreImp.cpp` - lifecycle management
