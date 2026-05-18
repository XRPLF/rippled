# `STObject.cpp` — Serialized Object Implementation

## Role and Context

`STObject` is the fundamental container type in the XRPL protocol layer. Every ledger entry, every transaction, and every inner object embedded within them is an `STObject` at its core. This file implements the lifecycle of those objects: construction from templates or wire bytes, field access and mutation, binary serialization, hashing, and JSON rendering.

`STObject` inherits from `STBase` (the abstract root of all serialized types) and acts as a typed heterogeneous map from `SField` keys to `STBase`-derived values. It sits one level above the raw serialization machinery (`Serializer`, `SerialIter`) and one level below transaction- or ledger-specific logic that relies on field accessors.

## Internal Storage: `v_` and `STVar`

Fields are stored in `v_`, a `std::vector<detail::STVar>`. `STVar` is a custom variant with a critical performance property: objects up to 72 bytes are stored inline in a stack-local `aligned_storage` buffer, avoiding heap allocation for the majority of small `ST*` types. Larger types fall back to `new`. The `STObject::copy()` and `STObject::move()` overrides delegate back into this mechanism, allowing `STVar` to reconstruct an `STObject` inside its own buffer when it fits.

## Templated vs. Free Mode

The most consequential design distinction in `STObject` is the two-mode lifecycle, controlled by `mType` (an `SOTemplate const*`):

- **Free mode** (`mType == nullptr`): Fields are stored in insertion order. `getFieldIndex()` performs a linear scan. The object accepts any field via `set()`, `getPField(field, createOkay=true)`, and `emplace_back()`. Free objects are how `STObject` is used during transaction building.

- **Templated mode** (`mType != nullptr`): `v_` is laid out in template order, with every slot pre-populated — `soeREQUIRED` fields carry a type-correct default, `soeOPTIONAL` and `soeDEFAULT` fields carry the `STI_NOTPRESENT` sentinel. `getFieldIndex()` delegates to `mType->getIndex()`, which is O(1) via an index array in `SOTemplate`. In templated mode, `set(STBase&&)` rejects unknown fields by throwing rather than appending.

The pre-population strategy means optional fields always have a slot — `isFieldPresent()` checks the sentinel type rather than the field's existence. `makeFieldPresent()` replaces a not-present slot with a default-value instance; `makeFieldAbsent()` does the reverse. `delField()` physically erases from `v_`, which is only semantically safe in free mode since templated slots are positionally indexed.

## Deserialization and Depth Guard

`set(SerialIter& sit, int depth)` drives wire deserialization. It reads `(type, field)` ID pairs, looks up the corresponding `SField`, and constructs each child via `STVar(sit, fn, depth+1)`. When the child field is itself an `STObject`, `applyTemplateFromSField()` is called immediately to bind the inner object to its known template if one exists.

Termination markers (`STI_OBJECT / field==1`) signal the end of the object. Encountering an `STI_ARRAY / field==1` inside an object is an error — cross-contaminated markers indicate malformed data. After all fields are read, the code checks for duplicates using `getSortedFields()` followed by `std::adjacent_find`, enforcing a fundamental ledger invariant: no `STObject` may contain two fields with the same `SField`.

The depth guard — `if (depth > 10) Throw<std::runtime_error>(...)` — is checked at the start of the `SerialIter` constructor. An attacker-controlled binary stream could otherwise force unbounded recursion through nested `STObject`/`STArray` structures; capping at depth 10 makes the worst-case stack consumption predictable.

## Template Application

`applyTemplate(SOTemplate const& type)` is used when an object was first deserialized in free mode and must then be validated against a known schema. It rebuilds `v_` from scratch in template order:

1. For each template slot, it searches the existing `v_` for a matching field. If found and the slot is `soeDEFAULT`, it rejects a field whose value equals the type default — explicitly encoding the default is prohibited by ledger rules.
2. Missing required fields (`soeREQUIRED`) throw `FieldErr`.
3. Any fields remaining in `v_` after template processing must be `isDiscardable()`. Non-discardable unknown fields throw.

The final `v_.swap(v)` atomically replaces the unordered deserialized data with the template-ordered, validated layout. `applyTemplateFromSField()` is a convenience wrapper that looks up the template from the global `InnerObjectFormats` singleton.

## `makeInnerObject` and Feature-Flag Logic

`makeInnerObject()` is a factory for inner objects that must carry template metadata. Its logic encodes a two-phase protocol amendment history:

- `fixInnerObjTemplate`: Added templates specifically to AMM inner objects (`sfAuctionSlot`, `sfVoteEntry`).
- `fixInnerObjTemplate2`: Extended templates to all remaining inner objects.
- If no `Rules` are available (pre-consensus or unit test context), templates are always applied.

This layered condition preserves backward compatibility: historical ledger entries serialized before these amendments lack the template structure, and replaying them requires not rejecting old data. The `getCurrentTransactionRules()` accessor reads the ambient transaction-processing context, making `makeInnerObject` implicitly context-sensitive.

## Serialization and Hashing

`add(Serializer& s, WhichFields whichFields)` is the canonical serialization path. It calls `getSortedFields()` to obtain a `fieldCode`-sorted view of present fields, then iterates through them, writing each field's type/ID header (`addFieldID`), its content (`field->add(s)`), and for nested objects/arrays, a termination marker (`s.addFieldID(sType, 1)`). The sort is mandatory — XRPL's binary format is canonically ordered by field code, so producing the same bytes for the same logical content requires deterministic ordering regardless of insertion order.

`getHash()` prepends a `HashPrefix` (a domain-separation tag) and hashes all fields. `getSigningHash()` passes `omitSigningFields`, which causes `getSortedFields()` to exclude fields like `TxnSignature` — the hash the private key signs over must be independent of the signature itself.

## Equality and Equivalence

Two equality predicates exist with different semantics:

- `operator==` compares only fields that return `isBinary() == true`. Non-binary fields (metadata, computed fields) are excluded. The implementation is O(n²) by design — the comment acknowledges this — because it compares two unordered sets by matching each element in one against all elements in the other.

- `isEquivalent()` takes a shortcut when both objects share the same `mType` pointer, comparing field-by-field positionally. When templates differ, it falls back to comparing sorted field lists element-by-element. This fast path is sound because two objects referencing the same `SOTemplate` instance are guaranteed to have `v_` in the same order with the same slots.

## Field Accessor Patterns

The typed getters (`getFieldU32()`, `getAccountID()`, `getFieldAmount()`, etc.) are thin delegations to two template helpers: `getFieldByValue<T>()` for value types and `getFieldByConstRef<T>()` for reference types. The const-ref variants return references to function-local `static` empty values when the field is absent, rather than throwing — making them safe to call on optional fields without prior presence checks. `getFieldObject()` is exceptional: it calls `applyTemplateFromSField()` on the returned copy, so the caller receives a template-bound object even when the source was free.

Flag manipulation (`setFlag`, `clearFlag`, `isFlag`) operates on the `sfFlags` field — a `STUInt32` present in most ledger objects. `setFlag` uses `getPField(sfFlags, createOkay=true)` to auto-create the field in free objects if absent, while `clearFlag` uses the non-creating variant and returns false if flags are not present.