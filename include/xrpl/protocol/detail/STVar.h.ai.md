# `include/xrpl/protocol/detail/STVar.h`

## Role and Purpose

`STVar` is the type-erased storage primitive that underlies the entire XRPL serialized-type system. Every field inside an `STObject` — which represents a transaction, ledger entry, or any structured protocol object — is stored as a `detail::STVar`. Because XRPL fields can be any one of roughly two dozen concrete types (integers of various widths, amounts, paths, blobs, nested objects, arrays, and more), the system needs a uniform container capable of holding any of them by value. `STVar` solves this problem by combining type erasure through the `STBase` polymorphic base class with a small-object optimization (SOO) that avoids heap allocation for most common field types.

The class lives in `xrpl::detail` to signal that it is an implementation detail, not a public API surface, even though it is widely used internally by `STObject`, `STArray`, and their relatives.

## Small-Object Optimization

The core design tension `STVar` resolves is: how do you store an arbitrary polymorphic type by value without a heap allocation on every field access? The answer is an inline aligned-storage buffer of `max_size = 72` bytes, held as `std::aligned_storage<72>::type d_`. When a concrete ST type fits within 72 bytes, `construct<T>()` placement-news it directly into `d_`. When it is larger, `construct<T>()` falls back to a regular `new`. The `on_heap()` predicate distinguishes the two cases by comparing `p_` against `&d_` — if the pointer doesn't point into the local buffer, the object is on the heap.

This threshold matters because the most common field types — `STUInt8`, `STUInt16`, `STUInt32`, `STUInt64`, `STAmount`, `STAccount`, `STUInt256`, and others — all fit within 72 bytes. Only large composite types like `STPathSet` or `STObject`/`STArray` themselves overflow onto the heap. A typical XRPL transaction has dozens of fields, so avoiding heap allocation for scalar fields has a meaningful cumulative effect on performance.

## Copy and Move Semantics

Because `STVar` holds a polymorphic object, copies and moves cannot be spelled as simple `memcpy` or `std::move`. The `STBase` class exposes two virtual methods — `copy(n, buf)` and `move(n, buf)` — that allow the concrete type to place-new a copy or move-construct a copy of itself into an external buffer if it fits within `n` bytes, or heap-allocate otherwise. `STVar` is a `friend class` of `STBase`, granting it exclusive access to these private virtual methods.

The move constructor has an important asymmetry: if the source object is on the heap, `STVar` simply steals the pointer and nulls out the source (a zero-copy pointer transfer). If the source object is in the source's inline buffer, it must call `move()` to relocate it into the destination's buffer, because the source's `d_` address is not valid after the source object is destroyed. The copy and move assignment operators follow the same logic with an additional `destroy()` call first.

`destroy()` is equally careful: for in-buffer objects it calls the destructor explicitly (`p_->~STBase()`); for heap objects it uses `delete`. This is the canonical pattern for manual SOO lifetime management.

## Construction Modes and Dispatch

`STVar` exposes three domain-specific constructors beyond copy/move:

- `STVar(defaultObject_t, SField const& name)` — constructs a default-valued instance of the type indicated by `name.fieldType`.
- `STVar(nonPresentObject_t, SField const& name)` — constructs an `STBase` sentinel representing an absent optional field (`STI_NOTPRESENT`).
- `STVar(SerialIter& sit, SField const& name, int depth)` — deserializes a field directly from a binary stream.

Both `defaultObject_t` and `nonPresentObject_t` are empty tag structs with explicit constructors, a common disambiguation idiom that prevents accidental implicit conversions. The global singletons `defaultObject` and `nonPresentObject` are the intended call-site tokens.

All three routes converge on `constructST(SerializedTypeID, depth, args...)`, which is the central dispatch switch. It maps every `SerializedTypeID` enum value to the appropriate concrete template instantiation of `construct<T>()`. The `ValidConstructSTArgs` concept enforces at compile time that `args` are either `(SField)` or `(SerialIter, SField)`, preventing misuse through template arguments alone.

For `STObject` and `STArray` — which can contain nested `STVar` instances — the deserialization path passes `depth` through to their own constructors. The constructor immediately throws `std::runtime_error` if `depth > 10`, capping the recursion depth and guarding against malformed or malicious serialized data that encodes deeply nested structures.

## `make_stvar<T>()` Factory

The free function `make_stvar<T>(args...)` bypasses the type-ID dispatch and constructs a concrete type directly. It is used in well-typed contexts where the caller already knows the exact `STBase` subclass, avoiding the overhead of the switch statement and the runtime field-type resolution.

## Equality

`operator==` and `operator!=` delegate to `STBase::isEquivalent()`, the virtual comparison method. This compares field values but notably not field names, consistent with the semantic that two fields with the same value but different names (e.g., a source amount and a destination amount of equal magnitude) are considered equivalent at the `STVar` level.

## Relationship to `STObject`

`STObject` stores its fields as `std::vector<detail::STVar>`, making `STVar`'s value semantics — particularly its copy and move constructors — critical for vector resizing and field manipulation. The `STObject::Transform` functor unwraps `STVar` to `STBase` references for iterator adapters, exposing the field sequence through a pointer-stable view. The interaction between `STObject`'s field vector and `STVar`'s SOO means most field accesses into a transaction avoid any heap activity beyond what the transaction's own vector allocation already implies.