# `LedgerHolder.h` — Thread-Safe Immutable Ledger Slot

`LedgerHolder` is a small synchronization wrapper used by `LedgerMaster` to safely share ledger snapshots across threads. Its entire job is to guarantee that one canonical `shared_ptr<Ledger const>` — always immutable, never null once set — can be written by one thread and read by many others without data races.

## Context: Why This Class Exists

`LedgerMaster` maintains two of these holders as private members:

```cpp
LedgerHolder mClosedLedger;   // the most recently closed ledger
LedgerHolder mValidLedger;    // the highest fully-validated ledger
```

These ledgers are produced by the consensus engine and then consumed concurrently by path-finding, RPC handlers, peer-fetching logic, and the validation pipeline. Rather than exposing a raw `shared_ptr` protected by `LedgerMaster`'s own `std::recursive_mutex`, the design isolates the locking concern inside `LedgerHolder`. Each holder carries its own dedicated `std::mutex`, which prevents contention on the coarser `m_mutex` for what is ultimately a pointer copy.

`get()` returns a full copy of the `shared_ptr`, not a reference, so the caller holds an independent owning handle. This is critical: if the holder is updated (i.e., `set()` is called from the consensus thread) while another thread is in the middle of processing the old ledger, the old ledger object remains alive until all callers release their copies. There are no dangling references.

## Invariants Enforced at Write Time

`set()` enforces two hard preconditions and terminates via `LogicError` — a fatal, unrecoverable error — if either is violated:

1. **Non-null**: passing a null `shared_ptr` is a programming error; `LedgerHolder` is not designed to represent "no ledger" after the first assignment.
2. **Immutable**: only ledgers on which `isImmutable()` returns true may be stored. The XRPL codebase uses mutability as a lifecycle marker: a ledger being built is mutable; once closed and hashed, it is sealed. Storing a mutable ledger would allow concurrent modification through the returned pointer, breaking thread safety without any lock.

This pattern mirrors `LedgerHistory` and `RCLValidations`, both of which enforce the same immutability contract with equal severity.

## Lock Strategy and a Noted Future Path

All three public methods — `set()`, `get()`, and `empty()` — take a `std::lock_guard` on the internal `m_lock` for the duration of the operation. The file's own comment acknowledges that `std::atomic<std::shared_ptr<T>>` (available since C++20) could make this class lock-free entirely. As of the current codebase the mutex remains, presumably for compatibility or because the simpler approach was "good enough" given the low contention expected on what are effectively rare write operations (ledger close is an infrequent event).

## Diagnostics via `CountedObject`

`LedgerHolder` inherits from `CountedObject<LedgerHolder>`, which uses a lock-free linked list of static `Counter` instances to track how many `LedgerHolder` objects are alive at any point. This costs nothing at runtime beyond two atomic increments over the object's lifetime and enables the `getCounts()` diagnostic path to report live instance counts, useful for detecting leaks in testing.

## Design Summary

`LedgerHolder` is intentionally minimal — three methods, one mutex, one `shared_ptr`. Its value lies not in complexity but in colocation: it bundles the immutability contract, the null guard, and the mutex into a single reusable unit, preventing callers from forgetting any of the three. The alternative — inline `std::mutex` members and ad hoc checks in `LedgerMaster` — would scatter identical boilerplate across each ledger slot, making future refactoring (e.g., switching to `std::atomic<shared_ptr>`) harder to apply consistently.