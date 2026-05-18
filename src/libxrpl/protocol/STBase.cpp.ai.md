# `STBase.cpp` — Root of the XRPL Serialized-Type Hierarchy

`STBase` is the abstract root of every "serialized type" (ST) in the XRPL protocol. Every field that can appear in a transaction, ledger entry, validation, or metadata — integers, amounts, account IDs, path sets, nested objects, arrays — inherits from this class. The `.cpp` file implements the base-class layer: field-name management, comparison dispatch, textual and JSON rendering, binary serialization plumbing, and the small-object allocation interface used by `STVar`.

## The Field-Name Pointer

The single data member is `SField const* fName`. `SField` instances are compile-time singletons: they live for the entire program lifetime, are non-copyable and non-movable, and encode a `(fieldType, fieldValue)` pair that serves as the canonical identifier of a field in the XRPL binary format. Every `STBase` is always a named field; the default constructor ties it to `sfGeneric` — the generic sentinel — rather than leaving `fName` null.

`isUseful()` on `SField` returns `true` when `fieldCode > 0`, meaning the field has a real protocol-assigned identity. Fields with `fieldCode <= 0` (like `sfGeneric`) are placeholders with no wire representation.

## Assignment Operator Semantics

The copy-assignment operator has a deliberately non-obvious rule: it only copies `fName` from the right-hand side when `this->fName` is *not useful*. This is called out in a large warning comment in the header:

> Do not create a vector of any object derived from STBase. The copy assignment operator has semantics that will cause contained types to change their names when an object is deleted because copy assignment is used to "slide down" the remaining types and this will not copy the field name.

When `STObject` or `STArray` removes an element by sliding remaining elements down via copy assignment, it intentionally preserves the slot's existing (meaningful) field name rather than adopting the source element's name. Conversely, when a freshly-constructed object (holding only `sfGeneric`) is assigned a value, it inherits the name from the source. This dual-purpose assignment is a deliberate trade-off to avoid a separate "copy value, preserve name" API.

## Comparison

`operator==` and `operator!=` compose two checks: `getSType()` must match (same runtime type) and `isEquivalent()` must return `true`. The base `isEquivalent()` asserts — via `XRPL_ASSERT` — that it is only ever called on an instance whose type is literally `STI_NOTPRESENT`, i.e., a raw `STBase` object that was never overridden. All concrete subclasses override `isEquivalent()` to perform value comparison.

## Serialization Interface

`add(Serializer& s)` is the hook for binary serialization. The base implementation calls `UNREACHABLE()` and is marked `LCOV_EXCL` — it must never execute; every concrete type overrides it. `addFieldID()` handles the universal prefix step: encoding the field's type and index into the byte stream (required before the field's data), guarded by an assertion that the field is "binary" (`fieldValue < 256`), meaning it has a valid protocol wire encoding.

## Small-Object Placement via `copy()` / `move()`

The virtual `copy()` and `move()` methods delegate to the protected `emplace()` template:

```cpp
template <class T>
static STBase* emplace(std::size_t n, void* buf, T&& val)
{
    using U = std::decay_t<T>;
    if (sizeof(U) > n)
        return new U(std::forward<T>(val));
    return new (buf) U(std::forward<T>(val));
}
```

This is the small-object optimization interface consumed by `detail::STVar`. `STVar` maintains a 72-byte aligned inline buffer; if the concrete derived type fits within those bytes it is placement-new'd there, avoiding a heap allocation. If it is larger, a regular `new` is used. All `STBase` subclasses participate automatically by overriding `copy()` and `move()` to call `emplace()` with `*this`. The size threshold and the buffer are owned entirely by `STVar` — `STBase` only provides the mechanism for constructing into an externally supplied buffer.

## Text and JSON Rendering

`getText()` returns an empty string at the base level. `getFullText()` checks `getSType() != STI_NOTPRESENT` before emitting anything; if the field has a name (`hasName()` → `fieldCode > 0`), it prepends `"fieldName = "` to the result of `getText()`. `getJson()` simply delegates to `getText()`. Derived classes override `getText()` and optionally `getJson()` for richer output. The free `operator<<` streams `getFullText()`.

## `downcast()` Safety

The header defines `downcast<D>()` using `dynamic_cast`, throwing `std::bad_cast` on failure rather than returning null. This makes type-incorrect field access a hard error rather than a silent null dereference — consistent with the XRPL codebase's philosophy of asserting invariants aggressively.

## Invariants and Assertions

All `XRPL_ASSERT` calls in this file are business-logic invariants, not input validation. They fire only in debug builds (or assertion-enabled configurations). The four asserted invariants are: (1) `fName` is non-null after construction or `setFName()`; (2) `isEquivalent()` is never reached on a typed subclass without override; (3) `addFieldID()` is only called on fields with a wire representation. These assertions serve as documentation of the protocol contract rather than user-input guards.