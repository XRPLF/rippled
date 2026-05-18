# `STVar.cpp` — Type-Erased Variant Storage for XRPL Serialized Types

`STVar` is the runtime polymorphic container at the heart of XRPL's serialization system. It acts as a type-erased "variant" that can hold any concrete `STBase`-derived type — `STUInt32`, `STAmount`, `STObject`, `STArray`, and roughly twenty others — without exposing the concrete type to callers. This file provides the out-of-line definitions for `STVar`'s constructors, assignment operators, destructor, and the central dispatch function `constructST`.

## Small-Object Optimization

The class stores objects using a two-tier strategy declared in the header: a 72-byte aligned stack buffer (`d_`) and a raw pointer `p_`. The `construct<T>()` template checks `sizeof(T)` at compile time: if the object fits within 72 bytes it is placement-new'd into `d_`; otherwise it is heap-allocated. The `on_heap()` predicate simply compares `p_` against the address of `d_` — if they differ, the object lives on the heap. This avoids a separate boolean flag entirely.

This optimization matters because `STVar` values appear inside `STObject` and `STArray`, which are themselves stored in vectors. Keeping small serialized types (integers, hashes, short blobs) in the stack buffer eliminates millions of heap allocations during transaction processing and ledger deserialization.

## Destruction: Two Paths

`destroy()` encodes the dual storage strategy directly:

```cpp
if (on_heap())
    delete p_;
else
    p_->~STBase();
```

Placement-new requires an explicit destructor call, not `delete` — which would wrongly attempt to free memory from the stack buffer. `delete` is reserved for the heap path. Both paths reset `p_` to `nullptr` to leave the object in a valid empty state.

## Copy and Move Semantics

Copy construction delegates to `other.p_->copy(max_size, &d_)` — a virtual method on `STBase`. Each derived type implements `copy()` and `move()` via the shared `emplace()` helper in `STBase`, which itself re-applies the same size check: place into the provided buffer if it fits, otherwise heap-allocate. This means the small-object decision is made independently on each copy, which is correct since the destination buffer is always a fresh `STVar::d_`.

Move construction handles the two cases differently. If the source object is on the heap, ownership is transferred by pointer swap (`p_ = other.p_; other.p_ = nullptr`) — a zero-copy O(1) move. If it lives in the source's stack buffer, a move-construction into the destination's buffer is necessary via `p_->move(max_size, &d_)`, since buffer addresses are non-transferable.

## Construction Entry Points and Tag Dispatch

Four public construction paths exist:

- **`STVar(SerialIter&, SField const&, int depth)`** — deserialization from a wire-format byte stream. This is the only path that receives and enforces a `depth` limit.
- **`STVar(SerializedTypeID, SField const&)`** — construct a default-valued object of a given type. Used internally.
- **`STVar(defaultObject_t, SField const&)`** — a named-tag wrapper that delegates to the `SerializedTypeID` path using `name.fieldType`.
- **`STVar(nonPresentObject_t, SField const&)`** — constructs a bare `STBase` with `STI_NOTPRESENT`, representing a field that exists in the schema but is absent from a specific object.

The `defaultObject_t` and `nonPresentObject_t` tag structs — defined as empty types with explicit constructors — exist solely to disambiguate these construction semantics at call sites without relying on overloading by type ID value.

## `constructST`: The Type Dispatch Core

`constructST` is a variadic template constrained by the `ValidConstructSTArgs` concept, which restricts the argument pack to exactly one of two forms: `(SField)` for default construction, or `(SerialIter, SField)` for deserialization. This compile-time narrowing ensures that downstream `construct<T>(args...)` calls will always resolve to valid `ST*` constructors.

The function dispatches on `SerializedTypeID` via a `switch`, calling `construct<T>` for the matching concrete type. The vast majority of cases forward args directly. `STI_OBJECT` and `STI_ARRAY` are the exception: because `STObject` and `STArray` are themselves containers of `STVar` values, their deserialization constructors recursively create more `STVar` objects. To prevent unbounded recursion on maliciously crafted or corrupt data, these two types receive `depth` as an additional constructor argument. The `constructWithDepth` lambda uses `if constexpr` to select between the `(SField)` and `(SerialIter, SField, depth)` calling conventions at compile time.

## Nesting Depth Guard

The deserialization constructor enforces a hard limit of 10 levels of nesting:

```cpp
if (depth > 10)
    Throw<std::runtime_error>("Maximum nesting depth of STVar exceeded");
```

This is a security boundary. Without it, a crafted ledger object with deeply nested `STObject` or `STArray` fields could cause stack exhaustion. The depth counter is incremented by each recursive `STObject`/`STArray` constructor call and passed down through `constructST`.

## Relationship to Surrounding Code

`STVar` lives in `xrpl::detail` — it is an implementation detail, not part of the public API surface. Callers inside `STObject` and `STArray` use it as their element type, and `make_stvar<T>(args...)` (defined inline in the header) is the friend-function factory for creating `STVar` instances of a known concrete type without going through the type-ID dispatch. The virtual `copy()` and `move()` methods on `STBase` form the contract that each derived serialized type must fulfil to participate in `STVar`'s ownership model.