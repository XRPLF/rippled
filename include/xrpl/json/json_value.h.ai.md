# `json_value.h` — Core JSON Value Type for XRPL

## Role in the System

This header defines the entire in-memory representation of JSON data used throughout the XRPL (rippled) codebase. It is the foundation on which all RPC request/response handling, transaction serialization diagnostics, and configuration parsing are built. Rather than depending on a third-party JSON library at the API boundary, the XRPL codebase maintains its own lean JSON implementation descended from the early JsonCpp library, giving it tight control over allocation, lifetime, and custom XRPL-specific type integration.

## The `Value` Class: A Discriminated Union

`Value` is the single workhorse type. It holds one of eight JSON types — `nullValue`, `intValue`, `uintValue`, `realValue`, `stringValue`, `booleanValue`, `arrayValue`, and `objectValue` — described by the `ValueType` enum. Internally, `Value` stores its data in a flat `union ValueHolder` with branches for `Int int_`, `UInt uint_`, `double real_`, `bool bool_`, `char* string_`, and `ObjectValues* map_`. The active branch is determined by the 8-bit `type_` field.

A single extra bit field, `allocated_` (declared as `int : 1` rather than `bool : 1` to avoid bitfield-boolean platform quirks), records whether `value_.string_` was heap-allocated and therefore needs freeing in the destructor. When a `Value` is constructed from a `StaticString`, `allocated_` stays `0` and the pointer is borrowed; when constructed from a `const char*` or `std::string`, the string is duplicated via `ValueAllocator` and `allocated_` is set to `1`. This allows a zero-overhead fast path for the common case of populating JSON objects with compile-time field names.

The copy constructor and copy-assignment operator perform deep copies: strings are duplicated through the allocator, and `ObjectValues` maps are heap-allocated fresh. Move semantics are also implemented — move construction pilfers `value_`, `type_`, and `allocated_` from the source and resets the source to `nullValue`, avoiding any allocation.

## The `CZString` Key Type and Unified Array/Object Storage

One of the most notable design choices is that both `arrayValue` and `objectValue` share the same underlying storage: `ObjectValues`, which is `std::map<CZString, Value>`. Array elements are stored using integer-indexed `CZString` keys constructed from `CZString(int index)`, while object members use string-keyed `CZString` instances. This unification means the implementation only has one map to manage, at the cost that arrays are slightly heavier than a `std::vector` would be. The trade-off favors implementation simplicity and the ability to iterate both arrays and objects through the same `ValueIteratorBase` infrastructure.

`CZString` itself is a private inner class implementing the map key. Its `DuplicationPolicy` enum (`noDuplication`, `duplicate`, `duplicateOnCopy`) dictates whether the key string is owned. When an object member is created from a `StaticString`, the `CZString` is built with `noDuplication`, avoiding a heap allocation for the member name. When a `const char*` key is used with a mutable `operator[]`, the name is duplicated via the allocator. The copy constructor of `CZString` correctly propagates ownership: a `noDuplication` key stays borrowed, a `duplicate` key triggers a fresh `makeMemberName()` call.

## The `StaticString` Optimization

`StaticString` is a lightweight tag type wrapping a `const char*`. Its constructor is `constexpr`, and its sole purpose is to signal to `Value`'s constructor and `operator[]` that the pointed-to string is a compile-time constant with program lifetime, so no duplication is needed. XRPL code commonly does:

```cpp
static const StaticString sfAccount("account");
object[sfAccount] = accountValue;
```

This avoids two heap allocations (for the member name and potentially the string value) that would occur if a `const char*` or `std::string` were used. The comparison operators between `StaticString` and `std::string` use `strcmp` directly on the underlying pointer, so no temporary strings are created during key lookup.

## Memory Management and `ValueAllocator`

All heap allocation for string values and member names flows through a global `ValueAllocator` singleton. The concrete implementation used at runtime is `DefaultValueAllocator` (defined in `json_value.cpp`), which uses `malloc`/`free` and `memcpy`. The abstract `ValueAllocator` interface exists as an extension point — labeled "Experimental do not use" in the header — that would allow a custom pool allocator to intercept all JSON string allocations. The singleton is initialized via a static `DummyValueAllocatorInitializer` to guarantee the allocator is available before `main()` runs.

## `xrpl::Number` Integration

The `Value(xrpl::Number const&)` constructor and the `to_json(xrpl::Number const&)` free function bridge XRPL's high-precision numeric type into JSON. Because JSON lacks a native type for XRPL's mantissa-exponent format, the conversion routes through `to_string(number)` and stores the result as a `stringValue`. This is the correct choice: XRPL amounts like IOU values exceed IEEE 754 double precision and must travel as strings to avoid rounding during serialization.

## Comparison and Ordering

`operator<` and `operator==` are `friend` functions comparing two `Value` instances. When types differ, there is special-case logic to correctly compare `intValue` against `uintValue` using `integerCmp`, which avoids the undefined behavior of mixing signed and unsigned in C++ comparisons. Same-type comparisons are type-switched directly against the union branch. For `arrayValue` and `objectValue`, comparison delegates to `std::map`'s own `operator<`, using `CZString::operator<` for key ordering, which falls back to `strcmp` for string keys and integer ordering for array indices.

## Iterator Design

`ValueIteratorBase` wraps `Value::ObjectValues::iterator` — the `std::map` iterator — and adds `key()`, `index()`, and `memberName()` accessors so callers can inspect whether they are iterating an array (integer key) or an object (string key). `ValueConstIterator` and `ValueIterator` extend the base with the standard bidirectional iterator interface. The `isNull_` flag on `ValueIteratorBase` handles the edge case of iterating over a `nullValue` — the iterators are valid but represent an empty range. Both iterator types expose `operator*` returning a reference to the mapped `Value`, with `ValueConstIterator`'s version being `const Value&`. The mutable `ValueIterator` can be constructed from a `ValueConstIterator`, but not the reverse, preserving const-correctness.

## Behavioral Invariants

- Accessing a non-existent member via non-const `operator[]` silently inserts a `nullValue` and returns a reference to it. This is the standard JSON library convention that allows nested construction like `root["tx"]["account"] = "r..."` without pre-creating intermediate objects.
- `clear()` removes all array elements or object members but does not change the `type_`. A `nullValue` on which `clear()` is called remains `nullValue`.
- `removeMember()` has a precondition that `type_` is `objectValue` or `nullValue`; it returns the removed value or `null`.
- `getMemberNames()` on a `nullValue` returns an empty `Members` vector without altering the type, matching the documented postcondition.