# `include/xrpl/protocol/STObject.h`

## Role in the System

`STObject` is the foundational container for all structured data in the XRPL protocol — transactions, ledger entries, and inner objects alike. It occupies the center of the Serialized Type ("ST") hierarchy defined in `STBase.h`, providing the recursive composition model through which heterogeneous named fields are stored, serialized, deserialized, hashed, and inspected. Both `STTx` (transaction wire format) and `STLedgerEntry` (on-ledger state) are `final` subclasses of `STObject`, meaning everything in ledger I/O flows through this type.

## Architecture Overview

`STObject` inherits from two bases: `STBase`, the polymorphic root of all serialized types, and `CountedObject<STObject>`, a CRTP diagnostic aid that maintains a global lock-free count of live instances for health monitoring. Its internal storage is a `std::vector<detail::STVar>`, where `STVar` is a type-erased wrapper with a 72-byte small-object optimization — types that fit (most scalars, hashes, amounts) are stored directly in an aligned buffer without heap allocation; larger types fall back to `new`.

An `STObject` can operate in one of two modes:

1. **Free mode** (`isFree()` returns `true`, `mType == nullptr`): no schema constraint. Fields are added ad hoc, iteration order reflects insertion order, and optional-field semantics are not enforced.

2. **Template mode**: an associated `SOTemplate const*` pointer imposes a schema. Each field in the template has a `SOEStyle`:
   - `soeREQUIRED` — must be present; reading it always succeeds.
   - `soeOPTIONAL` — may be absent; reads return a default-constructed value or throw depending on the access path.
   - `soeDEFAULT` — present logically but elided from serialization when it equals the type's zero value; a space-saving canonicalization used for common fields like flags.

The template pointer is assigned either at construction (via the `SOTemplate const& type` constructors) or later via `applyTemplate()`. This deferred binding allows objects to be deserialized from raw bytes and then validated against a known schema.

## The Proxy System — Why It Exists

The older access interface exposes named methods like `getFieldU32()`, `setFieldU32()`, `getAccountID()`, etc. These are type-unsafe in the sense that nothing at compile time prevents passing the wrong `SField` for a given type; the mismatch is caught at runtime via `dynamic_cast` and throws `std::runtime_error("Wrong field type")`.

The modern interface, introduced through template machinery, uses `TypedField<T>` (a subclass of `SField` parameterized on the concrete ST type it carries) and `OptionaledField<T>` (a thin wrapper produced by `operator~(TypedField<T>)`) as keys to `operator[]` and `at()`. These carry the ST type at compile time, enabling statically checked access patterns:

```cpp
// Required field — returns T::value_type, throws FieldErr if absent
auto amount = obj[sfAmount];

// Optional field — returns std::optional<T::value_type>
auto dest = obj[~sfDestination];
```

The mutable overloads return proxy objects instead of raw values. `ValueProxy<T>` wraps a non-optional field: it is implicitly convertible to `T::value_type` for reading and supports `operator=`, `operator+=`, and `operator-=` for writing (with arithmetic constraints enforced via C++20 concepts `IsArithmetic`, `IsArithmeticCompatible`). `OptionalProxy<T>` additionally supports assignment from `std::nullopt_t` to remove the field, and exposes `operator bool()` / `operator~()` for presence testing.

Both proxy types share a common base class `Proxy<T>` that stores a pointer to the owning `STObject`, the `SOEStyle` of the field, and a typed pointer to the field descriptor. The `assign()` method on `Proxy<T>` handles the subtlety of `soeDEFAULT` fields: if a value is being set to the type's zero value, the field is made absent (via `makeFieldAbsent`) rather than storing an explicit default — preserving canonical wire format.

The design decision to return proxy objects rather than references is intentional: returning `T&` would expose internal storage whose address could be invalidated by concurrent modification elsewhere in the object (e.g., via `set()` or `emplace_back()`). Proxies also let the framework intercept writes to apply schema enforcement.

## Serialization and Hashing

The private `add(Serializer& s, WhichFields whichFields)` method controls whether signing-excluded fields are emitted. The `WhichFields` enum (values `omitSigningFields = false` and `withAllFields = true`) is carefully chosen to alias directly to the `bool` argument of `SField::shouldInclude()`, avoiding a translation step. `getSortedFields()` collects field pointers and sorts them by `SField::fieldCode` — this canonical ordering is critical for deterministic serialization and hash computation.

`getHash()` and `getSigningHash()` produce `uint256` hashes prefixed by a `HashPrefix` discriminator that scopes each hash domain. `addWithoutSigningFields()` serializes only the signing-eligible subset, which underpins multi-sig and single-sig transaction validation.

## Field Lifecycle and Presence Management

`getPField(field, createOkay)` is the core lookup primitive: it searches `v_` for a field matching the given `SField`. When `createOkay` is true and the field is absent, it is created (in template mode, as an `STI_NOTPRESENT` placeholder; in free mode, via `emplace_back`). `makeFieldPresent()` promotes a placeholder to a live value; `makeFieldAbsent()` demotes a live value back to a placeholder (for template-mode optionals) or removes it entirely (for free mode). `delField()` removes an entry unconditionally.

`emplace_back()` is a pass-through to the underlying `v_` vector, used during deserialization and by the proxy system when creating new fields in free objects.

## Iteration

The public `iterator` type is a `boost::transform_iterator` wrapping the inner `list_type::const_iterator`. The `Transform` functor maps each `STVar` to its contained `STBase const&` via `STVar::get()`. This hides the `STVar` indirection layer from callers iterating over the object's fields, presenting a clean sequence of `STBase` references.

## Error Handling

`FieldErr` (a `std::runtime_error` subclass declared inside `STObject`) is thrown by the proxy and `at()` accessors when a required field is absent or schema constraints are violated. The standalone `throwFieldNotFound()` helper is used by the legacy `getFieldByValue` / `getFieldByConstRef` / `setFieldUsingSetValue` private implementation templates, which all follow the same pattern: locate the field via `peekAtPField`, check for `STI_NOTPRESENT`, `dynamic_cast` to the concrete type, and throw on mismatch.

The `disengage()` method on `OptionalProxy<T>` explicitly prevents assignment of `std::nullopt` to `soeREQUIRED` or `soeDEFAULT` fields — required fields cannot be removed, and default-value fields are semantically always "present."

## Relationship to Sibling Types

`STArray` (forward-declared in this header) is a sibling ST container holding an ordered sequence of `STObject` instances. `STObject` provides `peekFieldArray()` / `peekFieldObject()` to obtain direct mutable references into nested containers without copying. The `makeInnerObject()` static factory creates a free-mode `STObject` intended as a nested structure inside an `STArray`, seeded with the `soeDEFAULT` fields from the enclosing schema. The `getFieldObject()` method, by contrast, returns an object by value — a copy that can be inspected but whose modification does not propagate back.