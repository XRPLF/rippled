# `IntrusivePointer.ipp` — Intrusive Smart Pointer Method Definitions

This file provides the out-of-line template method bodies for three intrusive smart pointer classes declared in `IntrusivePointer.h`: `SharedIntrusive<T>`, `WeakIntrusive<T>`, and `SharedWeakUnion<T>`. As is conventional for C++ template implementations that must be visible to all translation units, the definitions live in a `.ipp` file that `IntrusivePointer.h` includes, rather than in a `.cpp` file.

## Why Not `std::shared_ptr`?

The comment on `SharedIntrusive` in the header explains the core motivation: the XRPL codebase needs a smart pointer that can release an object's *data* when the last strong reference drops, while deferring the *memory* release until the last weak reference also drops. `std::shared_ptr` guarantees that the destructor runs at strong-count zero, but an implementation using `make_shared` keeps the entire memory block alive until weak-count zero. More importantly, `std::shared_ptr` provides no hook between "last strong gone" and "last weak gone."

The intrusive design solves this by embedding reference counts directly in the pointee via `IntrusiveRefCounts` (a base struct the controlled type must inherit). When the strong count reaches zero, the pointer machinery calls a user-defined `partialDestructor()` — which can, for instance, reset `SHAMapInnerNode`'s child-pointer array to free the expensive working data — while the object shell continues to exist as long as any weak pointer holds on. Only when the weak count also reaches zero does `delete` run.

## `SharedIntrusive<T>` — The Strong Pointer

Construction and assignment follow a consistent pattern: acquire the new ref before releasing the old. The copy constructor uses a lambda initializer to atomically call `addStrongRef()` before storing the pointer in `ptr_`, ensuring the count is correct even if the lambda result is used to initialize a field directly:

```cpp
ptr_{[&] {
    auto p = rhs.unsafeGetRawPtr();
    if (p) p->addStrongRef();
    return p;
}()}
```

Move construction is cheaper: it calls `unsafeExchange(nullptr)` on the source to steal the pointer without touching ref counts. The `static_assert` in the heterogeneous move-assignment operator enforces at compile time that the same-type case is handled by the homogeneous overload, preventing this overload from being instantiated for `T == TT`.

### The `TAdoptTag` Pattern

The `adopt(T* p)` method and the raw-pointer constructor both take a `TAdoptTag` template parameter constrained by the `CAdoptTag` concept. Two tags exist: `SharedIntrusiveAdoptIncrementStrongTag` increments the strong count (for absorbing a raw pointer that hasn't had its count bumped yet), and `SharedIntrusiveAdoptNoIncrementTag` adopts the pointer without incrementing. `make_SharedIntrusive` allocates via `new T` — `IntrusiveRefCounts` initializes the strong count to 1 — and then adopts with `SharedIntrusiveAdoptNoIncrementTag` to avoid a double-count. The `noexcept` guarantee on that constructor path is enforced with a `static_assert` inside `make_SharedIntrusive` to prevent memory leaks if construction were to throw after allocation.

### `unsafeReleaseAndStore` — The Core Destruction Path

Every operation that replaces the stored pointer funnels through `unsafeReleaseAndStore(T* next)`. It atomically swaps the new pointer in via `std::exchange`, then calls `releaseStrongRef()` on the evicted pointer. The return value is a `ReleaseStrongRefAction` enum with three values:

- `noop` — other strong pointers remain; do nothing.
- `destroy` — no strong or weak pointers remain; call `delete`.
- `partialDestroy` — the weak count is non-zero; call `partialDestructor()` then `partialDestructorFinished()`.

The `partialDestructorFinished` template friend function sets the `partialDestroyFinishedMask` bit atomically on `IntrusiveRefCounts::refCounts`, and if the weak count has already reached zero it calls `notify_one()` to wake any thread that is waiting in `releaseWeakRef()` for the partial destructor to complete before running the full destructor.

### Cast Constructors

`SharedIntrusive` supports both `static_cast` and `dynamic_cast` construction from a `SharedIntrusive<TT>`. For the move variant of `dynamic_cast`, there is a subtle correctness invariant: if `dynamic_cast<T*>` returns null (the cast fails), the source pointer is restored via `rhs.unsafeExchange(toSet)` to prevent the controlled object from leaking. A code comment notes that the `unsafeExchange` structure is also kept in anticipation of a future atomic pointer mode.

## `WeakIntrusive<T>` — The Weak Pointer

`WeakIntrusive` manages a non-owning reference via `addWeakRef()` and `releaseWeakRef()`. Two deliberate omissions in the interface are worth noting:

- Copy assignment from another `WeakIntrusive` is `delete`d. The header comment explains this is because there are currently no use cases, and omitting it simplifies implementation. It can be reintroduced if needed.
- There is no move constructor from `SharedIntrusive<T>&&`. Moving a strong pointer into a weak pointer would require decrementing the strong count and adding a weak count, making it *more* expensive than copying the raw pointer and adding a weak ref. The deleted overload prevents this surprising hidden cost.

`lock()` calls `checkoutStrongRefFromWeak()` on the raw pointer, which uses a CAS loop to atomically increment the strong count only if it is currently non-zero. On success, the new `SharedIntrusive` is constructed with `SharedIntrusiveAdoptNoIncrementTag` — the checkout already performed the increment, so a second increment must not occur.

## `SharedWeakUnion<T>` — The Tagged-Pointer Union

`SharedWeakUnion` packs both a strong and weak reference into the space of a single pointer word. It stores the pointer as a `std::uintptr_t` called `tp_`, uses the low bit as a tag (1 = weak, 0 = strong), and recovers the raw pointer by masking with `ptrMask = ~1`. A `static_assert` on `alignof(T) >= 2` enforces that the actual pointer will never set the low bit, keeping the encoding sound.

`unsafeGetRawPtr()` applies the mask; `unsafeSetRawPtr(T*, RefStrength)` stores the pointer and conditionally ORs in the tag bit. `isStrong()` / `isWeak()` read the tag bit directly.

`convertToStrong()` and `convertToWeak()` allow in-place reference strength switching. `convertToWeak()` uses `addWeakReleaseStrongRef()` — an atomic operation on `IntrusiveRefCounts` that adds a weak delta and subtracts a strong delta in one CAS loop — to avoid a window where the strong count is zero but the weak count hasn't been incremented yet. If the result is `partialDestroy`, `convertToWeak` handles the two-phase partial destruction, including the `partialDestructorFinished` call that clears the pointer variable.

`get()` returns the raw pointer only if the union holds a strong reference; calling `get()` on a weak-tagged union returns null. `lock()` unifies both cases: if already strong, it bumps the strong count and returns; if weak, it attempts `checkoutStrongRefFromWeak()` and adopts without increment on success.

## Naming Convention for Primitives

All methods prefixed with `unsafe` are private and skip reference counting entirely. They manipulate the raw pointer field directly. This naming convention serves two purposes: it makes the separation between raw pointer mechanics and safe counted semantics immediately visible during code review, and the header comments note that these wrappers exist in anticipation of a future patch to support atomic pointer storage (which would require replacing `std::exchange` with `std::atomic::exchange` inside these one-line helpers).