# `CountedObject.cpp` — Object Instance Tracking Registry

This file provides the implementation of the `CountedObjects` singleton, the runtime registry at the center of the XRPL ledger's live object-count telemetry system. Its purpose is to support operator diagnostics: any subsystem class that inherits from `CountedObject<T>` automatically registers itself and has its live instance count reflected in the `get_counts` admin RPC command.

## System Architecture

The mechanism is a two-layer design defined in `CountedObject.h` and implemented here.

**`CountedObject<T>` (CRTP template)** is the user-facing mixin. Classes throughout the codebase inherit from it — `Ledger`, `STObject`, `SHAMapInnerNode`, `Job`, `NodeObject`, and dozens more. Each template instantiation owns a single function-local `static CountedObjects::Counter` identified by the demangled type name (via `beast::type_name<Object>()`). The mixin's constructor, copy constructor, and destructor call `increment()` and `decrement()` on that static counter, making instance tracking completely transparent to the derived class.

**`CountedObjects`** is the singleton registry. It holds two atomic members: `m_count`, a rough count of how many distinct `Counter` objects exist (i.e., how many tracked types are in the process), and `m_head`, the head of a lock-free singly-linked list of all live `Counter` instances.

## Lock-Free Registration

The most important design detail lives in `Counter`'s constructor in the header:

```cpp
do {
    head = instance.m_head.load();
    next_ = head;
} while (instance.m_head.exchange(this) != head);
++instance.m_count;
```

This is a classic compare-and-swap loop for prepending to a lock-free linked list. Each new `Counter` (one per tracked type, created lazily on first construction of that type) reads the current list head, sets its own `next_` to it, then attempts to atomically swap itself in as the new head. If another thread has modified the head concurrently, the exchange returns a value that doesn't match the previously read head, and the loop retries. This avoids any mutex on the registration path, which matters because registration happens at program startup or on first use of a given type — potentially during global initialization where lock ordering is undefined.

Note that `++instance.m_count` after the CAS loop is not itself atomic with the list insertion. This is intentional — the comment in `getCounts()` acknowledges it:

> When other operations are concurrent, the count might be temporarily less than the actual count.

`m_count` is used only to pre-allocate the return vector (`counts.reserve(m_count.load())`), so a transient undercount merely causes one extra reallocation in a rare race — a perfectly acceptable tradeoff for avoiding a more complex two-word CAS or lock.

## `CountedObjects` Singleton

`getInstance()` uses the Meyer's singleton idiom — a function-local `static CountedObjects instance` — which guarantees thread-safe initialization under C++11 and later without any explicit synchronization. The constructor initializes both atomic members to their zero/null states.

## `getCounts()` — Diagnostic Snapshot

`getCounts(int minimumThreshold)` is the only public query method. It traverses the lock-free linked list of `Counter` objects, collecting entries whose live count meets or exceeds the threshold, then sorts the result alphabetically by type name.

The traversal is inherently a snapshot under concurrent mutation: `Counter` nodes are never removed (they are `static` objects with program lifetime), so the list only grows. A snapshot taken mid-traversal may miss a `Counter` that was prepended after the traversal started, but will never encounter a dangling pointer. This is safe but not strongly consistent — again an acceptable tradeoff for a purely diagnostic facility.

The `minimumThreshold` filter exists so callers can suppress noise. The `get_counts` RPC handler in `GetCounts.cpp` passes `minObjectCount` from the request (defaulting to 10), meaning types with fewer than ten live instances are omitted from the JSON response. Tests use thresholds of 0 or 10 depending on whether they want an exhaustive view or a realistic operator view.

## Relationship to Diagnostics

The output of `getCounts()` feeds directly into the `get_counts` admin JSON-RPC command, where each `(name, count)` entry becomes a key-value pair in the response object. This gives operators a live snapshot of which XRPL internal object types are consuming memory at any moment — useful for diagnosing memory leaks, unexpected object retention (e.g., stale `SHAMapInnerNode` instances after ledger close), or verifying that cleanup paths are functioning correctly. The regression test in `Regression_test.cpp` exploits the same mechanism to assert that the SHAMap does not leak nodes between ledger applications.

## Design Tradeoffs

The design prioritizes zero overhead on the hot path. Each constructor/destructor call does exactly one `std::atomic<int>` increment or decrement — a single fetch-add instruction on modern hardware. Registration of a new type incurs a CAS loop, but that happens at most once per type per process lifetime. The `getCounts()` snapshot path is slow (full list traversal + sort) but it is only ever called from admin RPC or test code, never from any ledger-critical path.