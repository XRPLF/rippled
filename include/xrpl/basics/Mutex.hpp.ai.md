# `include/xrpl/basics/Mutex.hpp`

## Purpose and Motivation

This header introduces a Rust-inspired `Mutex<T>` idiom to the XRPL C++ codebase. The core problem it solves is a well-known C++ anti-pattern: separating a mutex from the data it protects, which lets callers accidentally read or write the data without holding the lock. The standard library provides `std::mutex` and RAII guards, but the data itself remains a separate, freely-accessible member. Nothing in the type system stops code from doing `data_ = x;` without ever calling `lock()`.

The Rust model solves this by making the lock guard the only path to the data. `Mutex.hpp` reproduces that guarantee in C++: the protected value lives inside `Mutex<T>`, is entirely private, and the only way to reach it is through a `Lock` object returned by `Mutex::lock()`. A `Lock` holds both the RAII lock and a reference to the data, so when the `Lock` goes out of scope the mutex is released and the reference becomes unreachable simultaneously.

The file is credited to the Clio project (`https://github.com/XRPLF/clio`) and lives under the `xrpl` namespace in `include/xrpl/basics/`.

## `Lock<ProtectedDataType, LockType, MutexType>`

`Lock` is a thin RAII wrapper that bundles a lock instance (`LockType<MutexType> lock_`) with a reference to the protected data (`ProtectedDataType& data_`). It exposes `operator*`, `get()`, and `operator->` — all in both const and non-const variants — so users interact with the guarded value naturally.

The constructor `Lock(MutexType& mutex, ProtectedDataType& data)` is `private`, with `friend class Mutex<std::remove_const_t<ProtectedDataType>, MutexType>` as the sole access point. The `std::remove_const_t` strip is intentional: when the `Mutex` is const it instantiates `Lock<ProtectedDataType const, ...>`, whose friend declaration must still point back to `Mutex<ProtectedDataType>` (non-const), not the non-existent `Mutex<ProtectedDataType const>`. This single line of `friend` is what makes the safety guarantee airtight — no code path outside `Mutex::lock()` can construct a `Lock`.

The conversion operators `operator LockType<MutexType>&()` and `operator LockType<MutexType> const&()` allow a `Lock` to decay to a reference to the underlying lock object. This matters in practice when `std::unique_lock` is chosen as the `LockType` and the lock needs to be passed to `std::condition_variable::wait()`, which accepts a `std::unique_lock<std::mutex>&`. The conversion keeps the idiom composable with the standard library's synchronisation utilities.

## `Mutex<ProtectedDataType, MutexType>`

`Mutex` stores a `mutable MutexType mutex_` and a `ProtectedDataType data_{}`. The `mutable` qualifier is deliberate: acquiring a lock is logically a non-mutating operation on the container, so it must be callable on a `const Mutex` instance. Without `mutable` the const `lock()` overload could not take `mutex_` by non-const reference as required by any standard lock type.

Two `lock()` overloads handle const-correctness propagation:

```cpp
// const Mutex → Lock<ProtectedDataType const, ...>  (read-only access)
Lock<ProtectedDataType const, LockType, MutexType>  lock() const;

// non-const Mutex → Lock<ProtectedDataType, ...>    (read-write access)
Lock<ProtectedDataType, LockType, MutexType>        lock();
```

Calling `lock()` on a `const Mutex` yields a `Lock` whose `operator*` and `operator->` return `const` references. The compiler enforces this — there is no cast or escape hatch. The test suite verifies it with `static_assert(std::is_const_v<...>)`.

Both overloads are templated on `LockType`, which defaults to `std::lock_guard`. Callers can substitute `std::unique_lock` for deferred or timed locking, or combine a `std::shared_mutex` as the `MutexType` with `std::shared_lock` as the `LockType` to implement reader-writer semantics with the same ergonomic API. The test in `src/tests/libxrpl/basics/Mutex.cpp` demonstrates exactly this: multiple shared readers coexist while an exclusive writer uses `std::unique_lock`.

The `make()` static factory provides perfect-forwarding in-place construction of the protected object:

```cpp
auto m = Mutex<MyStruct>::make(arg1, arg2);  // forwards to MyStruct(arg1, arg2)
```

This avoids a move-construct-then-copy sequence when constructing from arguments and also supports move-only types like `std::unique_ptr<T>`.

## Design Trade-offs

The main cost of this pattern is that the `Lock` object must remain alive for the entire scope of any access. Code that needs to unlock mid-scope, or pass just the protected value to a function, requires `std::unique_lock` as the `LockType` so the conversion operator can expose the lock for manual `unlock()`/`lock()` calls. The default `std::lock_guard` is intentionally non-flexible — it signals "lock for this scope, nothing else" and prevents premature unlocking.

There is no `try_lock()` surface exposed through `Mutex` itself. A caller wanting try-lock semantics must drop to `std::unique_lock` (for `try_lock()`) or interact with the raw mutex type outside this wrapper, which is a deliberate friction point rather than an oversight. The design prioritises the common, safe path — RAII lock for the lifetime of a `Lock` object — over less common, riskier patterns.

The file is the sole resident of the `basics/` header directory that was found, suggesting it was added as a focused utility pulled from Clio rather than part of a larger local module. Its test coverage in `Mutex.cpp` is comprehensive: default construction, value construction, `make()` with forwarding, const and non-const access, custom lock types, `std::shared_mutex` reader-writer patterns, and move-only protected types.