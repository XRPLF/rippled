# `include/xrpl/nodestore/Factory.h`

## Role in the System

`Factory.h` defines the abstract base class `Factory`, the central abstraction point for pluggable storage backends in the XRPL NodeStore subsystem. The NodeStore is the key-value store responsible for persisting all ledger nodes (account state, transactions, and metadata objects) to disk. Rather than hard-coding a single storage engine, the system delegates construction of `Backend` instances to interchangeable `Factory` objects — one per storage engine type. This is the classic Abstract Factory pattern applied to a production-critical database layer.

## The Two `createInstance` Overloads

The core of the interface is two overloads of `createInstance`, both returning `std::unique_ptr<Backend>`. They share four parameters: `keyBytes` (the fixed key width, always 32 bytes for SHA-512 Half hashes in XRPL), a `Section` of key/value configuration pairs parsed from `rippled.cfg`, a `burstSize` for the backend's write buffer, and a `Scheduler` for async task dispatch. The difference is the second overload additionally accepts a `nudb::context&`.

The second overload is specifically designed for NuDB's asynchronous I/O threading model. A `nudb::context` owns the thread pool that services NuDB's background I/O; when a caller already has a shared context (e.g., for the rotating database that manages shard imports), it can pass it in to share threads across backends. The default implementation of this second overload simply `return {}`s — returning an empty `unique_ptr`. This is a deliberate design choice: non-NuDB backends (`MemoryFactory`, `NullFactory`, `RocksDBFactory`) inherit the default and silently produce a null result, signaling "this backend doesn't use a NuDB context." The caller (`ManagerImp`) falls back to the simpler overload in that case. This avoids forcing every factory subclass to implement a method that has no meaning for their engine.

## Registration and Discovery

Factories don't self-register by magic — each concrete factory registers itself with the `Manager` singleton in its constructor via `Manager::insert(*this)`. For example:

```cpp
explicit NuDBFactory(Manager& manager) : manager_(manager)
{
    manager_.insert(*this);
}
```

A module-level free function like `registerNuDBFactory(Manager&)` creates a `static` factory instance, whose constructor immediately registers with the manager. `Manager::find(name)` then performs a case-insensitive name lookup, enabling the configuration string `type=NuDB` to resolve to `NuDBFactory` at startup. The `getName()` pure virtual method supplies the string key ("NuDB", "memory", "null") for this lookup table.

## Relationship to `Backend` and `Manager`

`Factory` sits at the intersection of two other abstractions. `Backend` (defined in `Backend.h`) is the runtime interface for all storage operations — `fetch`, `store`, `storeBatch`, `open`, `close`, etc. `Factory`'s only job is to *construct* those `Backend` instances from configuration; it has no storage methods itself.

`Manager` (defined in `Manager.h`) is the singleton that owns the registry of `Factory` objects and dispatches `make_Backend()` and `make_Database()` calls by reading the `type=` key from the configuration section. `Manager` depends on `Factory`; `Factory` depends on `Backend` only through its return type. This layering keeps each concern isolated.

## Design Trade-offs

Passing `keyBytes` into `createInstance` rather than inferring it from the key type allows a single backend type to serve different hash-size use cases without subclassing. In practice, every `Backend` instance in production uses 32-byte keys (SHA-512 Half), but the interface is general enough to support alternative hash sizes in tests.

The `burstSize` parameter flows directly into NuDB's `db_.set_burst()` call after the database is opened, controlling how much data NuDB buffers in memory before flushing. Exposing it at the `Factory` level — rather than burying it as a config-file-only setting — lets the `Manager` apply a node-wide policy (e.g., derived from the configured cache size) without the factory needing to re-parse it.

The virtual destructor is the only concrete member in the class. Since `ManagerImp` stores a `std::vector<Factory*>` (raw pointers, not owning), factories must outlive the manager or be explicitly erased first. Concrete factories registered as function-local statics have program lifetime, which is the expected pattern.