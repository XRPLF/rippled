# `LocalValue.h` — Coroutine-Aware Local Storage

## Role and Motivation

Standard `thread_local` storage is insufficient in a system that runs cooperative coroutines on a thread pool. XRPL's `JobQueue::Coro` coroutines (backed by `boost::coroutines2`) are created on one thread, suspended with `yield()`, and resumed — potentially on a different thread. If two coroutines share a worker thread, a naive `thread_local` variable would expose the state left behind by a previously suspended coroutine, producing subtle, hard-to-reproduce bugs.

`LocalValue<T>` solves this by providing **coroutine-aware local storage**: each coroutine (or plain thread, as a fallback) gets its own independent copy of a `T`, keyed to its execution context rather than its OS thread.

## Architecture

The design has two interlocking layers: a per-coroutine dictionary (`LocalValues`) managed via a thread-local pointer, and a typed wrapper (`LocalValue<T>`) that uses its own address as a dictionary key.

### `detail::LocalValues`

`LocalValues` is a runtime dictionary that maps `void const*` keys to heap-allocated `BasicValue` instances. The keys are the addresses of `LocalValue<T>` objects; the values hold a type-erased `Value<T>` (a concrete subclass of `BasicValue`) accessed through a virtual `get()` that returns `void*`. This type-erasure pattern lets a single heterogeneous map hold values of arbitrary types without any registry or type-ID scheme — the `LocalValue<T>` template handles all casts.

The `onCoro` flag distinguishes two ownership modes. When `onCoro == true`, the `LocalValues` is embedded directly in a `Coro` object (`detail::LocalValues lvs_`) and its lifetime is tied to the coroutine's. When `onCoro == false`, the `LocalValues` was heap-allocated for a plain thread worker, and the `cleanup()` deleter passed to `boost::thread_specific_ptr` will `delete` it on thread exit.

### Thread-Local Pointer Swap in `Coro::resume()`

The critical mechanism lives in `Coro.ipp`. Every time a coroutine is resumed, `Coro::resume()` performs a pointer swap:

```cpp
auto saved = detail::getLocalValues().release();
detail::getLocalValues().reset(&lvs_);
// ... run coroutine body ...
detail::getLocalValues().release();
detail::getLocalValues().reset(saved);
```

This installs the coroutine's private `lvs_` as the active `LocalValues` for the duration of the coroutine's time slice, then restores the previous state. Any `LocalValue<T>` dereference during that time slice will therefore see the coroutine's private dictionary. Because `lvs_` is owned by the `Coro` object and the `thread_specific_ptr` merely borrows it (it will not delete it thanks to `cleanup()`'s `onCoro` guard), there is no double-free risk.

### `LocalValue<T>` — The Public Interface

`LocalValue<T>` is a global or static object that holds a single "prototype" value `t_` set at construction time. The prototype is never mutated; it only serves as the initializer for per-context copies.

`operator*()` implements the lookup:

1. Call `detail::getLocalValues().get()` to retrieve (or lazily create) the `LocalValues` for the current context.
2. If no `LocalValues` exists yet, allocate one with `onCoro = false` (plain thread path) and register it with the `thread_specific_ptr`.
3. Search for `this` in `lvs->values`. On a hit, cast the `void*` back to `T&` and return it.
4. On a miss, emplace a new `Value<T>` copy-initialized from `t_`, then return a reference to that new copy.

`operator->()` is a trivial forwarding wrapper to `operator*()`.

## Concrete Usage

In `IOUAmount.cpp`, `LocalValue<bool>` governs whether IOU arithmetic uses the newer `STNumber` code path or the legacy one. Wrapping this flag in a `LocalValue` rather than `thread_local` ensures that two coroutines executing concurrently on the same thread pool can independently select their arithmetic mode without one overwriting the other's flag:

```cpp
static LocalValue<bool> r{true};
```

The coroutine test in `Coroutine_test.cpp` verifies the isolation property directly: four coroutines each set `*lv = id` (their own integer ID), interleave via yields, and confirm that no coroutine ever sees another's value. A plain-thread job running on the same pool also sees an independent copy.

## Key Design Decisions

**Address-keyed map instead of a registry.** Using `this` (the `LocalValue<T>*`) as the map key avoids any global registry or static ID counter. Each `LocalValue<T>` is naturally unique by address, and the approach requires no synchronization beyond what the `thread_specific_ptr` already provides.

**Lazy allocation.** The per-context copy is created on first dereference rather than at construction. This keeps coroutine startup cheap when only some `LocalValue` instances are actually accessed.

**`void*` type erasure with `unique_ptr<BasicValue>`.** A single `unordered_map` can hold values of unrelated types (`bool`, `int`, custom structs) because ownership and destruction are managed through the virtual destructor of `BasicValue`. The `LocalValue<T>` template retains type knowledge and performs the cast safely — the key equality guarantees that the `void*` behind a given key will always be a `T*`.

**`onCoro` ownership flag.** The asymmetry between coroutine-owned and thread-owned `LocalValues` is handled by a single boolean rather than two separate code paths or a custom deleter per instance. The `boost::thread_specific_ptr` deleter is fixed at static-initialization time, so the flag is the only extensible hook available.