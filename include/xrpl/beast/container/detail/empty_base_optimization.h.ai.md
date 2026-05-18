# `beast/container/detail/empty_base_optimization.h`

## Purpose

This header implements the *Empty Base Optimization* (EBO) wrapper pattern — a well-known C++ technique for eliminating the storage cost of stateless policy types. In standard C++, every object must have a unique address, so even a completely empty class consumes at least one byte when stored as a data member. But when an empty class is used as a *base*, the compiler is permitted to collapse its contribution to zero bytes. This file provides `empty_base_optimization<T>` as a portable, uniform wrapper that exploits this rule when it is safe to do so and silently falls back to plain member storage when it is not.

## The Eligibility Trait

`is_empty_base_optimization_derived<T>` encodes the two conditions that must hold for EBO to be applicable:

1. `std::is_empty<T>::value` — the type has no non-static data members and no virtual functions.
2. `!boost::is_final<T>::value` — the type is not marked `final`, which would prevent inheritance outright.

The `boost::is_final` check is the critical guard. A `final` class that is also empty cannot be used as a base, and the primary template would fail to compile without it.

## Two Specializations via `isDerived`

The template has three parameters: `T`, an integer `UniqueID` (default 0), and a computed boolean `isDerived`. Template partial specialization selects between two fundamentally different implementations:

**EBO path (`isDerived = true`):** `empty_base_optimization` privately inherits from `T`. The `member()` accessor returns `*this` cast to `T&` — zero storage overhead when `T` is empty. All constructors forward their arguments directly to `T`'s constructor via perfect forwarding.

**Fallback path (`isDerived = false`):** `empty_base_optimization` holds `T` as a plain data member `t_`. The `member()` accessor returns `t_`. This path is taken for non-empty types (which have state to store anyway), for `final` types (which cannot be inherited from), and for non-class types.

The `member()` name provides a single uniform API so callers never need to know which path was taken. This is the core value of the abstraction.

## The `UniqueID` Parameter

The integer `UniqueID` exists to handle the case where a class must wrap the *same* empty type twice as a base. Without it, `class Foo : private empty_base_optimization<Alloc>, private empty_base_optimization<Alloc>` would produce a duplicate-base compilation error. Giving each instance a distinct `UniqueID` makes them different instantiations of the template and thus distinct base classes. In the current codebase only `UniqueID=0` (the default) is used, but the parameter leaves the door open for containers that might co-locate, say, a hash functor and an equal functor of the same stateless type.

## Usage in the Aged Containers

Both `aged_ordered_container` and `aged_unordered_container` inherit from `empty_base_optimization<ElementAllocator>` inside their private `config_t` helper class, alongside direct private inheritance from policy types like `KeyValueCompare`, `ValueHash`, and `KeyValueEqual`. The `alloc()` method then delegates to `empty_base_optimization<ElementAllocator>::member()`.

In the overwhelmingly common case where users pass `std::allocator<T>` — which is a stateless empty type — the allocator costs nothing in the `config_t` object. The `config_t` pattern deliberately aggregates the clock reference, comparator, hasher, equality predicate, and allocator into a single object so that the compiler can apply EBO across all of them simultaneously, a technique sometimes called the "compressed tuple" pattern.

## Design Notes

The file originates from Boost.Beast (Boost Software License), where the same EBO helper appears in many container implementations. The private inheritance (`class empty_base_optimization : private T`) intentionally hides `T`'s interface — callers use only `member()`, preventing accidental calls to methods of the wrapped policy object through the container's own public interface. This keeps the wrapper a pure storage mechanism with no behavioral leakage.