# `SOTemplate.cpp` — Serialized Object Schema Registry

## Role in the System

The XRPL ledger serializes every transaction and ledger object as a typed collection of named fields. `SOTemplate` is the compile-time-initialized schema that answers the question "which fields are legal here, and how are they required?" for each known object type. Every transaction format (`ttPAYMENT`, `ttOFFER_CREATE`, etc.) and every ledger entry format (`ltACCOUNT_ROOT`, `ltOFFER`, etc.) has exactly one `SOTemplate` that describes its field schema. The implementation in this file is short — 62 lines — but the two invariants it enforces at construction time are load-bearing for the entire serialization layer.

## Key Types

`SOTemplate` operates on two supporting types defined in `SOTemplate.h`:

- **`SField`** — a globally registered descriptor for a single serialized field (e.g., `sfAmount`, `sfDestination`). Each `SField` carries a permanent numeric identifier `fieldNum` assigned at registration time. `SField::getNumFields()` returns the total count of all registered fields.

- **`SOElement`** — a thin wrapper pairing an `SField` reference with a presence style (`soeREQUIRED`, `soeOPTIONAL`, `soeDEFAULT`) and an optional MPT-support flag for amount and issue fields. Its constructor calls `isUseful()` on the field, which rejects fields with a non-positive `fieldCode` — `sfInvalid` and similar sentinel fields cannot appear in a template.

The `SOEStyle` enum encodes the three presence rules that `STObject` enforces during serialization: `soeREQUIRED` means the field must always be present, `soeOPTIONAL` means it may appear with a default value, and `soeDEFAULT` means it may appear but must not carry the type's default value (and inner objects of this kind must be constructed through `STObject::makeInnerObject()`).

## Construction and the Two-List Design

The constructor accepts two separate field lists: `uniqueFields` and `commonFields`. The split is semantically meaningful at the `KnownFormats` layer — every transaction type has its own unique fields, but all transactions share a set of common fields (e.g., `sfFee`, `sfSequence`, `sfSignature`). By accepting them separately, callers can maintain that common set once and pass it to every format without duplicating the field descriptors. Inside the constructor the two lists are merged into a single `elements_` vector (unique fields first) and the distinction is discarded — from `SOTemplate`'s perspective the merged sequence is the authoritative ordered list.

There are two constructor overloads: one accepting `std::initializer_list<SOElement>` pairs (for the typical in-source literal syntax) and one accepting `std::vector<SOElement>` pairs (for programmatic construction). The initializer-list overload simply converts to vectors and delegates to the vector overload, so all real logic lives in one place.

## The Index Lookup Table

The core data structure is `indices_`, a `std::vector<int>` pre-sized to `SField::getNumFields() + 1` and filled with `-1` (meaning "not in this template"). After merging the element lists, the constructor iterates over `elements_` and writes the element's position `i` into `indices_[sField.getNum()]`. The result is a direct-address table: given any `SField`, `getIndex()` returns its position in `elements_` in O(1) with a single vector subscript — no hashing, no comparisons, no cache misses from pointer chasing.

This design is correct because `SField::fieldNum` values are dense integers assigned at static initialization time. Using `fieldNum` as a vector index gives O(1) without hash-map overhead, which matters because `getIndex()` is called on every field access during serialization and deserialization of every transaction or ledger object.

## Invariants Enforced at Construction

Two correctness checks are applied in the constructor loop:

**Range check.** Every field's `getNum()` must satisfy `0 < fieldNum < indices_.size()`. The lower bound guards against sentinel/invalid fields that carry non-positive field numbers. The upper bound guards against fields registered after this `SOTemplate` was constructed (which would produce an out-of-bounds write). Because `SField::getNumFields()` is called at construction time, the size is snapshotted — if additional fields were registered dynamically afterward, they could not be added to this template (which is intentional; templates are immutable after construction). The same range check is repeated in `getIndex()`, making lookups defensively safe even if a caller passes an unusual field.

**Duplicate check.** Before recording `indices_[sField.getNum()] = i`, the constructor calls `getIndex(sField)` and throws if the returned value is not `-1`. This catches programmer errors where the same `SField` appears twice in either list or once in each list. Since a duplicate would silently overwrite the earlier entry in `indices_`, breaking the field's position mapping, the check is necessary to catch mistakes that would otherwise produce silent data corruption.

Both checks throw `std::runtime_error` via `Throw<>`, the XRPL macro that ensures the exception propagates correctly even across ABI boundaries. Because `SOTemplate` objects are constructed at program startup (as part of `TxFormats::getInstance()`, `LedgerFormats::getInstance()`, etc.), any violation is a fatal programming error caught immediately during initialization — not during live transaction processing.

## Move-Only Semantics

The header explicitly declares `SOTemplate` as move-only. The comment acknowledges that copying both `elements_` and `indices_` vectors is expensive, and since templates are created once and queried many times, there is no legitimate use case for copying. `KnownFormats::Item` stores `SOTemplate` by value inside a `std::forward_list` (so item addresses never change), and it passes field vectors by move into the constructor, keeping allocation costs at construction time only.

## Relationship to `KnownFormats` and `STObject`

`KnownFormats<KeyType, Derived>` (the base class for `TxFormats`, `LedgerFormats`, and `InnerObjectFormats`) stores one `KnownFormats::Item` per registered format, and each `Item` owns an `SOTemplate`. When the deserialization layer processes a serialized blob, it retrieves the appropriate `Item` by transaction or ledger type, extracts its `SOTemplate`, and uses `getIndex()` to validate that each field in the blob is permitted and to read the associated `SOEStyle`. The template's iteration interface (`begin()`/`end()`) allows callers to enumerate all expected fields in definition order, which is used when constructing default-initialized objects and when verifying that all required fields are present.