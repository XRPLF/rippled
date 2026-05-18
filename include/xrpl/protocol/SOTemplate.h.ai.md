# `include/xrpl/protocol/SOTemplate.h`

## Role in the System

`SOTemplate.h` provides the compile-time/startup schema machinery for XRPL's serialized objects. Every transaction type, ledger entry type, and inner object in the XRP Ledger carries a typed binary representation encoded as an `STObject`. Before any `STObject` instance can validate its fields, serialize itself, or apply defaults, it must consult a schema that answers three questions for each possible field: Is this field present in this object type at all? Is it mandatory or optional? Does it allow default-value encoding?

This file defines the two building blocks for that schema: `SOElement` (a single field's schema entry) and `SOTemplate` (the complete ordered list of entries for one object type).

## `SOElement` — A Single Field's Schema Entry

`SOElement` pairs an `SField` reference with an `SOEStyle` value, optionally augmented with an `SOETxMPTIssue` tag for amount fields.

```cpp
std::reference_wrapper<SField const> sField_;
SOEStyle style_;
SOETxMPTIssue supportMpt_ = soeMPTNone;
```

The use of `std::reference_wrapper` rather than a raw pointer or a copy is deliberate: `SField` instances are immovable, non-copyable singletons that live for the process lifetime (see `SField.h`), and they are referenced throughout the system. Storing a reference wrapper instead of a pointer communicates the ownership model clearly and allows `SOElement` itself to be stored in `std::vector` — which requires copy/move semantics that `SField` cannot provide.

The constructor validates the wrapped field immediately via the private `init()` helper:

```cpp
void init(SField const& fieldName) const {
    if (!sField_.get().isUseful())
        Throw<std::runtime_error>(...);
}
```

`isUseful()` returns true only if `fieldCode > 0`, meaning the field is a known, named, serializable field — not the sentinel `sfInvalid` or `sfGeneric`. This guard prevents accidentally inserting placeholder fields into a live schema.

A second constructor is constrained to `STAmount` and `STIssue` typed fields only (via a C++20 `requires` clause) and takes an additional `SOETxMPTIssue` argument. This enforces that MPT support annotations can only be applied to fields that actually carry amounts or issue specifiers, not arbitrary field types.

## `SOEStyle` — Field Presence Semantics

```cpp
enum SOEStyle {
    soeINVALID  = -1,
    soeREQUIRED = 0,  // must be present
    soeOPTIONAL = 1,  // may be absent; if present, may carry the type's default value
    soeDEFAULT  = 2,  // may be absent; if present, must NOT carry the type's default value
};
```

The distinction between `soeOPTIONAL` and `soeDEFAULT` is subtle and important. For some fields, the presence of the field with its default value and the absence of the field have different protocol-level meanings (e.g., `QualityIn` on a trust line). `soeDEFAULT` communicates that when the field is present, it is holding meaningful non-default state — the system will serialize and validate it differently. The comment also notes that inner objects with default fields must be created via `STObject::makeInnerObject()`, not directly, to preserve this invariant.

## `SOETxMPTIssue` — Multi-Purpose Token Awareness

```cpp
enum SOETxMPTIssue { soeMPTNone, soeMPTSupported, soeMPTNotSupported };
```

This annotation on amount/issue fields records whether the transaction format supports Multi-Purpose Tokens (MPT) in that field. It allows the validation layer in `STObject` to check MPT compatibility at the schema level rather than in scattered per-transaction validation code. Fields that are not amount/issue types get the default `soeMPTNone`, which is never tested.

## `SOTemplate` — The Immutable Schema Object

`SOTemplate` holds the merged, ordered list of `SOElement` entries for one object type, plus a compact reverse-lookup index.

```cpp
std::vector<SOElement> elements_;
std::vector<int> indices_;  // field num -> element index
```

The constructor (implemented in `SOTemplate.cpp`) takes two separate vectors — `uniqueFields` and `commonFields` — and concatenates them into `elements_`. The split exists because `KnownFormats` separates fields specific to one transaction/object type from fields shared across all formats. After merging, the constructor builds the `indices_` table: a dense array indexed by `SField::getNum()`, pre-sized to `SField::getNumFields() + 1` and initialized to `-1` (unmapped). Each `SOElement`'s field number maps to the element's position in `elements_`. This gives O(1) field lookup in `getIndex()` and `style()` — a critical property since these are called during deserialization and validation of every ledger object.

The constructor enforces two invariants at initialization time: every field number must be within the valid range, and no field may appear twice in the same template. Both violations throw `std::runtime_error`, catching schema bugs at application startup rather than silently producing corrupt objects later.

`SOTemplate` is declared move-only (copy constructor and copy-assignment are deleted). The comment explains this clearly: copying a vector of `SOElement`s is expensive, and there is no existing use case that requires it. All consumers hold a `const*` or `const&` to a template owned by the `KnownFormats` registry.

## Relationship to `KnownFormats` and `STObject`

The canonical lifecycle is: at startup, `KnownFormats` subclasses (e.g., `TxFormats`, `LedgerFormats`, `InnerObjectFormats`) construct `SOTemplate` objects during singleton initialization, each receiving the `SOElement` list for one object type. These templates are then stored inside `KnownFormats::Item` objects whose addresses never move (stored in a `std::forward_list`).

At runtime, `STObject` holds a `SOTemplate const* mType` pointer. When an `STObject` is constructed or deserialized with a specific format, it calls `applyTemplate()` or `set(SOTemplate const&)`, which uses the template to validate present fields, fill in absent required fields, and reject unrecognized fields. The `style()` and `getIndex()` methods on `SOTemplate` are the workhorses of this validation pass.

The result is a schema system that is fully lock-free at runtime (templates are read-only after construction), requires no heap allocation per field lookup, and catches schema errors at process startup rather than during transaction processing.