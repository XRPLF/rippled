# `SharedWeakCachePointer.ipp` — Template Method Implementations

## Role in the System

This file provides the out-of-line method implementations for `SharedWeakCachePointer<T>`, declared in `SharedWeakCachePointer.h`. Because the class is a template, the definitions live in a `.ipp` file rather than a `.cpp` file; `TaggedCache.h` `#include`s this `.ipp` directly to ensure the definitions are available at the point of instantiation.

`SharedWeakCachePointer<T>` is the core pointer abstraction that makes `TaggedCache`'s two-tier lifecycle work. Each `ValueEntry` in the cache map holds one of these, and the variant state — strong or weak — encodes which tier an object occupies: strongly-referenced objects are "hot" and kept alive by the cache itself; weakly-referenced objects are "warm" and tracked but not kept alive, surviving only as long as external code holds a `shared_ptr` to them.

## Internal Representation

The private `combo_` member is a `std::variant<std::shared_ptr<T>, std::weak_ptr<T>>`. The design comment in the header is precise: this uses less memory than holding both pointers simultaneously. Because the variant can only hold one alternative at a time, storage is the size of the larger alternative plus discriminant overhead — a meaningful saving in a map that may contain millions of ledger-object entries.

The variant discriminant is interrogated throughout via `std::get_if`, which returns a pointer to the held alternative (or `nullptr` if the wrong alternative is active), avoiding exceptions and enabling the no-throw invariants.

## Lifecycle Management: `convertToStrong` and `convertToWeak`

These two methods are the reason the class exists. `convertToWeak()` upgrades a hot cache entry to a warm one by replacing the `shared_ptr` alternative with a `std::weak_ptr<T>` constructed from it, releasing the cache's own ownership stake. If the object still has external owners, `weak_ptr::lock()` will continue to succeed; once all external owners drop their handles the object is collected and subsequent `lock()` calls return null.

`convertToStrong()` does the reverse during a cache-hit path: it calls `weak_ptr::lock()` and, if successful, replaces the variant with the resulting `shared_ptr`, reasserting the cache's ownership. Both methods are idempotent — calling them when already in the target state is a harmless no-op returning `true`.

`TaggedCache::sweepHelper()` depends on this pair to implement the sweep lifecycle without erasing entries from the map: entries that haven't been accessed recently are demoted to weak; entries whose weak pointer has also expired are removed entirely.

## Accessor Semantics

**`getStrong()` vs `lock()`** serve different call sites. `getStrong()` returns a `const&` to the held `shared_ptr` — if the variant is in the weak state it returns a reference to a static empty `shared_ptr`. This avoids incrementing the reference count and is appropriate when the caller already knows the pointer is strong (e.g., during a cache insertion). `lock()` unconditionally produces a new owning `shared_ptr` by either copying the strong alternative or calling `weak_ptr::lock()`, and is the safe choice when the strength is uncertain.

**`operator bool()`** returns `true` only if the variant holds a `shared_ptr` alternative and that pointer is non-null. A slot in weak state always returns `false`, which is deliberately asymmetric: code that checks `if (entry.ptr)` treats weak entries the same as empty entries, simplifying `TaggedCache::ValueEntry::isCached()` and `isWeak()`.

**`expired()`** has a slightly subtle implementation: for the `weak_ptr` alternative it delegates to `weak_ptr::expired()`; for the `shared_ptr` alternative it returns `false` (a live strong pointer is never expired). A null strong pointer — i.e., a `shared_ptr{}` stored after `reset()` — falls through to the final `return !std::get_if<std::shared_ptr<T>>(&combo_)`, which evaluates to `true` because `get_if` returns a non-null pointer to the held (but null-content) `shared_ptr`. This edge case correctly marks a reset slot as expired.

**`reset()`** explicitly stores a default-constructed `shared_ptr<T>{}` into the variant rather than relying on any implicit state. This ensures the variant is in the `shared_ptr` alternative (the "null strong" state), preventing future calls from hitting the `weak_ptr` branch unexpectedly.

## Type Safety via C++20 Concepts

Every constructor and assignment operator that accepts a `shared_ptr<TT>` is gated by `requires std::convertible_to<TT*, T*>`. This enforces covariance at compile time: you can construct a `SharedWeakCachePointer<Base>` from a `shared_ptr<Derived>` only if `Derived*` is implicitly convertible to `Base*`. No runtime check or `dynamic_cast` is involved. This mirrors the implicit conversions already present in `std::shared_ptr`'s own converting constructors, maintaining a familiar interface contract.

## Concurrency Considerations

`SharedWeakCachePointer` itself carries no internal lock. Concurrent mutation is the responsibility of the surrounding `TaggedCache`, which guards all access to `ValueEntry::ptr` through its `m_mutex`. The lack of internal synchronization is intentional: a per-pointer lock would add overhead to every cache access for a case where the surrounding container already serializes operations.