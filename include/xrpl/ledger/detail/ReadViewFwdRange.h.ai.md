# `include/xrpl/ledger/detail/ReadViewFwdRange.h`

## Role in the System

This header defines the type-erased forward-iteration infrastructure that allows any concrete `ReadView` implementation — whether a fully-validated ledger, an open in-progress ledger, a cached view, or a sandbox — to expose its state entries and transactions through a single, stable iterator type. Without this mechanism, every `ReadView` subclass would need to publish its own concrete iterator type, making it impossible to write view-agnostic code that walks the ledger state or transaction set.

The file lives in `xrpl::detail`, signalling that it is internal plumbing. Callers interact with it indirectly through `ReadView::sles` and `ReadView::txs`.

## The Type-Erasure Pattern

The design follows the classic "virtual concept" or "type-erased iterator" pattern. It has two layers.

**`ReadViewFwdIter<ValueType>`** is an abstract base class that defines the four primitive operations any forward iterator must support: `copy()` (polymorphic clone), `equal()` (comparison against another base), `increment()` (advance), and `dereference()` (retrieve value). Each concrete `ReadView` subclass implements this interface privately and returns instances via the factory methods `slesBegin()`, `slesEnd()`, `slesUpperBound()`, `txsBegin()`, and `txsEnd()` declared in `ReadView.h`. Those factory methods return `std::unique_ptr<iter_base>` — the only place where the concrete type is visible.

**`ReadViewFwdRange<ValueType>::iterator`** wraps a `unique_ptr<iter_base>` and exposes all the standard STL forward-iterator operators. From the perspective of calling code, the iterator is a regular value type with copy, move, equality, dereference, and increment — no virtual dispatch is visible. The virtual dispatch is entirely hidden inside the `impl_` pointer.

## Key Design Decisions

**Why `copy()` instead of relying on `unique_ptr` copy?** `std::unique_ptr` is intentionally non-copyable because it models unique ownership. The abstract `copy()` method provides a virtual clone that deep-copies the concrete iterator state. The `iterator`'s copy constructor calls `other.impl_->copy()` to produce a new, independent polymorphic instance. This is the standard workaround for value-semantics copy of a type-erased object.

**Cached dereference via `mutable std::optional<value_type>`**: The `operator*` implementation in the `.ipp` file checks `cache_` before calling `impl_->dereference()`. Once the value is loaded it is stored and subsequent `operator*` or `operator->` calls return it cheaply. The cache is cleared in `operator++`, ensuring stale data is never returned after advancing. This matters because ledger state entries (`std::shared_ptr<SLE const>`) involve a heap allocation and potentially a map lookup; avoiding repeated dereferences is a meaningful optimization in tight iteration loops over large ledgers.

**Noexcept move semantics enforced by `static_assert`**: Two `static_assert` checks inside the class definition — and one on `ValueType` itself — guarantee that move construction and move assignment of the iterator are `noexcept`. This is essential for use in standard containers and algorithms that rely on noexcept-move for efficient reallocation. Because `std::unique_ptr` move and `std::optional` move are already noexcept, and `ValueType` is constrained, the guarantee holds without extra effort.

**`view_` pointer carried on the iterator**: Each iterator stores a `ReadView const*` alongside the `impl_`. At first glance this seems redundant, since `impl_` knows the view internally. Its purpose is visible in `operator==` in the `.ipp`: it fires an `XRPL_ASSERT` that both sides of a comparison reference the same view. Comparing iterators from different views would be undefined behaviour; the assertion catches this programming error in debug builds without any cost in release.

## Relationship to `ReadView`

`ReadView.h` includes this header and uses both templates directly. `ReadView::sles_type` extends `ReadViewFwdRange<std::shared_ptr<SLE const>>` and delegates `begin()`, `end()`, and `upper_bound()` to the virtual factory methods on the owning `ReadView`. `ReadView::txs_type` does the same for `std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>`. The template implementations in `ReadViewFwdRange.ipp` are included at the bottom of `ReadView.h` — after the full `ReadView` definition is available — rather than from within this header itself, the standard pattern for avoiding circular dependencies with template bodies.

## What This Enables

Any code that holds a `ReadView const&` can write a range-for loop over `view.sles` or `view.txs` without knowing whether the underlying object is a `Ledger`, an `OpenView`, a `CachedView`, or a `PaymentSandbox`. The type-erased iterator handles all dispatch transparently. Concrete implementations only need to provide the four primitive `ReadViewFwdIter` virtual methods and the five factory methods — a clean, minimal extension point for a class hierarchy that spans the entire ledger layer.