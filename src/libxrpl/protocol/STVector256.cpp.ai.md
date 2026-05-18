# `STVector256.cpp` — Serialized Array of 256-bit Hash Values

`STVector256` is the XRPL serialized type for ordered lists of `uint256` values. On the wire it carries type identifier `STI_VECTOR256` (code 19) and appears throughout the ledger wherever a field must hold multiple 256-bit hashes — most visibly in `sfAmendments` (active feature flags), `sfIndexes` (directory-node page entries), and `sfHashes` (ledger history lists). The `.cpp` file implements the non-trivial methods; all simple accessors and mutations are inlined in the accompanying header.

## Class Design

`STVector256` inherits from two bases: `STBase` provides the named-field identity, `Serializer`/`SerialIter` integration, and the placement-new polymorphism contract; `CountedObject<STVector256>` hooks into the diagnostics subsystem so live instance counts can be monitored at runtime. The only data member is `mValue`, a `std::vector<uint256>`, deliberately private with the full `std::vector` API surfaced as thin inline forwarding wrappers. This layering lets callers treat the object as a first-class container while keeping the `STBase` invariant (only `STVar` and friend classes touch `copy`/`move`) intact.

## Deserialization Constructor

The most complex method in the `.cpp` is the `SerialIter`-based constructor:

```cpp
STVector256::STVector256(SerialIter& sit, SField const& name) : STBase(name)
{
    auto const slice = sit.getSlice(sit.getVLDataLength());
    if (slice.size() % uint256::size() != 0)
        Throw<std::runtime_error>(...);
    ...
}
```

The binary wire format stores the entire array as a single variable-length (VL-prefixed) blob — a length-prefix encoding shared by all variable-size fields. `getVLDataLength()` reads and decodes the prefix, then `getSlice()` returns a `Slice` into that many bytes of the stream. The guard that follows is the critical validation: if the blob's size is not an exact multiple of 32, the data is corrupt or truncated, and the constructor throws `std::runtime_error` rather than silently producing a partial array. On success, the constructor reserves capacity once and fills `mValue` in a single forward pass by constructing each `uint256` from a 32-byte sub-slice.

The choice to `Throw` here (versus an assertion) is deliberate: this path is exercised on untrusted network data during ledger deserialization. A mismatched size is a protocol violation that must be surfaced as an exception so the calling peer-management code can close the offending connection rather than crash.

## Serialization — `add()`

```cpp
void STVector256::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), ...);
    XRPL_ASSERT(getFName().fieldType == STI_VECTOR256, ...);
    s.addVL(mValue.begin(), mValue.end(), mValue.size() * (256 / 8));
}
```

`addVL` writes the total byte count as a VL prefix followed by the raw bytes produced by iterating over each `uint256`. The two `XRPL_ASSERT` guards enforce that the `SField` this object was constructed with is actually a binary, `VECTOR256`-typed field. These catch programmer errors — such as accidentally assigning a `STVector256` value to an `STBlob` field — early in debug builds, before bad data reaches the wire or the canonical hash computation.

## Copy/Move Placement Protocol

`copy()` and `move()` forward to `STBase::emplace()`, the template that implements the placement-new-or-heap pattern used by `STVar`:

```cpp
STBase* STVector256::copy(std::size_t n, void* buf) const { return emplace(n, buf, *this); }
STBase* STVector256::move(std::size_t n, void* buf)       { return emplace(n, buf, std::move(*this)); }
```

When the caller has pre-allocated a buffer large enough for an `STVector256`, `emplace` constructs in-place to avoid a heap allocation. When the buffer is too small it falls back to `new`. This matters because `STVar` — the type-erased wrapper used inside `STObject` to hold heterogeneous ST fields — relies on this protocol to keep small scalar fields on the stack while allowing larger or dynamically-sized types like `STVector256` to heap-allocate transparently.

## Default Value Semantics

`isDefault()` returns `true` when `mValue` is empty. In XRPL's canonical serialization rules, default-valued fields are omitted from the wire encoding, so an empty `STVector256` contributes nothing to a transaction hash. This is consistent with how all ST types behave — the absence of a field and an explicitly empty field have the same canonical representation.

## JSON Output

`getJson()` produces a `Json::arrayValue` where each element is the hex string of its `uint256`, via `to_string()`. The `JsonOptions` parameter is accepted but not consumed; `STVector256` has no output variants that depend on API version or date flags. The resulting JSON array is what appears in RPC responses for fields like `sfAmendments`.

## Relationship to Sibling ST Types

Within the `libxrpl/protocol` module, `STVector256` is one of the narrower concrete `STBase` specializations. Unlike `STBlob`, which holds arbitrary byte sequences, `STVector256` is exclusively typed for hash-sized values — this specificity is what justifies a distinct wire type rather than reusing `STBlob`. Fields that need a single hash use `STBitString<256>` (`uint256` directly); `STVector256` exists precisely for the multi-hash case. The `SF_VECTOR256` typedef in `SField.h` provides the typed-field wrapper that lets `STObject::getFieldV256()` and `setFieldV256()` give compile-time type safety at field-access sites.