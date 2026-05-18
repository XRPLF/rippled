# `SHAMapStore.h` — Abstract Interface for Ledger Storage Lifecycle Management

`SHAMapStore` is the pure abstract interface through which the rest of the XRPL node interacts with the ledger database management subsystem. Its two primary responsibilities are: (1) owning the creation and configuration of the `NodeStore::Database` backend that persists SHAMap node data on disk, and (2) driving the *online delete* process — the mechanism by which old ledger history is pruned from a running node without downtime or restart.

The concrete implementation lives in `SHAMapStoreImp` (declared in `SHAMapStoreImp.h`, implemented in `SHAMapStoreImp.cpp`). This header exists as an abstraction boundary so that the rest of the application — `LedgerMaster`, fee infrastructure, and configuration consumers — can interact with storage management without coupling to the background thread, database rotation logic, or SQLite bookkeeping that the implementation encapsulates. Callers obtain a `SHAMapStore&` through `Application::getSHAMapStore()` and never need to know the concrete type.

## The Online Delete Model

The fundamental challenge online delete solves is: a validator node accumulates ledger history indefinitely; operators may not want to keep all of it; but reads and writes to the store must continue without pause while old data is discarded. The `SHAMapStore` interface is shaped entirely by this problem.

The implementation uses a **dual-backend rotation** strategy. `makeNodeStore()` creates a `NodeStore::DatabaseRotatingImp` backed by two on-disk stores (a "writable" backend and an "archive" backend). As new ledgers are validated and old ones are no longer needed, the archive backend is cleared and the two backends are atomically swapped — the old writable store retires, and a fresh backend becomes current. This lets deletion proceed against the retiring backend without blocking reads or writes to the active one.

## Interface Walk-Through

`onLedgerClosed()` is the system's heartbeat. `LedgerMaster` calls it every time a ledger is validated, passing a `shared_ptr<Ledger const>`. Inside `SHAMapStoreImp`, this posts the ledger to the background deletion thread via a condition variable — no deletion work happens on the calling thread. The interface makes this integration point explicit as a first-class method rather than a callback or observer slot, reflecting how central it is to the design.

`makeNodeStore()` is called once at startup. It returns a `unique_ptr<NodeStore::Database>` and has the important side effect of initializing the internal `dbRotating_` pointer that the deletion loop uses. When `online_delete` is not configured, it creates a simple single-backend database; when online delete is enabled, it creates the `DatabaseRotatingImp` pair and records both backend paths in the SQLite state database for crash-safe recovery on restart.

`clampFetchDepth()` is a subtle but important correctness guard. `LedgerMaster` uses it to limit how far back the node will attempt to fetch ledger data from peers. When online delete is active, fetching a ledger older than `deleteInterval_` is counterproductive — that ledger may be deleted before the fetch completes, or may already be deleted. This method returns `min(fetch_depth, deleteInterval_)` when deletion is active, and passes through unchanged otherwise.

`setCanDelete()` and `getCanDelete()` govern the deletion boundary. The "can delete" ledger index is the highest ledger that the background thread is authorized to delete. In normal (non-advisory) mode this is implicitly managed by `deleteInterval_`. In **advisory delete** mode (`advisoryDelete()` returns `true`), an external administrative call must explicitly advance this boundary before any deletion occurs — useful when operators want fine-grained control or are coordinating a rolling upgrade across multiple validators.

`getLastRotated()` returns the ledger sequence of the most recent completed rotation boundary (or a rotation in progress). This is persisted in the SQLite state database so it survives restarts. The distinction between `getLastRotated()` and `getCanDelete()` matters: `lastRotated` is where the backend last swapped; `canDelete` is the administrative ceiling up to which deletion is permitted.

`minimumOnline()` provides the peer-to-peer acquisition lower bound. When online delete is active and about to clear history through sequence N, `minimumOnline_` is bumped to N+1. This prevents the node from advertising or trying to serve ledgers it is in the process of deleting. If online delete is not configured, or has never run, this returns an empty `optional` — meaning the node will try to maintain history as far back as it can.

`rendezvous()` is a synchronization primitive for callers that must wait until the background thread is idle (i.e., `working_` is false). This is used during shutdown to avoid tearing down state while a rotation is mid-flight.

`fdRequired()` reports the number of file descriptors the store needs, so the application can set `ulimit` appropriately before opening backends. The dual-backend rotation model uses more file descriptors than a single-backend configuration.

## Factory Function

`make_SHAMapStore()` takes an `Application&`, a `NodeStore::Scheduler&`, and a `beast::Journal`, following the same factory pattern used throughout XRPL's application layer (compare `make_AmendmentTable`, `make_NetworkOPs`). This keeps construction details — including the concrete subclass, configuration parsing, and validation of `online_delete` vs. `ledger_history` consistency — out of call sites, and allows the interface to evolve without changing consumers.

## Design Rationale

The decision to express this as a pure interface rather than a concrete class is worth noting. `SHAMapStoreImp` has a significant background thread and substantial mutable state. Putting an abstraction boundary here means that unit tests can substitute a no-op `SHAMapStore` without spawning deletion threads or requiring a real database, and that the threading model remains an implementation detail rather than part of the public contract. The interface surface is deliberately minimal: only the operations that external subsystems actually need are exposed.