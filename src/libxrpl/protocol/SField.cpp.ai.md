# `SField.cpp` — Protocol Field Registry

## Role in the System

`SField.cpp` is the single authoritative source of truth for every named field in the XRPL binary protocol. Serialized types — transactions, ledger entries, validation messages, metadata — are all composed of typed, named fields. Every field that can appear in any XRPL binary object must be registered here before the process starts. This file performs that mass registration at static initialization time, building two global lookup tables that the rest of the codebase queries at runtime.

## The X-Macro Field Definition Pattern

The most architecturally significant choice in this file is its use of the X-macro technique via `<xrpl/protocol/detail/sfields.macro>`. That macro file contains the master list of all XRPL protocol fields (roughly 300+), written once using `TYPED_SFIELD` and `UNTYPED_SFIELD` invocations. The same file is included in two very different contexts:

- In `SField.h`, the macros expand to `extern` variable declarations, so every translation unit that includes the header can reference any field by name.
- In `SField.cpp`, the macros expand to actual `const` variable definitions, constructing each field object exactly once.

This design eliminates the need to maintain two synchronized lists. Any developer adding a new field only touches `sfields.macro`. The naming convention is enforced mechanically: the `sfName` token in each macro call becomes both the C++ variable name and the human-readable field name. The field name string is derived by stripping the `sf` prefix from the variable name via `std::string_view(#sfName).substr(2)`.

## Construction Access Control

`SField` objects are immutable value-typed descriptors that must exist for the lifetime of the process. Allowing arbitrary construction of new `SField` objects at runtime would break the registry invariants. To prevent this, the file defines `SField::private_access_tag_t` — a struct whose definition is forward-declared as `public` in the class but is only actually defined in `SField.cpp`. A file-local `static` instance named `access` is the only token that satisfies the constructor's first parameter requirement:

```cpp
static SField::private_access_tag_t access;
```

Every constructor call (`SField(access, ...)` or the macro-expanded `TypedField<T>(access, ...)`) must pass this token. Since `private_access_tag_t` is only constructible from within this translation unit, it is effectively a compile-time access control mechanism. No external code can create an `SField` — they can only look them up.

## Field Codes and the Dual Registry

Each field is identified by a 32-bit `fieldCode` computed as `(SerializedTypeID << 16) | fieldValue`. This packs the type family (e.g., `STI_UINT32 = 2`) and the per-type index into a single integer, making code comparisons O(1) and canonically ordered. Fields with the same index in different type families are entirely distinct.

The two static `unordered_map` members, `knownCodeToField` and `knownNameToField`, are populated in each constructor:

```cpp
knownCodeToField[fieldCode] = this;
knownNameToField[fieldName] = this;
```

Both maps hold raw `const` pointers. This is safe because every `SField` is a `const` global with program lifetime — there is no deallocation. The lookup functions `getField(int code)` and `getField(std::string const& fieldName)` simply query these maps, returning `sfInvalid` on a miss rather than throwing or returning null.

## Uniqueness Enforcement via `XRPL_ASSERT`

Both constructors check that neither the `fieldCode` nor the `fieldName` already appear in the maps before inserting:

```cpp
XRPL_ASSERT(!knownCodeToField.contains(fieldCode), "... : fieldCode is unique");
XRPL_ASSERT(!knownNameToField.contains(fieldName), "... : fieldName is unique");
```

`XRPL_ASSERT` expands to `ALWAYS_OR_UNREACHABLE`, which in turn expands to `assert()`. In a release build (`NDEBUG` defined), these checks compile away entirely, meaning a duplicate field in `sfields.macro` would silently shadow the earlier registration. When running under the Antithesis fuzzing instrumentation, failed assertions allow execution to continue rather than aborting. The practical implication is that duplicate-field detection is a debug-build-only safety net, not a runtime guarantee.

## Historical Outliers and Discardable Fields

Four fields bypass the macros and are constructed directly:

- `sfInvalid` (code `-1`) and `sfGeneric` (code `0`) use the two-argument constructor that accepts a raw `fieldCode` integer rather than separate type/value components. `sfInvalid` serves as the "not found" sentinel returned by lookup misses; `sfGeneric` is a catch-all for untyped contexts.
- `sfHash` and `sfIndex` are `STI_UINT256` fields with `fieldValue` of 257 and 258 respectively — values deliberately above 256. The `isDiscardable()` predicate returns `true` when `fieldValue > 256`, and `shouldInclude()` gates binary serialization on `fieldValue < 256`. These two fields therefore exist only in JSON representations of ledger state, carrying computed values (the hash of an object, its ledger key) that cannot be embedded in the binary encoding of the object itself.

## `TypedField<T>` and Compile-Time Type Safety

`TypedField<T>` is a thin template wrapper over `SField` that tags each field with the C++ type of its serialized payload (e.g., `SF_UINT32` is `TypedField<STInteger<uint32_t>>`). The constructor simply forwards all arguments to `SField`:

```cpp
template <class T>
template <class... Args>
TypedField<T>::TypedField(private_access_tag_t pat, Args&&... args)
    : SField(pat, std::forward<Args>(args)...)
```

The type parameter is never stored at runtime; it only matters to the template machinery in `STObject` and `STField` accessors that need to enforce that you can't read a `sfFlags` field as an `STAmount`. The `OptionaledField<T>` wrapper and the `operator~` overload provide syntactic sugar for expressing optional field access.

## `compare()` and Canonical Ordering

`SField::compare()` returns `-1`, `0`, or `1` in the style of a comparator, but uses `0` as a sentinel for "illegal combination" when either field has a non-positive code. This covers `sfInvalid` and `sfGeneric`. The ordering is purely by `fieldCode`, which means fields are sorted first by type family, then by their index within that family — matching the canonical binary serialization order defined by the XRPL protocol specification.