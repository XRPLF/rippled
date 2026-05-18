# `ReadViewFwdRange.ipp` — Type-Erased Forward Iterator Method Bodies

This file contains the template method implementations for `ReadViewFwdRange<ValueType>::iterator`, the concrete iterator type whose declaration lives in `ReadViewFwdRange.h`. It is included verbatim at the bottom of `ReadView.h` via `#include <xrpl/ledger/detail/ReadViewFwdRange.ipp>`, which is the standard `.ipp` pattern for separating template bodies from declarations without creating separate translation units.

## Why This File Exists

`ReadView` is an abstract ledger interface with multiple concrete backing types — open ledger, closed ledger, apply views, and so on. Its two exposed range types, `sles_type` (for state ledger entries) and `txs_type` (for transactions), both inherit from `detail::ReadViewFwdRange<T>`. The challenge is that iteration over these ranges must work uniformly across all concrete `ReadView` implementations without exposing the backing type to callers. The solution is type erasure: `ReadViewFwdRange::iterator` stores a `std::unique_ptr<iter_base>` (where `iter_base` is an alias for `ReadViewFwdIter<ValueType>`), and all iteration operations are delegated through that polymorphic pointer. This file is the stable dispatch layer that implements that delegation for every iterator operation.

## Copy Semantics and the Virtual Clone

Copying a `std::unique_ptr` is impossible by definition, which creates a problem: the `ForwardIterator` concept requires copyability so callers can save and restore position. The solution is the virtual `copy()` method on `ReadViewFwdIter`, which performs a deep clone of the concrete implementation. Every copy constructor and copy assignment operator in this file calls `impl_->copy()` rather than attempting to copy `impl_` directly. This is the classic virtual-clone idiom — the only correct way to duplicate a polymorphic object through a pointer-to-base without knowing the derived type at the call site.

Move operations are straightforward: `impl_` is moved out of the source, leaving it `nullptr`. This is intentional, since a null `impl_` is the convention for end-of-range.

Both move constructor and move assignment are `noexcept`, which the header enforces with `static_assert(std::is_nothrow_move_constructible<iterator>{})`. This matters because STL algorithms and containers prefer moves over copies during relocation, and the `noexcept` guarantee enables that optimization.

## Equality and the Same-View Invariant

`operator==` enforces a precondition via `XRPL_ASSERT`: both iterators must originate from the same `ReadView`. Comparing iterators from different ranges is undefined behavior for any `ForwardIterator`; rather than silently producing a wrong answer, the assert surfaces this as a bug at development time.

When both `impl_` pointers are non-null, equality is delegated to `impl_->equal()`, keeping concrete comparison logic inside the type-erased layer. The null-pointer case covers end-of-range comparisons: two null `impl_` pointers compare equal (both represent end), and null vs. non-null is not equal. This makes `nullptr` a natural, zero-overhead sentinel for the end iterator.

## Lazy Dereference Caching

The `cache_` member (declared as `mutable std::optional<value_type>`) is populated on first access in `operator*()`. The rationale is that `impl_->dereference()` is non-trivial — for ledger entries it typically involves a shared-pointer lookup and value construction. Caching the result avoids redundant work when the same position is dereferenced multiple times, as is common in range-based for loops and algorithm passes. The `mutable` qualifier allows `operator*()` and `operator->()` to populate the cache even on a `const` iterator, which is consistent with the `const` semantics of these operators while still enabling the optimization.

`operator++()` calls `impl_->increment()` then immediately resets the cache via `cache_.reset()`. The invalidation is correct: once the iterator advances, the cached value from the previous position is stale and must not be returned.

## Postfix Increment and Cache Transfer

The postfix `operator++(int)` is worth examining closely. It constructs the saved-position copy using `impl_->copy()` directly and then moves `cache_` into it with `prev.cache_ = std::move(cache_)`. This is slightly more efficient than using the copy constructor, which would call `impl_->copy()` (already accounted for) but would also copy `cache_` rather than move it. By moving the cache into `prev`, the implementation avoids copying the cached value object — particularly relevant when `ValueType` is `std::shared_ptr<SLE const>` or the pair type used by `txs_type`. After constructing `prev`, `++(*this)` advances the current iterator and clears its cache in the normal way.

## Relationship to the Rest of the Ledger Layer

The two concrete range types declared in `ReadView.h` — `sles_type` and `txs_type` — inherit from `ReadViewFwdRange<std::shared_ptr<SLE const>>` and `ReadViewFwdRange<tx_type>` respectively, and they produce iterators of the type implemented here. The concrete `iter_base` subclasses provided by each `ReadView` implementation (spanning files like `ApplyViewBase` and `RawStateTable` in the same `detail/` directory) supply the `copy()`, `equal()`, `increment()`, and `dereference()` overrides that this file calls through the virtual interface. This file never needs to know which of those implementations is active at runtime.