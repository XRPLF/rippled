# `include/xrpl/basics/IntrusivePointer.h`

This header defines XRPL's custom intrusive smart pointer system: `SharedIntrusive<T>`, `WeakIntrusive<T>`, and `SharedWeakUnion<T>`. The system was designed specifically for `SHAMapInnerNode` — the inner nodes of the radix-16 Merkle trie at the heart of ledger state — but is general enough to serve other reference-counted types. The driver for building this rather than using `std::shared_ptr` is a lifecycle feature called the *partial destructor*, combined with a memory-efficient combined strong/weak pointer variant.

## Why Not `std::shared_ptr`?

The file's own comment names the key difference clearly. With `std::shared_ptr` created via `make_shared`, the control block (which contains both the strong and weak counts) lives alongside the object in a single allocation. That allocation is not reclaimed until both the strong *and* weak counts hit zero. So if something holds a `std::weak_ptr` to an inner node, the node's full allocation — including its 16 child pointers — stays live even after the last `shared_ptr` drops. For the SHAMap this is expensive: each inner node can hold up to 16 child `SharedIntrusive` pointers. The partial destructor mechanism exists specifically to release those children as soon as the strong count falls to zero, leaving only a shell waiting for the weak count to drain.

## Reference Count Layout

The actual counters live in `IntrusiveRefCounts` (`IntrusiveRefCounts.h`), which must be a base class of any type `T` used with these pointers. A single `std::atomic<uint32_t>` field encodes four things:

- **Bits 0–15**: strong count (up to 65535 owners)
- **Bits 16–29**: weak count (14 bits, up to 16383 weak holders)
- **Bit 30**: `partialDestroyStarted` flag
- **Bit 31**: `partialDestroyFinished` flag

Packing counts and flags into one atomic integer means `releaseStrongRef()` can atomically decrement the count *and* set the `partialDestroyStarted` flag in a single CAS loop, avoiding a TOCTOU window where two threads could both decide to trigger partial destruction. The two flags are required to safely sequence concurrent partial- and full-destruction: the last weak pointer release spins on `atomic::wait()` if the partial destructor has started but not yet finished, preventing `delete` from racing with `partialDestructor()`.

## `SharedIntrusive<T>` — The Strong Pointer

`SharedIntrusive<T>` holds a raw `T* ptr_` whose lifetime is controlled by an intrusive strong count on `*ptr_`. Copy construction calls `ptr_->addStrongRef()`; move construction steals via `unsafeExchange(nullptr)` without touching the count. When the last strong holder releases (`unsafeReleaseAndStore(nullptr)` called from destructor or `reset()`), `releaseStrongRef()` returns one of three `ReleaseStrongRefAction` values:

- `noop` — other strong holders remain
- `destroy` — both counts are zero; `delete prev`
- `partialDestroy` — weak holders remain; call `prev->partialDestructor()` then `partialDestructorFinished(&prev)`

The call to `partialDestructorFinished` is the responsibility of the smart pointer class, not the pointee's `partialDestructor()`. This deliberate separation — noted in comments — forces every new `partialDestructor` implementation to explicitly arrange that call, making it harder to accidentally omit the step that wakes waiting threads.

The `unsafe*` private methods (`unsafeGetRawPtr`, `unsafeSetRawPtr`, `unsafeExchange`, `unsafeReleaseAndStore`) are named with the "unsafe" prefix not because they are dangerous in isolation, but as an architectural seam: the comment explicitly anticipates a future patch where `ptr_` might become `std::atomic<T*>`, and isolating all direct pointer access through these methods makes such a change localized.

## Adopt Tags and `make_SharedIntrusive`

Two tag types — `SharedIntrusiveAdoptIncrementStrongTag` and `SharedIntrusiveAdoptNoIncrementTag` — control whether adopting a raw pointer bumps the strong count. `make_SharedIntrusive<TT>()` allocates a new object with `new TT(...)` and wraps it with `NoIncrement`. This is correct because `IntrusiveRefCounts` initializes its atomic field to `strongDelta` (= 1), meaning the object is born with a strong count of one; incrementing again would be a double-count. The `static_assert` in `make_SharedIntrusive` verifies that the adopting constructor is `noexcept`, since a throw after the raw `new` but before the pointer is wrapped would leak the allocation.

## Cast Tags

`StaticCastTagSharedIntrusive` and `DynamicCastTagSharedIntrusive` are dispatch tags for cast-constructors, enabling the `intr_ptr::static_pointer_cast<T>()` and `intr_ptr::dynamic_pointer_cast<T>()` free functions. The move variant of the dynamic-cast constructor handles failure carefully: it uses `unsafeExchange` to steal the pointer from `rhs`, attempts `dynamic_cast`, and if it fails, exchanges the pointer back into `rhs` so ownership is not lost.

## `WeakIntrusive<T>` — The Weak Pointer

`WeakIntrusive<T>` mirrors the weak semantics of `std::weak_ptr`. Copy construction calls `ptr_->addWeakRef()`; the destructor calls `unsafeReleaseNoStore()` which invokes `releaseWeakRef()`. The interesting method is `lock()`: it calls `checkoutStrongRefFromWeak()`, a CAS loop that increments the strong count only if it is already non-zero. If the strong count has already hit zero the lock fails and an empty `SharedIntrusive` is returned. Note that copy assignment from a `WeakIntrusive` is deleted — the comment explains this was omitted to simplify the implementation since no current use case required it.

## `SharedWeakUnion<T>` — The Tagged Pointer

`SharedWeakUnion<T>` is the most architecturally unusual piece. It stores both the pointer value and a strong/weak discriminator inside a single `uintptr_t` field `tp_` by using pointer tagging: if the low bit is `1`, the pointer represents a weak reference; if it is `0`, a strong reference. This works because `alignof(T) >= 2` is statically asserted, guaranteeing the low bit of any valid `T*` is always zero.

The practical value is for tagged caches, where a cache slot should hold a strong pointer when the object is actively needed but can downgrade to a weak pointer to allow eviction without cache churn. `convertToStrong()` and `convertToWeak()` perform in-place promotion and demotion: `convertToStrong()` atomically promotes a weak checkout to a strong reference using `checkoutStrongRefFromWeak()` then releases the weak count; `convertToWeak()` uses the atomic `addWeakReleaseStrongRef()` operation to swap one strong count for one weak count in a single CAS loop, handling the `partialDestroy` case that arises if this was the very last strong pointer. The `lock()` method unifies weak and strong paths: if already strong, increment and return; if weak, attempt a checkout.

## `intr_ptr` Namespace

The nested `intr_ptr` namespace provides `std::shared_ptr`-style vocabulary aliases — `SharedPtr<T>`, `WeakPtr<T>`, `SharedWeakUnionPtr<T>`, `make_shared<T>()`, `static_pointer_cast<T>()`, `dynamic_pointer_cast<T>()` — used throughout the SHAMap subsystem. `SHAMapInnerNode` stores its 16 children as `intr_ptr::SharedPtr<SHAMapTreeNode>` and exposes `partialDestructor()` to reset them when the last strong holder drops.