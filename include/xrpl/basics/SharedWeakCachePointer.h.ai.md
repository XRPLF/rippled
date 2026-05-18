# `SharedWeakCachePointer<T>` — Single-Slot Strong/Weak Pointer for Tagged Caches

## Purpose and Motivation

`SharedWeakCachePointer<T>` solves a memory layout problem specific to the XRPL ledger's `TaggedCache` subsystem. A tagged cache must track every live object it has ever handed out, even after evicting that object from its hot tier. The naïve representation would store both a `std::shared_ptr<T>` (the strong reference that keeps the object alive while it is cached) and a `std::weak_ptr<T>` (the tracking reference after eviction) side-by-side in each cache entry. That wastes an entire control-block pointer per entry: both smart-pointer types are the same size internally, and only one is ever meaningful at a time.

This class resolves that by wrapping `std::variant<std::shared_ptr<T>, std::weak_ptr<T>>` in a single object. At any moment the cache entry holds *either* a strong pointer *or* a weak pointer — never both, never neither (after construction). The variant's discriminator bit is all that's needed to distinguish the two states.

## State Model

The class has two logical states and explicit transitions between them:

**Strong state** — the variant holds a `std::shared_ptr<T>`. The cache entry is "hot": it keeps the referenced object alive independently of any caller. `isStrong()` returns `true`, and `getStrong()` returns the `shared_ptr` by const reference without any atomic increment.

**Weak state** — the variant holds a `std::weak_ptr<T>`. The cache entry is "tracking": the object stays alive only as long as some caller holds its own `shared_ptr`. `isWeak()` returns `true`. Whether the tracked object still exists must be determined by calling `lock()` or `expired()`.

`convertToStrong()` atomically attempts to promote a weak entry to strong by calling `weak_ptr::lock()`. If the referent has already been destroyed it returns `false` and the entry remains weak/expired. `convertToWeak()` demotes a strong entry by constructing a `weak_ptr` from the held `shared_ptr` and replacing the variant alternative. Both transitions are in-place — no allocation or deallocation, just variant reassignment.

## Relationship to `TaggedCache`

`TaggedCache` is the direct consumer of this class. Its inner `ValueEntry` stores a `shared_weak_combo_pointer_type` (defaulted to `SharedWeakCachePointer<T>`) alongside a `last_access` timestamp. `ValueEntry` delegates the strong/weak distinction entirely to the wrapper:

```cpp
bool isCached()  const { return ptr && ptr.isStrong(); }
bool isWeak()    const { if (!ptr) return true; return ptr.isWeak(); }
bool isExpired() const { return ptr.expired(); }
```

During `sweep()`, the cache iterates its entries and calls `convertToWeak()` on entries whose `last_access` has aged out. Entries that are already weak and whose `expired()` is `true` are removed from the map entirely. This two-phase lifecycle — strong while hot, weak while tracked — is the reason the pointer needs to change state in place rather than being replaced by a different type.

## Accessor Semantics

`lock()` is the general-purpose accessor. It works regardless of current state: it returns the held `shared_ptr` directly if the variant is strong, or calls `weak_ptr::lock()` if it is weak. `getStrong()` is a cheaper alternative when the caller already knows or expects the strong state; it returns the `shared_ptr` by const reference (avoiding a reference-count increment) and returns a static empty `shared_ptr` if the variant is currently weak.

`operator bool()` deserves a note: it returns `true` when the variant's active alternative is `std::shared_ptr<T>`, even if that `shared_ptr` is null (e.g., after `reset()`). It does *not* dereference the pointer. This is why `TaggedCache::ValueEntry::isCached()` combines both `ptr &&` (variant is in shared-state) and `ptr.isStrong()` (the shared pointer is non-null). `isStrong()` explicitly checks `p->get() != nullptr` and is therefore the correct predicate when null-ness matters.

## Template Constraints and Covariance

All constructors and assignment operators that accept a `std::shared_ptr<TT>` carry the constraint `requires std::convertible_to<TT*, T*>`. This permits initialising a `SharedWeakCachePointer<Base>` from a `shared_ptr<Derived>` while preventing accidental narrowing conversions. Move overloads are provided alongside copy overloads to avoid unnecessary reference-count increments when a temporary `shared_ptr` is being transferred into the cache entry.

## Implementation Split

The class declaration lives in `SharedWeakCachePointer.h`, with all method bodies in `SharedWeakCachePointer.ipp`. `TaggedCache.h` includes the `.ipp` directly, which is the standard XRPL pattern for template implementations that are only needed by a single well-known consumer. This keeps the header lean and makes the dependency relationship explicit: if you include `TaggedCache.h` you get the implementation; including only the header gives you the interface for forward-declaration purposes.