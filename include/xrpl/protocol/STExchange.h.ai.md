# `STExchange.h` — Type-Safe Bridge Between Serialized and Native C++ Types

## Role in the System

The XRPL wire protocol stores all ledger data in serialized form: integers as `STInteger<U>`, variable-length byte sequences as `STBlob`, and so on — all ultimately derived from `STBase`. Application logic operates on plain C++ types: raw integer scalars, `Slice` (a non-owning view), `Buffer` (an owning byte container). `STExchange.h` is the thin adapter layer that bridges these two worlds.

Without this file, every call site that reads or writes an `STObject` field would need to know which serialized subclass backs a particular field, perform its own `dynamic_cast`, and manually construct heap-allocated serialized objects. By centralizing that mapping in one traits header, the rest of the codebase can work with native C++ types through a uniform API.

## The `STExchange<U, T>` Traits Struct

The primary abstraction is the `STExchange` traits struct, parametrized on `U` (the serialized type, e.g. `STInteger<uint32_t>`) and `T` (the desired native C++ type). Each specialization provides:

- `value_type` — the canonical C++ representation for that serialized type
- `static void get(std::optional<T>&, U const&)` — extracts a native value from the serialized object
- `static std::unique_ptr<U> set(field, T const&)` — constructs a heap-allocated serialized object ready for insertion into an `STObject`

Using explicit template specializations rather than virtual dispatch or an inheritance hierarchy keeps all conversions fully resolved at compile time. There is no runtime polymorphism involved in the type mapping itself.

The partial specialization `STExchange<STInteger<U>, T>` covers the entire family of integer types (`STUInt8`, `STUInt16`, `STUInt32`, `STUInt64`, `STInt32`) generically with a single template. The `get` simply calls `u.value()` and the `set` calls `std::make_unique<STInteger<U>>(f, t)` — straightforward, since integers carry no ownership complexity.

The `STBlob` specializations are explicit full specializations rather than a partial one, and there are two of them: one for `Slice` and one for `Buffer`. This split reflects a deliberate ownership contract. `Slice` is a non-owning view over memory it does not control, so both `get` (which copies data out via `emplace`) and `set` (which copies data in via `data()/size()`) always make copies. `Buffer` owns its memory, so `STExchange<STBlob, Buffer>` provides an additional move-semantic overload of `set`: when the caller passes an rvalue `Buffer&&`, the `STBlob` is constructed with `std::move(t)`, avoiding any heap allocation beyond the `STBlob` itself. This is important in hot paths that build transaction objects.

## Free Functions: `get`, `set`, `erase`

### `get`

```cpp
template <class T, class U>
std::optional<T> get(STObject const& st, TypedField<U> const& f);
```

The implementation uses `STObject::peekAtPField` — a non-mutating lookup that does not insert a default value for absent fields, which is critical when merely inspecting an object. Two separate absence conditions are then checked: a null pointer (the field was never registered in the object's schema) and `STI_NOTPRESENT` (the field exists in the schema but has been explicitly marked absent). Only after both checks pass does the function `dynamic_cast` to `U const*`. The comment "This should never happen" on the failed-cast path is accurate: the `TypedField<U>` descriptor already encodes the serialized type at compile time, so a mismatch would be a programming error, not a data validation failure.

A second overload of `get` omits the explicit `T` parameter and infers it from `U::value_type`. This is the ergonomic default — callers write `get(st, sfSequence)` rather than `get<std::uint32_t>(st, sfSequence)`. The explicit-`T` overload exists for cases where you want a different view of the same underlying wire type, most practically when reading an `STBlob` field as either `Slice` (for temporary inspection) or `Buffer` (when you need ownership of the bytes).

### `set`

The primary `set` template uses `std::decay<T>` to strip cv-qualifiers and references before selecting the `STExchange` specialization, and `std::forward<T>` to preserve the value category so the move-semantic `Buffer&&` overload in `STExchange<STBlob, Buffer>` fires correctly when an rvalue is passed. Two additional overloads handle `STBlob` specifically: one accepts a `(size, init)` pair where `init` is a callable invoked to populate the blob in-place (useful for constructing large blobs without an intermediate copy), and one accepts a raw `(void const*, size_t)` pair for C-style interop.

### `erase`

`erase` delegates to `STObject::makeFieldAbsent`, which marks the field as not present in serialized output without removing it from the object's declared schema. This distinction matters for canonical serialization: the field slot still exists in the schema but contributes nothing to the wire encoding.

## Design Rationale

The separation of the type-mapping logic into a standalone traits layer — rather than embedding it in `STObject`, `STBlob`, or `STInteger` — is the key architectural decision here. If `STBlob` itself knew about `Buffer` and `Slice` conversions, or if `STInteger` knew about all the integer types, adding a new C++ view of an existing wire type would require modifying core protocol classes. With `STExchange`, a new specialization like `STExchange<STBlob, std::span<uint8_t>>` can be added in one place without touching any of the serialization infrastructure.

The `TypedField<U>` parameter used throughout all free functions enforces type safety at the field-descriptor level: passing a `TypedField<STUInt32>` where a `TypedField<STBlob>` is expected is a compile error, not a runtime `dynamic_cast` failure.