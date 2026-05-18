# `NullFactory.cpp` — No-Op NodeStore Backend

This file provides the "none" backend for the XRPL NodeStore: a complete, compilable implementation of `Backend` and `Factory` that does absolutely nothing. Every read returns `notFound`, every write is silently discarded, `open()` and `close()` are empty, and `isOpen()` always returns `false`. Its existence is not a mistake — it is a deliberate, necessary piece of the backend registry that supports configuration-driven operation without live storage.

## The Two-Class Pattern

The file follows the same two-class structure used by every other backend in the `backend/` directory (NuDB, RocksDB, Memory): an inner `Backend` subclass holds the actual storage logic, and a companion `Factory` subclass knows how to construct it and registers itself with the global `Manager` singleton.

`NullBackend` inherits from `Backend` and satisfies every pure-virtual requirement in that interface. The `fetch()` method returns the `notFound` status immediately without touching its arguments. `fetchBatch()` returns a default-constructed `std::pair`, meaning an empty vector and a default `Status`. Both `store()` and `storeBatch()` are empty-bodied. `for_each()` never invokes the supplied callback. `getWriteLoad()` returns zero, `fdRequired()` returns zero, and `getName()` returns an empty string (since there is no named file or path to report). The `isOpen()` override always returns `false`, which is honest: the backend never transitions to an open state.

`NullFactory` holds a reference to the `Manager` it was given at construction and registers itself by calling `manager_.insert(*this)` in its constructor. Its `getName()` override returns `"none"`, which is the configuration string that triggers this backend when a node operator sets `type=none` (case-insensitive, matched by `Manager::find()`). `createInstance()` simply heap-allocates a fresh `NullBackend` and returns it as a `std::unique_ptr<Backend>`.

## Registration and Lifetime

`registerNullFactory()` is a free function that the `ManagerImp` constructor calls during the singleton's initialization, alongside the equivalent registration functions for NuDB, RocksDB, and the in-memory backend. Inside `registerNullFactory()`, the factory object is declared as a `static NullFactory const instance{manager}`. The `static` local variable guarantees thread-safe, once-only initialization under C++11 and later, and the `const` qualifier reinforces that the factory is never mutated after construction. The factory's self-registration into `manager_` happens at that first-construction moment and persists for the process lifetime.

This design avoids any global-variable initialization-order hazard: the `Manager` reference is passed in explicitly from the already-constructed `ManagerImp`, and the factory object only comes alive at the point of the call, not at program startup.

## Why This Backend Exists

A null backend is architecturally useful in several scenarios. During testing, components that depend on a `Backend` interface can be wired to `NullBackend` without provisioning any real storage — all lookups will miss, which is predictable and deterministic. In production configurations where a node operator wants to operate without a persistent store (relying entirely on an ephemeral or in-memory layer), `type=none` provides a safe, named configuration value rather than an unconfigured or absent backend. Without a registered `"none"` factory, the manager's `find()` call for that type string would return `nullptr`, likely producing a fatal error at startup.

The backend also serves as a clean documentation artifact: reading it defines the minimal contract that all `Backend` implementations must satisfy, since every pure-virtual method from `Backend.h` appears here in its simplest possible form. No caching, no I/O, no scheduling, no error paths — just the interface skeleton made concrete.