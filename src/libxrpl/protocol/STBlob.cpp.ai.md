# STBlob.cpp — Variable-Length Binary Field Implementation

## Role in the System

`STBlob` is the XRPL serialized-type class that holds variable-length binary data (`STI_VL`) and account identifiers (`STI_ACCOUNT`). It sits at the bottom of the protocol's type hierarchy — `STBase` is the abstract root, and `STBlob` is one of the concrete leaf types that knows how to read itself from a byte stream, write itself back, and compare its contents. In ledger objects and transactions, fields like `SigningPubKey`, `TxnSignature`, and `Account` are all represented as `STBlob` instances at the serialization layer.

## Class Hierarchy and `emplace` Pattern

`STBlob` inherits from both `STBase` and `CountedObject<STBlob>`. The `CountedObject` mixin provides live-object counting for diagnostics, while `STBase` supplies the field name (`SField`) and the virtual dispatch interface common to all serialized types.

The two private methods `copy()` and `move()` exist solely to support `detail::STVar`, a type-erased container for polymorphic ST objects used inside `STObject`. Rather than owning ST values through heap-allocated `unique_ptr`s, `STVar` maintains a small fixed-size inline buffer and uses placement new when the object fits. The `STBase::emplace()` template captures this: if `sizeof(STBlob) <= n`, it constructs the value in `buf` via placement new and returns a pointer to it; otherwise it falls back to a regular `new` heap allocation. `copy()` and `move()` simply forward `*this` (by copy or `std::move`) into this helper, enabling the `STVar` machinery to clone or relocate blob fields without knowing their concrete type at the call site.

## Serialization and Deserialization

Deserialization is handled in the `SerialIter` constructor, which extracts the blob's payload by calling `st.getVLBuffer()`. This reads a variable-length-prefixed byte sequence from the stream and returns a `Buffer` — an owning heap-allocated byte array. The length prefix is decoded by `SerialIter` before this point, so the `STBlob` constructor receives already-delimited data and does nothing but store it.

Serialization goes through `add(Serializer& s)`, which calls `s.addVL(value_.data(), value_.size())`. The `addVL` family encodes the length prefix before the raw bytes, exactly the inverse of `getVLBuffer`. Before writing anything, `add()` enforces two `XRPL_ASSERT` invariants:

1. `getFName().isBinary()` — checks that the `SField`'s numeric field value is less than 256, which in the XRPL field numbering system distinguishes binary fields from non-binary ones.
2. `getFName().fieldType == STI_VL || getFName().fieldType == STI_ACCOUNT` — guards that only the two known wire types that use VL-encoding pass through this serialization path.

These assertions defend against a programmer mistake — constructing an `STBlob` around an `SField` of the wrong type, which would produce a malformed wire encoding. With the statically declared `SField` constants used everywhere in the codebase this should never fire in production, but the guards catch misuse early during development.

## The Dual-Type Design (`STI_VL` and `STI_ACCOUNT`)

Both `STI_VL` and `STI_ACCOUNT` fields are represented by the same `STBlob` class with the same wire encoding: a VL-prefixed byte string. An `STI_ACCOUNT` field is just a 20-byte blob (the account's `AccountID`). The distinction in `fieldType` carries semantic meaning — it controls how the JSON layer formats the value and how parsers validate it — but at the binary serialization level handled by this file, both cases go through the identical `addVL` path. This unification avoids duplicating the serialization logic while still preserving the type tag in the `SField` for higher-level interpretation.

## Supporting Methods

`getSType()` unconditionally returns `STI_VL`, which is the type code used by the protocol's field-ID encoding. When an `STBlob` is added to a containing `STObject` and written to the wire, the field-ID byte encodes `STI_VL` regardless of whether the field is semantically an account address or a raw blob.

`getText()` converts the raw bytes to uppercase hex via `strHex()`, which is used by `getFullText()` in `STBase` and ultimately surfaces in JSON output and log messages.

`isEquivalent()` performs a `dynamic_cast` to confirm the other `STBase` is actually an `STBlob`, then compares the underlying `Buffer` values. This byte-level equality check is used by `STBase::operator==` and ultimately drives transaction de-duplication and ledger comparison logic.

`isDefault()` returns `true` when `value_` is empty. The `STObject` serialization machinery uses this to skip optional fields that have not been set, keeping wire-format representations compact.