# `include/xrpl/protocol/SField.h` — Protocol Field Identification and Typed-Field System

## Role in the System

`SField.h` defines the complete compile-time type system for identifying and categorizing every data field that appears in XRPL binary-serialized objects: transactions, ledger entries, validations, and transaction metadata. Every piece of protocol data — from a `sfSequence` integer to a `sfSigners` array — is identified by an `SField` singleton. This file is the authoritative source for the field registry, the wire-type catalog, and the metadata flags that govern how each field is captured in transaction metadata on-ledger.

## `SerializedTypeID` — Wire Types via X-Macro

The `SerializedTypeID` enum defines the wire types recognized by the binary serialization codec. It is generated from a single `XMACRO` definition using the classic X-macro pattern: the macro is expanded once with `TO_ENUM` to produce enum values and again with `TO_MAP` to populate a `sTypeMap` from string names to integers. This keeps the type list in one place — adding a new serialization type requires only one line in `XMACRO`, and both the enum and the string-lookup map update automatically.

The encoding reflects XRPL's field encoding on the wire. The "common" types (codes 1–11) share a compact single-nibble representation, while "uncommon" types (16+) require an extra byte. Values 12–13 are reserved gaps, and high-level types (10001–10004) like `STI_TRANSACTION` and `STI_LEDGERENTRY` are container types that cannot be embedded inside other serialized objects.

## The `field_code` Packing Scheme

```cpp
inline int field_code(SerializedTypeID id, int index) {
    return (safe_cast<int>(id) << 16) | index;
}
```

Every `SField` has a single integer `fieldCode` that packs two dimensions into 32 bits: the upper 16 bits encode the `SerializedTypeID` and the lower 16 bits encode the field's position within that type. This combined key is both the lookup key in the registry and the canonical comparison value for serialization ordering. When the serializer writes fields in a canonical order (required for deterministic signatures), it sorts by `fieldCode` — which naturally groups all fields of the same type together and sequences them by their index within that type, exactly as the XRPL binary format specification requires.

## `SField` — Singleton Field Descriptors

Each `SField` instance describes one field in the protocol. All fields are created at static-initialization time in `SField.cpp` and live until program termination. Copy, move, and assignment are explicitly deleted — no duplicates can exist. This singleton guarantee is enforced at construction via `XRPL_ASSERT` checks on the `knownCodeToField` and `knownNameToField` maps.

The key architectural decision is the `private_access_tag_t` pattern. The tag type is defined as a public nested struct in `SField`, but its only definition (the `struct` body) lives in `SField.cpp`. The `.cpp` file then creates a single file-scoped `static SField::private_access_tag_t access` variable. Because the tag's constructor is private to that translation unit, only `SField.cpp` can ever pass an access tag to the `SField` constructors, making it impossible for any other code to construct new `SField` instances — a compile-time enforcement of the singleton factory pattern without needing a traditional registry class.

Fields are registered at construction into two static `unordered_map` tables — `knownCodeToField` (keyed by packed `fieldCode`) and `knownNameToField` (keyed by string) — enabling O(1) lookup in either direction via `getField()`. All overloads of `getField` converge on the integer-keyed map.

### Field Metadata Flags (`fieldMeta`)

Each `SField` carries an `int fieldMeta` bitmask controlling which ledger metadata events should record this field's value:

- `sMD_ChangeOrig` / `sMD_ChangeNew` — record before/after values on modification
- `sMD_DeleteFinal` — record value at deletion (e.g., `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` use `sMD_DeleteFinal` instead of the full default)
- `sMD_Create` — record value at creation
- `sMD_Always` — record whenever the containing node is touched (used by `sfRootIndex`)
- `sMD_BaseTen` — treat the value as base-10 for display rather than hex (used by MPT amount fields)
- `sMD_PseudoAccount` — signals that a 256-bit hash in this field represents a pseudo-account address (used by `sfAMMID`, `sfVaultID`, `sfLoanBrokerID`)
- `sMD_NeedsAsset` — the `STNumber` field requires an associated asset type before it can be serialized as a ledger object; used for vault/loan financial fields like `sfAssetsAvailable`, `sfDebtTotal`

The default `sMD_Default` is `sMD_ChangeOrig | sMD_ChangeNew | sMD_DeleteFinal | sMD_Create`.

### Signing vs. Non-Signing Fields

The `IsSigning` enum (`yes`/`no`) marks whether a field is included when computing a transaction's signing digest. Fields like `sfTxnSignature`, `sfSigners`, `sfMasterSignature`, `sfSignature`, and `sfCounterpartySignature` are all marked `notSigning` — they are excluded from signature hashing because they are themselves the signature data (or carry auxiliary signatures). This prevents a bootstrap paradox where the signature covers itself.

### Binary vs. Discardable Fields

`isBinary()` returns true when `fieldValue < 256`, indicating the field has a valid wire representation and can be serialized to binary. `isDiscardable()` returns true when `fieldValue > 256` — fields like `sfHash` and `sfIndex` have artificially high field values (257, 258) and exist only in the JSON representation; they cannot be round-tripped through binary serialization and are silently dropped during serialization.

## `TypedField<T>` and `OptionaledField<T>`

`TypedField<T>` extends `SField` with a compile-time `type` alias, allowing calling code to interact with a field with full knowledge of its C++ type at compile time (e.g., `SF_UINT32` is `TypedField<STInteger<uint32_t>>`). This enables type-safe access patterns in the serialized object API — a `TypedField<STAmount>` can only be used to retrieve an `STAmount` value.

`OptionaledField<T>` wraps a `const TypedField<T>*` to indicate that the field may be absent. The `operator~` overload on `TypedField` provides a concise syntax for constructing `OptionaledField`, allowing callers to write `~sfAmount` to express "this field is optionally present."

## The `sfields.macro` Two-Phase Include

The macro file `include/xrpl/protocol/detail/sfields.macro` is included twice with completely different macro definitions. In `SField.h`, `TYPED_SFIELD` and `UNTYPED_SFIELD` expand to `extern SF_##stiSuffix const sfName` declarations, making all field names visible throughout the codebase as `extern` symbols. In `SField.cpp`, the same macros expand to actual object definitions with constructor invocations. This technique eliminates all duplication between declaration and definition, ensuring every field declaration in the header automatically has exactly one matching definition in the translation unit — adding a new field requires only one line in the macro file.

The macro strips the `sf` prefix from the field name to produce the human-readable string name stored in `fieldName` (e.g., `sfSequence` → `"Sequence"`), using `std::string_view(#sfName).substr(2)` in `SField.cpp`.

## Canonical Field Ordering

`SField::compare()` returns -1, 0, or 1 based on `fieldCode` ordering, and returns 0 (illegal) for any comparison involving a sentinel field with a non-positive code. Because `fieldCode` encodes type in the high bits and field index in the low bits, the natural integer ordering produces the canonical XRPL binary serialization order required for deterministic transaction signing.