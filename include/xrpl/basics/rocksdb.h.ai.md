# `include/xrpl/basics/rocksdb.h`

## Role and Purpose

This header is a thin aggregation shim that centralizes all RocksDB public API includes behind a single compile-time feature flag. Rather than scattering `#if XRPL_ROCKSDB_AVAILABLE` guards and individual `<rocksdb/…>` includes across every translation unit that touches the database layer, the codebase funnels all such inclusions through this one file. Any source file that needs RocksDB types simply includes `<xrpl/basics/rocksdb.h>` and never needs to mention the feature flag itself.

## The `XRPL_ROCKSDB_AVAILABLE` Guard

The macro `XRPL_ROCKSDB_AVAILABLE` is set to `1` via a CMake target property when the RocksDB library is found during the build. The relevant line in `CMakeLists.txt`:

```
PROPERTIES INTERFACE_COMPILE_DEFINITIONS XRPL_ROCKSDB_AVAILABLE=1
```

This means the macro is propagated as an interface definition on the CMake target, so any consumer that links against it automatically sees the flag without manual `-D` flags. When RocksDB is absent — for example, in minimal builds or unsupported platforms — the entire block compiles away to nothing, and the rest of the `#if XRPL_ROCKSDB_AVAILABLE` guards in implementation files like `RocksDBFactory.cpp` suppress the RocksDB-specific code paths as well.

## Headers Aggregated

When the flag is set, the file pulls in the full surface of RocksDB headers needed by the XRPL node-store backend:

- Core database API (`db.h`, `options.h`, `status.h`, `slice.h`, `iterator.h`)
- Write pipeline (`write_batch.h`, `transaction_log.h`)
- Tuning and customization points (`cache.h`, `filter_policy.h`, `memtablerep.h`, `merge_operator.h`, `compaction_filter.h`, `slice_transform.h`, `comparator.h`)
- Diagnostics and introspection (`statistics.h`, `perf_context.h`, `table_properties.h`)
- Block and table format control (`table.h`, `flush_block_policy.h`, `universal_compaction.h`)
- Environment abstraction (`env.h`, `convenience.h`, `types.h`)

The breadth of this list matches what `RocksDBFactory.cpp` actually uses: the factory constructs `rocksdb::Options`, employs the `rocksdb::EnvWrapper` subclass `RocksDBEnv` for custom thread naming, and uses `rocksdb::WriteBatch` for batched writes through the `BatchWriter` layer.

## Design Rationale

Keeping all RocksDB includes behind a single header prevents the `#if XRPL_ROCKSDB_AVAILABLE` boilerplate from leaking into every file that indirectly depends on RocksDB types. It also makes it straightforward to swap the exact set of required headers in one place should the RocksDB API evolve. The commented-out line `// #include <rocksdb2/port/port_posix.h>` is a remnant of an earlier investigation into an alternative RocksDB namespace or fork (`rocksdb2`) and signals that portability considerations were weighed at some point.

This pattern is consistent with how XRPL handles other optional system dependencies: the feature availability check is resolved once at the CMake level and then expressed as a simple macro, keeping the C++ headers themselves free of build-system details.