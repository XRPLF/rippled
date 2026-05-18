# `include/xrpl/basics/CountedObject.h`

## Purpose

This header provides a zero-per-instance-overhead mechanism for counting live objects of any given type throughout the rippled process lifetime. It exists for operational diagnostics: the `get_counts` admin RPC command interrogates `CountedObjects` to report how many instances of each tracked type are currently alive, helping operators identify memory growth, cache saturation, or unexpected object accumulation.

## Design Pattern — CRTP Instance Counting

The design uses the Curiously Recurring Template Pattern (CRTP). A class opts into counting by inheriting `CountedObject<Derived>`:

```cpp
class SHAMapItem : public CountedObject<SHAMapItem> { ... };
class NodeObject  : public CountedObject<NodeObject>  { ... };
class Job         : public CountedObject<Job>         { ... };
```

Across the codebase, roughly two dozen types follow this pattern — `STPathElement`, `STPath`, `InfoSub`, `HashRouter::Entry`, `Book`, `CanonicalTXSet`, and many more. Adding the base class is the entire integration cost; no other instrumentation is required.

The key insight that makes this zero-per-instance overhead is that `CountedObject<T>::getCounter()` returns a **function-local static** `Counter` object — one per template instantiation, not one per live instance. The only per-instance cost is two atomic increments (constructor and destructor) touching a shared counter.

## Three-Layer Architecture

**`CountedObject<T>`** (template base class) — the public-facing layer. Its default constructor, copy constructor, and destructor call `getCounter().increment()` / `decrement()` respectively. The copy constructor is explicitly defined to increment because a copy produces a new live object; the assignment operator is `= default` because assigning between two existing objects doesn't change the total number of live instances. There is no explicit move constructor, so moves fall back to the copy constructor, which correctly increments for the new object while the source's destructor later decrements for the old one.

**`CountedObjects::Counter`** (inner class) — the per-type bookkeeping node. Each `Counter` holds its type name (obtained via `beast::type_name<T>()`, which uses `typeid` plus GCC/Clang ABI demangling for a human-readable string), an `std::atomic<int>` live count, and a raw `Counter*` pointer to the next node in an intrusive singly-linked list.

**`CountedObjects`** (singleton) — the global registry. It owns the head of the lock-free linked list and a count of registered counter types.

## Lock-Free Registration

`Counter` objects self-register when they are first constructed — which happens at first use of any given type, during static initialization of `getCounter()`'s local static. Registration must be thread-safe without a mutex, because many types can be instantiated concurrently at startup:

```cpp
Counter* head = nullptr;
do {
    head = instance.m_head.load();
    next_ = head;
} while (instance.m_head.exchange(this) != head);
```

This is a classic CAS (compare-and-swap) insertion loop: load the current head, set `next_` to it, then atomically exchange the head with `this`. If the head changed between the load and the exchange, retry. Because `Counter` objects are permanent (static lifetime), they are never removed from the list, so traversal during `getCounts()` never encounters a dangling pointer regardless of whether other registrations are happening concurrently.

## `getCounts()` and the Reporting Path

`CountedObjects::getCounts(int minimumThreshold)` traverses the linked list and collects `(name, count)` pairs for any type whose live count is at or above the threshold. It pre-reserves the result vector using `m_count.load()` as a hint (the comment in the implementation acknowledges this can be temporarily under-counted under concurrency — it is only an optimization). The results are sorted alphabetically before return.

The `get_counts` admin RPC handler (`GetCounts.cpp`) calls this with a configurable `min_count` (defaulting to 10) and serializes the results into a JSON object, mixing them with cache statistics, database sizes, write load, and uptime. Object counts appear as top-level keys named by the demangled C++ type.

## Concurrency Properties

All per-type counts use `std::atomic<int>` with default sequential consistency, so `increment()` and `decrement()` are safe from any thread. The linked-list head pointer `m_head` is also `std::atomic<Counter*>`. There are no mutexes anywhere in this file. The only non-atomic operation is reading `Counter::next_` during traversal in `getCounts()`, which is safe because `next_` is written exactly once at construction time and never modified thereafter.

## Why Not Alternatives

A virtual-function approach (e.g., a pure virtual `typeName()` method) would require each instance to carry a vtable pointer and would not trivially aggregate counts across all instances of the same type without additional infrastructure. A manual registry with `std::map` would need a mutex. The CRTP-plus-static-counter approach achieves type safety, automatic demangled names, lock-free operation, and zero per-instance storage — at the cost of slightly surprising copy/move semantics that operators must understand when subclassing.