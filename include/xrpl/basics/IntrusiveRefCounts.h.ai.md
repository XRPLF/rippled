# `IntrusiveRefCounts.h` — Atomic Reference Counting for Intrusive Smart Pointers

## Role in the System

`IntrusiveRefCounts` is the reference-counting backbone for XRPL's custom intrusive smart pointer family: `SharedIntrusive<T>`, `WeakIntrusive<T>`, and `SharedWeakUnion<T>`, all defined in `IntrusivePointer.h`. A class participates in this system simply by inheriting from `IntrusiveRefCounts`; the notable consumer is `SHAMapInnerNode`, whose billions-of-ops-per-second traversal patterns make every byte and every allocation matter.

The design answers a specific criticism of `std::shared_ptr`: with `make_shared`, the control block and the object are co-allocated, which is efficient, but the memory cannot be reclaimed until the weak count also hits zero. XRPL's intrusive design embeds the counts directly in the object, saves the separate control-block allocation, and — crucially — introduces a *partial destruction* protocol that lets the object shed its expensive payload (e.g., child node pointers) the moment the strong count reaches zero, even while weak pointers keep the shell alive.

## The Packed Atomic Field

All state lives in a single `mutable std::atomic<uint32_t> refCounts`. The 32 bits are divided as follows:

| Bits | Field |
|------|-------|
| 0–15 | Strong count (16 bits) |
| 16–29 | Weak count (14 bits) |
| 30 | `partialDestroyStartedBit` |
| 31 | `partialDestroyFinishedBit` |

Packing everything into one atomic word means that decrementing a count *and* setting a flag can be done as a single compare-and-swap, eliminating any window between the two operations that a second atomic would expose. The helper struct `RefCountPair` wraps the unpack/repack logic: its constructor extracts each field by masking and shifting, and `combinedValue()` reassembles them.

## Destruction Lifecycle and the Partial Destructor

When `releaseStrongRef()` finds that the strong count is dropping to exactly one (i.e., to zero), it branches on whether any weak references exist:

- **No weak refs**: returns `ReleaseStrongRefAction::destroy`. The caller (in `IntrusivePointer.ipp`) runs `delete ptr`, calling the full destructor.
- **Weak refs present**: atomically sets `partialDestroyStartedBit` *in the same CAS that decrements the count*, then returns `partialDestroy`. The caller invokes `ptr->partialDestructor()`, then calls the free function `partialDestructorFinished(&ptr)`.

This CAS loop in `releaseStrongRef()` almost always executes once; looping is necessary only if another thread modifies `refCounts` between the `load` and `compare_exchange_weak`.

`partialDestructorFinished()` is a `friend` function template declared in the class. It atomically sets `partialDestroyFinishedBit` via `fetch_or`, then — if the weak count is already zero at that point — calls `refCounts.notify_one()` to wake any thread blocked in `releaseWeakRef()`. The intentional `T**` (double-pointer) signature is a deliberate API ergonomic signal: after the call, `*o` is set to `nullptr` to discourage use-after-free, because another thread may immediately delete the object upon seeing the finished bit.

## Why Two Bits for Partial Destroy

There is a genuine race that a single bit cannot handle. Consider:

1. Thread A: last strong pointer releases → strong count goes to zero, weak count is 1, `partialDestroyStarted` is about to be set (but has not been set yet).
2. Thread B: last weak pointer releases → sees `strong == 0`, `weak == 1 → 0` → would naively call the full destructor, racing with Thread A's partial destructor.

The `partialDestroyStarted` bit, set atomically in the same CAS that decrements the strong count, prevents Thread B from proceeding until the partial destructor's state is stable. The `partialDestroyFinished` bit then lets `releaseWeakRef()` know it is safe to destroy. When neither bit is set, `releaseWeakRef()` calls `refCounts.wait(…)` — a futex-style block on the atomic value — and rechecks upon wake-up.

## Key Operations

**`addStrongRef()` / `addWeakRef()`** use `fetch_add` with `acq_rel` ordering — the common fast path that requires no CAS loop.

**`addWeakReleaseStrongRef()`** is an atomic composite operation needed when `SharedWeakUnion` converts itself from a strong to a weak reference. Doing these as two separate operations would create a transient moment where the object has neither kind of reference holding it, which could trigger premature destruction. The implementation computes `weakDelta - strongDelta` and applies it as one delta in a CAS loop, while still setting `partialDestroyStartedBit` if appropriate.

**`checkoutStrongRefFromWeak()`** implements `lock()` semantics: it atomically increments the strong count, but only if the strong count is currently nonzero. If it reaches zero between load and CAS, the loop exits with `false`, signalling that the object is already being destroyed.

## Design Notes and Tradeoffs

The `addStrongRef()` is `noexcept` by requirement: `make_SharedIntrusive` calls it immediately after `new T(...)`, and if it could throw, the freshly allocated object would leak. The `static_assert` in `make_SharedIntrusive` enforces this.

The `uint16_t` strong count cap of 65 535 and 14-bit weak count cap of 16 383 are annotated with a `TODO`: if audit reveals these are insufficient, both types would need to widen to `uint32_t`, moving the entire atomic to `uint64_t`. The `checkStrongMaxValue` and `checkWeakMaxValue` constants leave a 32-unit margin below the hard cap, and debug-mode assertions in `RefCountPair`'s constructors fire before the actual overflow, providing early warning.

The `partialDestructorFinished()` free function is deliberately *not* called at the end of the `partialDestructor()` virtual method itself. This means any class that inherits `IntrusiveRefCounts` and implements its own `partialDestructor()` must explicitly call `partialDestructorFinished()` when done. The comment explains this was chosen to make the protocol visible at the call site rather than hiding it inside the smart-pointer machinery, reducing the chance that new subclasses silently skip the required notification.