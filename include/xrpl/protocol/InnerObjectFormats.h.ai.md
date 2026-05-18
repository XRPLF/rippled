# `include/xrpl/protocol/InnerObjectFormats.h`

## Role in the System

`InnerObjectFormats` is the central registry of structural schemas for all *inner object* types in the XRPL protocol. Where `TxFormats` catalogues top-level transaction layouts, `InnerObjectFormats` does the same for the serialized sub-objects that appear nested inside transactions and ledger entries — things like `sfSigner`, `sfSignerEntry`, `sfNFToken`, `sfAuctionSlot`, `sfPriceData`, and a dozen others. Without this registry, the serialization layer would have no authoritative way to know which fields are required, optional, or default-suppressed inside each kind of inner object.

## Design: CRTP Singleton Over `KnownFormats`

The class inherits from `KnownFormats<int, InnerObjectFormats>`, the CRTP template that provides generic format-management infrastructure: a `std::forward_list<Item>` for stable storage, and two `boost::container::flat_map` indices for O(log n) lookup by name and by key. The key type is `int` — specifically the numeric field code returned by `SField::getCode()` — rather than the `TxType` enum used by `TxFormats`. This is intentional: inner objects are already identified by their `SField` descriptors in wire format, so reusing the field code as the registry key avoids a separate enumeration.

The constructor is private. All 17 format entries (as of the current codebase) are registered in the constructor body via `KnownFormats::add()`, which simultaneously inserts each `Item` into both lookup maps. The `add()` call takes vectors of `SOElement` (field + `SOEStyle` pairs like `soeREQUIRED`, `soeOPTIONAL`, or `soeDEFAULT`) that together compose an `SOTemplate`. Because `std::forward_list` is node-based, the addresses of stored `Item` objects remain stable after insertion, so the flat maps can hold raw pointers safely.

`getInstance()` returns a `const&` to the lazily-initialized, function-local static — the standard Meyer's singleton pattern. The object is effectively immutable after construction; no public mutating interface is exposed.

## The Primary Lookup: `findSOTemplateBySField`

```cpp
SOTemplate const* findSOTemplateBySField(SField const& sField) const;
```

This is the only method added on top of the base-class API. It accepts an `SField` reference, reads its integer code via `getCode()`, delegates to `KnownFormats::findByType()`, and returns a pointer to the corresponding `SOTemplate`, or `nullptr` if the field isn't a known inner object type. Returning a raw (nullable) pointer rather than a `std::optional<std::reference_wrapper<…>>` is consistent with the rest of the XRP protocol codebase and avoids unnecessary wrapping overhead for what is essentially a table lookup on a hot serialization path.

## How the Registry Is Consumed

Three call sites illustrate the registry's role:

**`STObject::makeInnerObject()`** in `STObject.cpp` calls `findSOTemplateBySField` to attach the correct `SOTemplate` to a freshly created inner object, which causes `STObject::set(SOTemplate const&)` to pre-populate the object's fields with the correct default/non-present sentinels. Notably, this only applies when the network amendments `fixInnerObjTemplate` (for AMM objects `sfVoteEntry` and `sfAuctionSlot`) or `fixInnerObjTemplate2` (for all others) are enabled. This amendment gate reveals a historical detail: the registry predated its enforcement on the network, so the data model and its validation were shipped separately.

**`STObject::applyTemplateFromSField()`** calls `findSOTemplateBySField` to retroactively apply a template to an already-deserialized `STObject`. This is used when validating objects coming off the wire: unknown or disallowed fields are rejected, required fields are checked for presence, and `soeDEFAULT` fields are rejected if they carry their default value.

**Transaction handlers** (e.g., `NFTokenMint.cpp`, `OracleSet.cpp`, `TransactionSign.cpp`) call `findSOTemplateBySField` directly to obtain the schema for a specific inner object type before constructing or validating it, bypassing the general `STObject` machinery when targeted access is needed.

## Relationship to `SOTemplate` and `SOEStyle`

Each registered entry wraps an `SOTemplate`, which pairs field codes with `SOEStyle` constraints: `soeREQUIRED` means the field must be present, `soeOPTIONAL` means it may be absent or present with a non-default value, and `soeDEFAULT` means it may be absent but — if present — must carry a non-default value. This distinction matters for canonical serialization: a field serialized with its default value wastes wire space and is treated as a protocol violation for `soeDEFAULT` fields.

## Invariants and Safety

The private constructor guarantees that the registry is complete and consistent at the point `getInstance()` first returns. The `KnownFormats::add()` implementation guards against duplicate key registration with a `LogicError` call, so double-registering the same inner object type would abort the process immediately. Because `getInstance()` returns `const&`, callers cannot mutate the registry after initialization, and thread safety for reads-after-construction is guaranteed by the C++11 static initialization rules that `function-local static` relies on.