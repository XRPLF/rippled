# `ManagerImp.h` — NodeStore Backend Registry and Factory Singleton

## Role in the System

`ManagerImp` is the concrete singleton that powers the NodeStore factory-registry pattern. It sits one layer beneath the public `Manager` interface, hidden inside the `detail/` directory to signal it is an implementation detail rather than a stable API. Its job is to maintain a runtime registry of `Factory` objects, each representing a pluggable storage backend (NuDB, RocksDB, Null, Memory), and to orchestrate the construction of `Backend` and `Database` instances from configuration data.

The `Manager` abstract class, defined in `Manager.h`, declares the singleton interface and exposes `Manager::instance()`, but that method simply delegates to `ManagerImp::instance()`, keeping the implementation type invisible to callers outside the `nodestore` subsystem.

## Singleton Lifecycle and Static Initialization Safety

`ManagerImp::instance()` uses the Meyers' singleton pattern — a function-local `static ManagerImp _` — which is guaranteed thread-safe in C++11 and avoids the static initialization order fiasco for construction. The constructor immediately registers all four built-in factories by calling free functions: `registerNuDBFactory`, `registerRocksDBFactory`, `registerNullFactory`, and `registerMemoryFactory`, each of which calls `insert()` on the manager they receive.

A critical design comment in the `.cpp` explains the choice to use free-function registration rather than global `Factory` objects: if a `Factory` were itself a global, its destructor might call `Manager::instance().erase()` after `ManagerImp` had already been destroyed (C++ gives no reliable order guarantee for global destructor calls). That would be undefined behavior. By calling the register functions from the `ManagerImp` constructor and not relying on `Factory` destructors to call `erase()`, the design avoids this pitfall entirely.

## Factory Registry: Insertion, Removal, and Lookup

The registry is a `std::vector<Factory*>` — raw, non-owning pointers — protected by a `std::mutex`. This pairing is the minimal mechanism needed: `insert()` and `erase()` are called during startup and shutdown, but `find()` is the hot path called whenever a backend is being instantiated.

`find()` performs a case-insensitive linear scan using `boost::iequals`, which accommodates configuration files that might spell a backend name as `"NuDB"`, `"nudb"`, or `"NUDB"` interchangeably. With four registered backends, the linear scan cost is negligible. The mutex is held across the full scan, ensuring correctness even if `erase()` races with a lookup.

`erase()` uses `XRPL_ASSERT` to enforce the invariant that only previously-inserted factories are removed. An attempt to erase an unknown factory is treated as a programming error, not a recoverable condition.

## Backend and Database Construction

`make_Backend()` reads the `"type"` key from the configuration `Section`, finds the matching factory, and delegates to `factory->createInstance()`, supplying `NodeObject::keyBytes` as the fixed key size. If the type is missing or unrecognized, `missing_backend()` throws a `std::runtime_error` with a clear user-facing message pointing to the `[node_db]` configuration stanza. This error function is `static` — it encapsulates the one diagnostic message that should be consistent everywhere a missing backend is detected, and is reused in both the empty-type and unrecognized-type code paths.

`make_Database()` composes on top of `make_Backend()`: it creates and opens the raw `Backend`, then wraps it inside a `DatabaseNodeImp`, which provides the full `Database` interface (async read threads, caching, ledger-sequence-aware dispatch). The explicit `backend->open()` call before wrapping is architecturally significant — it separates the construction of a backend object from the act of opening the underlying storage, allowing the error to be surfaced before the full `DatabaseNodeImp` stack is built.

## Relationship to Sibling Detail Classes

Within the `detail/` directory, `ManagerImp.h` is the entry point for all database creation. `DatabaseNodeImp.h` defines what `make_Database()` ultimately returns: a single-backend, non-rotating database with async fetch and a persistent `Backend` held via `std::shared_ptr`. The rotating variant (`DatabaseRotatingImp`) is constructed elsewhere (for hot/cold storage separation) and is not managed by `ManagerImp::make_Database()`. `BatchWriter`, `EncodedBlob`, `DecodedBlob`, and the codec utilities are backend-level concerns used inside individual `Factory` implementations, not by `ManagerImp` itself.

## Design Tradeoffs

Storing `Factory*` raw pointers instead of `shared_ptr` or `unique_ptr` means the manager never owns the factories. Ownership is intentionally left to the caller (in practice, to static storage managed by the registration free-functions). This avoids double-free scenarios and keeps the registry lightweight. The tradeoff is that callers must ensure factories outlive the manager — a contract enforced by the singleton-lifetime design rather than by any runtime check.