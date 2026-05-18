# `STArray.cpp` — Serialized Array of Inner Objects

`STArray` is the XRPL protocol's typed container for sequences of `STObject` instances. It sits at the core of the ledger's binary encoding: any transaction field that holds a list of sub-objects — `sfMemos`, `sfSigners`, `sfNFTokens`, and others — is represented as an `STArray` on the wire and in memory. This file provides the non-trivial method bodies; the inline accessors and iterator plumbing live in the header.

## Class Design

`STArray` inherits from `STBase`, the polymorphic root of all serialized types, and from `CountedObject<STArray>` for diagnostic object-count tracking. Its sole data member is `list_type v_`, a `std::vector<STObject>`. Because `STBase` carries a field-name pointer (`SField const* fName`) separately from the data, the move constructor and move assignment operator must explicitly call `setFName(other.getFName())` before moving `v_`. If these were left to the compiler, the field name association would be lost, causing downstream field-ID mismatches during serialization.

The copy constructor and copy assignment are compiler-defaulted, inheriting `STBase`'s shallow-copy semantics for the field name and `std::vector`'s deep copy for the elements. This is fine for `STArray` because `STObject` is itself copyable.

## Deserialization

The most architecturally significant code is the `SerialIter`-based constructor:

```cpp
STArray::STArray(SerialIter& sit, SField const& f, int depth) : STBase(f)
```

XRPL's binary format encodes an `STArray` as a sentinel-terminated sequence. The parser loops over field-ID tokens until it reads a token with `type == STI_ARRAY && field == 1`, which is the canonical end-of-array marker. This design means the array length is not encoded up front; the decoder must process tokens linearly, which is the same approach used for `STObject` (whose terminator is `STI_OBJECT, field == 1`).

Four distinct validation checks guard the loop body:

1. **End-of-array marker** (`STI_ARRAY, field 1`) — break cleanly.
2. **Misplaced end-of-object marker** (`STI_OBJECT, field 1`) — if this appears where an array terminator is expected, the stream is structurally corrupt. A `std::runtime_error("Illegal terminator in array")` is thrown.
3. **Unknown field** — `SField::getField(type, field)` returns a sentinel invalid `SField` for unrecognized `(type, field)` pairs. `fn.isInvalid()` catches this and throws `"Unknown field"`.
4. **Non-object element** — every element in an `STArray` must be an `STObject` (`STI_OBJECT`). If the field's `fieldType` is anything else, `"Non-object in array"` is thrown.

This fail-hard approach is intentional: a ledger object with a malformed or unexpected array element must never be silently accepted, since doing so would break consensus-level equivalence checks across nodes.

After each element passes validation, it is constructed directly into the vector with `v_.emplace_back(sit, fn, depth + 1)`. The `depth + 1` increment threads a recursion counter into each child `STObject`'s own deserialization constructor, which enforces a maximum nesting depth of 10. This is a defense against crafted payloads that could overflow the call stack through deeply nested object hierarchies.

Immediately after construction, `v_.back().applyTemplateFromSField(fn)` is called. The `fn` parameter — the field wrapping this inner object (e.g., `sfMemo`, `sfSigner`) — carries an `SOTemplate` registered in `InnerObjectFormats`. Applying the template validates the just-deserialized `STObject` against the known schema for that field type: unknown fields are rejected, required fields are checked, and fields carrying default values are flagged. This call can throw (`// May throw` is the explicit acknowledgment in the comment), and if it does, the partially constructed `STArray` is unwound by the exception. There is no partial-recovery logic — this is correct behavior because a partially valid ledger object is an invalid ledger object.

## Binary Serialization

The `add(Serializer& s)` method mirrors the deserialization loop structure. For each `STObject` element:

```cpp
object.addFieldID(s);   // write the element's typed field ID
object.add(s);          // write the element's content
s.addFieldID(STI_OBJECT, 1);  // write the per-element object terminator
```

Notably, `add()` does not write the outer array's own field ID — that responsibility belongs to the caller (usually `STObject::add()`), which calls `addFieldID()` on this `STArray` before calling `add()`. The outer array's end-of-array terminator (`STI_ARRAY, 1`) is similarly written by the parent, not here. This split of responsibility is consistent across all `STBase` subclasses.

## JSON Representation

`getJson()` emits a JSON array where each present element becomes a JSON object with a single key — the element's field name (from `getFName().getJsonName()`) — mapping to the element's own JSON representation:

```json
[
  { "Memo": { "MemoData": "..." } },
  { "Memo": { "MemoData": "..." } }
]
```

This wrapping in an outer object keyed by field name is critical for round-trip fidelity with the XRPL JSON API: it preserves the named-field context of each inner object, which would otherwise be lost in a flat JSON array. Elements with type `STI_NOTPRESENT` are skipped — these are placeholder entries that represent absent optional fields in a template-bound context.

`getText()` and `getFullText()` produce bracket-delimited comma-separated lists for human-readable debugging, with `getFullText()` delegating to `STObject::getFullText()` (which includes field names) while `getText()` uses the value-only form.

## Buffer-Placement Copy and Move

The `copy()` and `move()` overrides implement the in-place construction pattern inherited from `STBase`:

```cpp
STBase* STArray::copy(std::size_t n, void* buf) const {
    return emplace(n, buf, *this);
}
```

`STBase::emplace()` places the object into `buf` if it fits within `n` bytes; otherwise it heap-allocates. This allows the `detail::STVar` variant type — which holds heterogeneous `STBase` subtype values in a small fixed-size buffer — to avoid heap allocation for small objects while still handling arbitrarily large ones correctly.

## Equivalence and Default State

`isEquivalent()` uses `dynamic_cast` to confirm the compared object is also an `STArray`, then delegates to vector equality (`v_ == v->v_`), which cascades through `STObject::operator==`. `isDefault()` is simply `v_.empty()`, reflecting that an empty array is a no-op value that need not be encoded on the wire.

The `sort()` method accepts a raw function pointer (not a `std::function`) for ordering elements by caller-supplied criteria. This is used primarily when signing multi-signer transactions, where `sfSigners` must be canonically ordered by account ID to ensure deterministic serialization across all signing parties.