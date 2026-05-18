# `json_value.cpp` — Core JSON Value Type for XRPL

## Role in the System

This file implements `Json::Value`, the discriminated-union type that represents any JSON datum in the XRPL codebase. It is the foundational building block for all JSON construction, parsing, serialization, and RPC message handling throughout rippled. Every inbound API request and outbound response ultimately passes through `Value` objects; the class must therefore be both correct across all seven JSON types and efficient enough to be used freely in hot paths.

## Discriminated Union Design

`Value` stores its payload in a raw `union ValueHolder` tagged by a `ValueType` enum stored in an 8-bit bitfield. The union holds either a plain integer/double/bool scalar, a heap-allocated `char*` string, or a heap-allocated `ObjectValues*` — a `std::map<CZString, Value>`. Because both arrays and objects share the same `map_` branch, the distinction between `arrayValue` and `objectValue` is purely in how keys are interpreted: arrays use integer `CZString` keys, objects use string keys. This collapses two superficially different containers into one representation, keeping the union small and the switch-dispatch uniform.

The `allocated_` member is a one-bit field that tracks whether the `value_.string_` pointer is heap-owned, enabling the `StaticString` optimization: a `Value` constructed from a `StaticString` stores the raw pointer without copying, setting `allocated_` to zero so the destructor skips `free`. This is intentional and safe because `StaticString` is a compile-time tag signalling that the underlying C-string has static or program-lifetime storage. Hot-path code in rippled frequently uses `static const StaticString` constants as object keys to avoid heap allocation entirely.

## `CZString`: The Map Key Type

Object and array keys are stored as `Value::CZString` (C-Zero String), a private inner class that wraps either a `char*` or an integer index. The `DuplicationPolicy` enum — `noDuplication`, `duplicate`, `duplicateOnCopy` — drives whether the `CZString` owns its memory. Numeric array keys use the integer-path constructor `CZString(int index)`, leaving `cstr_` null; string object keys use the `char const*` constructor with an appropriate policy.

The copy constructor of `CZString` contains a subtle but important rule: if the source was marked `noDuplication` (i.e., it points at a static string), the copy preserves that status and does *not* copy the pointer. If the source owns a duplicate, the copy makes its own duplicate. This ownership discipline is what allows `operator[]` on an object to accept both transient `const char*` keys (which get duplicated into the map) and long-lived `StaticString` keys (which are stored by reference).

## Memory Management via `DefaultValueAllocator`

String memory is routed through a global `ValueAllocator*` obtained from the static function `valueAllocator()`, with `DefaultValueAllocator` as the sole concrete implementation. This allocator uses `malloc`/`free` directly rather than `new`/`delete`. The reason is historical: the original JsonCpp design allowed callers to swap in a custom allocator (a pool allocator, for example) without recompiling — hence the virtual interface. The `DummyValueAllocatorInitializer` global ensures the allocator singleton is constructed before `main()` to avoid static-initialization-order issues.

`duplicateStringValue()` accepts an optional explicit length so that `std::string` values (which know their length without a `strlen` call) can skip that scan. The `nullptr` path for the input string is handled gracefully: length is forced to zero and `memcpy` is skipped, producing a heap-allocated empty string.

## Copy, Move, and Swap Semantics

Copy-assignment uses the classic copy-and-swap idiom: a copy-constructed temporary is swapped in via `swap()`, which performs three `std::swap` calls on the raw members. This provides strong exception safety at the cost of an extra allocation when copying a string or map. Move construction simply steals the union members and resets the source to `nullValue`/`allocated_=0`, avoiding any heap operation. Move-assignment also uses swap, ensuring the stolen-from temporary's destructor properly cleans up whatever was in `this` before the move.

## Array Sizing Semantics

`size()` on an `arrayValue` returns the index of the last map entry plus one, rather than the actual entry count. This reflects a sparse-array model: if you write `arr[5] = x` on an empty array, `size()` returns 6, even though only one slot is occupied. The non-const `operator[](UInt)` auto-inserts `null` entries when needed. This differs from typical C++ container behaviour and can surprise callers who expect `size()` to equal the number of non-null elements.

## Type Coercion

The `asXxx()` accessors implement a permissive coercion lattice. `asInt()` accepts `null` (returns 0), `uintValue` (with range check), `realValue` (with range check), `booleanValue` (0/1), and even `stringValue` (via `beast::lexicalCastThrow`). `asAbsUInt()` is a XRPL-specific addition that returns the absolute value of any numeric type as an unsigned integer, handling the edge case of `INT_MIN` by casting through `int64_t` before negating to avoid overflow. Arrays and objects are never coercible to scalars; such attempts fire `JSON_ASSERT_MESSAGE` which aborts in debug builds.

`isConvertibleTo()` encodes these rules in predicate form without performing the conversion, allowing callers to check feasibility cheaply. The `realValue`-to-integer path additionally checks that the double has no fractional component (using `fabs(round(x) - x) < epsilon`) before approving the conversion.

## Mixed-Type Comparison

The free `operator<` and `operator==` handle the case where one operand is `intValue` and the other is `uintValue` through the helper `integerCmp(Int, UInt)`. Negative signed integers are immediately less than any unsigned value; non-negative signed integers are compared after safe widening. Without this, comparing `Value(-1) < Value(0u)` could yield the wrong answer due to implicit unsigned promotion. For all other type mismatches, comparison falls back to type-enum ordering, giving `Value` a total order suitable for use as a map key.

## Iterator Architecture

`ValueIteratorBase`, `ValueConstIterator`, and `ValueIterator` (implemented in the companion `json_valueiterator.cpp`, which is conceptually part of this translation unit) wrap `std::map::iterator` over `ObjectValues`. The `isNull_` flag handles the degenerate case of iterating over a `nullValue`: both `begin()` and `end()` return default-constructed iterators, and `computeDistance` between two null iterators returns zero rather than invoking undefined behaviour on an uninitialized `std::map::iterator`. The `key()` method on the iterator reconstructs a `Value` from the `CZString` — returning an integer `Value` for array indices or a string `Value` for object keys — making generic traversal straightforward.