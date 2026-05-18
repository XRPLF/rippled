# `include/xrpl/nodestore/Manager.h`

## Role in the NodeStore Subsystem

`Manager` is the central registry and abstract factory for the NodeStore persistence layer. It solves a plugin-registration problem: the XRPL node database supports multiple storage backends — NuDB, RocksDB, in-memory, and null — and something must map the string name in `xrpld.cfg` (e.g., `type=NuDB`) to the concrete `Backend` class that implements it. `Manager` owns that mapping and exposes the two creation points the rest of the application needs: `make_Backend()` for a raw storage engine and `make_Database()` for a fully-wired `Database` object.

## Interface Design

The class is declared as an abstract base whose `instance()` static method returns the concrete `ManagerImp` singleton defined in `ManagerImp.cpp`. This two-layer design — abstract interface in the header, concrete singleton in the `.cpp` — is deliberate: callers depending only on the header see a clean interface without being coupled to the implementation or forced to include its dependencies. It also makes the `Manager` interface mockable in tests without exposing the singleton machinery.

The singleton itself uses a Meyer's static local (`static ManagerImp _`) in `ManagerImp::instance()`, which guarantees thread-safe initialization under C++11 and beyond. Copy construction and copy assignment are explicitly deleted to reinforce the single-instance contract.

## Factory Registry

`insert()` and `erase()` maintain a runtime list of `Factory*` pointers. Each `Factory` subclass knows how to create one kind of backend. `ManagerImp`'s constructor pre-populates the list by calling four registration functions — `registerNuDBFactory`, `registerRocksDBFactory`, `registerNullFactory`, and `registerMemoryFactory` — so the four built-in backend types are always available without the caller doing anything.

`find()` performs a case-insensitive string comparison using `boost::iequals` so that config values like `NuDB`, `nudb`, or `NUDB` all resolve to the same factory. Both `insert()`, `erase()`, and `find()` acquire a `std::mutex` guard, making the registry safe for concurrent access during startup when multiple subsystems may race to register factories.

There is an important lifetime comment in `ManagerImp.cpp` explaining why `erase()` is not called from `Factory` destructors: C++ does not define destruction order for objects with static storage duration, so `ManagerImp` could be destroyed before the `Factory` instances, making a call into `Manager::instance().erase()` undefined behaviour. The current design avoids this by having the `ManagerImp` constructor eagerly register all factories and accepting that the list may outlive them — a pragmatic trade-off for a static singleton that is alive for the entire program lifetime.

## Backend and Database Construction

`make_Backend()` reads the `type` key from the supplied `Section` parameters, looks up the matching factory, and delegates to `Factory::createInstance()`, passing the fixed key size (`NodeObject::keyBytes`), configuration, burst size, scheduler, and journal. If the `type` key is missing or unrecognised, it throws a `std::runtime_error` with a human-readable message pointing the operator to the configuration file — a defensive pattern that surfaces misconfiguration early at startup rather than silently failing later.

`make_Database()` layers on top: it calls `make_Backend()` to get an opened backend, then wraps it in a `DatabaseNodeImp`, which adds the read-thread pool, write batching, and the higher-level `Database` API. The `burstSize` and `readThreads` parameters thread through both layers, giving the caller fine-grained control over I/O concurrency.

## Relationship to Other Headers

`Factory.h` defines the abstract `Factory` base, which `Manager` accepts by reference — the two are tightly coupled but kept in separate headers to limit inclusion cost. `DatabaseRotating.h` is pulled in to make `DatabaseRotating`'s full type available to callers who construct it via a subclass of `Manager`; `Manager.h` itself doesn't directly create rotating databases, but including the header here ensures downstream callers don't need an extra include. `Backend.h` and `Scheduler.h` flow in transitively through `Factory.h`, completing the set of abstractions `Manager` coordinates.