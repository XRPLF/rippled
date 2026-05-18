# `NodeStore/ManagerImp.cpp` — Backend Factory Registry and Database Construction

## Role in the System

`ManagerImp.cpp` is the concrete implementation of the `Manager` singleton within XRPL's `NodeStore` subsystem. The `Manager` interface is the central switchboard that the rest of the node process uses to create persistent storage objects: it knows which storage backends are available (NuDB, RocksDB, Null, Memory), how to construct them from configuration, and how to wrap them in the higher-level `Database` abstraction. `ManagerImp` is the only implementation, hidden behind the abstract `Manager` interface to allow the rest of the codebase to remain decoupled from any particular backend.

## Singleton Lifecycle and Factory Registration

`ManagerImp::instance()` uses a Meyers singleton — a function-local static — to guarantee thread-safe, lazily initialized construction:

```cpp
static ManagerImp _;
return _;
```

The constructor immediately registers all four built-in backends by calling free functions: `registerNuDBFactory`, `registerRocksDBFactory`, `registerNullFactory`, and `registerMemoryFactory`. Each of these functions contains its own function-local static factory object (for example, `static NullFactory const instance{manager}`), which calls `manager.insert(*this)` in its constructor. This layered approach is deliberate and the file contains an explicit comment explaining why.

The critical design decision is that factories are **not registered via global variables**. If a `Factory` subclass were a global (with a static member or translation-unit scope variable), its C++ destruction order relative to the `ManagerImp` singleton would be undefined across translation units. When such a global's destructor called `Manager::instance().erase()`, the `ManagerImp` might already have been destroyed, resulting in undefined behavior. By calling the registration functions from `ManagerImp`'s own constructor, the factories are initialized after `ManagerImp` and — because function-local statics are destroyed in reverse initialization order — will be destroyed before it, making `erase()` calls in factory destructors safe.

## Factory Lookup and Backend Construction

`make_Backend` is the core factory dispatch method. It extracts the `"type"` key from the configuration `Section` and uses it to find a matching `Factory` via `find()`. The lookup uses `boost::iequals` for case-insensitive name matching, so `"NuDB"`, `"nudb"`, and `"NUDB"` all resolve to the same factory. If the `"type"` key is absent or the name doesn't correspond to any registered factory, `missing_backend()` throws a `std::runtime_error` with a user-facing message directing the operator to add a `[node_db]` entry to `xrpld.cfg`. This transforms a confusing crash or silent failure into actionable guidance.

Once a factory is located, `make_Backend` calls `Factory::createInstance` with `NodeObject::keyBytes` as the fixed key size — a constant reflecting that all XRPL node objects use a 256-bit (32-byte) hash as the storage key.

`make_Database` composes backend creation with database construction: it calls `make_Backend` to get an unopened backend, explicitly calls `backend->open()` on it, then wraps it in a `DatabaseNodeImp`. The separation between backend creation and opening is important — it allows `make_Backend` to hand back an object that can be inspected or configured before I/O begins, while `make_Database` handles the full ready-to-use database lifecycle in one call.

## Thread Safety

The internal factory list `list_` (a `std::vector<Factory*>`) is protected by `mutex_`. Every method that reads or modifies `list_` — `insert`, `erase`, and `find` — acquires a `std::lock_guard` before touching the container. This allows factories to be registered and deregistered safely from any thread, though in practice registration happens only during `ManagerImp`'s own construction on whichever thread first calls `instance()`.

## Error Handling Strategy

There is a deliberate asymmetry in how two classes of error are reported. Configuration errors (missing or unknown `"type"`) produce `std::runtime_error` via `Throw<>`, because they represent operator mistakes that must surface immediately with a meaningful message. In contrast, `erase()` uses `XRPL_ASSERT` to verify that the factory pointer being removed actually exists in the list — this is a programming invariant that should never be violated at runtime, so a debug assertion is appropriate rather than a recoverable exception.

## Public Interface Forwarding

`Manager::instance()` is defined in this file as a one-liner that forwards to `ManagerImp::instance()`. This keeps the abstract `Manager` header free of implementation details while still providing the global access point: callers that include only `Manager.h` can call `Manager::instance()` without knowing that `ManagerImp` exists.