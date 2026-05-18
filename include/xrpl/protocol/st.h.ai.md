# `include/xrpl/protocol/st.h` — Serialized Type Umbrella Header

`st.h` is an aggregation header that exposes the entire XRPL *Serialized Type* (ST) system through a single `#include`. Every type that can appear inside a transaction, ledger entry, or consensus message is declared in one of the headers it pulls in. Consumers that work with protocol objects at any depth — transaction processing, serialization, JSON conversion, RPC formatting — can include this one file instead of tracking individual ST dependencies.

## The Serialized Type System

The ST system is XRPL's canonical binary-to-object representation. Every field in a transaction or ledger object has a known wire type and a symbolic name (`SField`). That pairing is the foundation from which all ST types are built. `SField.h` (included transitively) defines the complete set of named fields — `sfAmount`, `sfDestination`, `sfFlags`, and so on — each carrying a `SerializedTypeID` that identifies which ST class holds it on the wire.

### `STBase` — the polymorphic root

`STBase` is the abstract superclass for all serialized values. Every instance holds a pointer to its `SField`, which gives the field its name and type code. The virtual interface is minimal: `getSType()`, `add()` (write to a `Serializer`), `getJson()`, `getText()`, `isEquivalent()`, and `isDefault()`.

Two private virtual methods, `copy(n, buf)` and `move(n, buf)`, implement a *small-buffer optimization* throughout the type tree. When an ST value needs to be copied into a container, the container provides a local buffer of size `n`. If the concrete type fits in that buffer it is placement-new'd there; otherwise it falls back to heap allocation. Every concrete ST class forwards to `STBase::emplace()` for this logic. The `detail::STVar` helper (declared in `STBase.h`, defined elsewhere) wraps one ST value inline, exploiting this protocol so that `STObject`'s internal field vector avoids a separate heap allocation per field for common small types.

`STBase::operator=` is intentionally asymmetric: it copies the *value* but not the *field name*. This is called out explicitly in a code comment because `std::vector` uses copy-assignment to slide elements down on erase, and if the name were overwritten the field identity would silently change. Callers must use `setFName()` explicitly when name propagation is needed.

### Scalar leaf types

`STInteger<Integer>` (in `STInteger.h`) is a template covering `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, and `int32_t`, with convenience aliases `STUInt8`, `STUInt16`, `STUInt32`, `STUInt64`, and `STInt32`. The template is fully header-inline; serialization asserts that the owning field is binary and that its declared type matches the template instantiation.

`STBitString<Bits>` handles fixed-width hash/ID values backed by `base_uint<Bits>`. The four concrete aliases — `STUInt128`, `STUInt160`, `STUInt192`, `STUInt256` — cover account IDs (160-bit), transaction hashes (256-bit), and related digests. A notable comment explains that the template parameter is `int` rather than `unsigned` to work around a gdb RTTI bug that breaks pretty-printers for templates instantiated over unsigned types.

`STBlob` wraps a variable-length byte sequence in a `Buffer` and exposes it as a `Slice`. It handles the transaction's `SigningPubKey`, `Signature`, memo data, and other variable-length fields. `STAccount` (from `STAccount.h`) is a specialized 160-bit serialized type carrying an `AccountID`.

### Composite types

`STObject` (in `STObject.h`) is the workhorse container. It stores an ordered heterogeneous sequence of `detail::STVar` values and optionally binds to an `SOTemplate` that describes which fields are required, optional, or default. The vector is accessed through a `boost::transform_iterator` that strips the `STVar` wrapper and exposes bare `STBase const&` to iterators. `STObject` provides typed accessors like `getFieldU32()`, `setFieldAmount()`, and field-presence queries; the `Proxy`/`ValueProxy`/`OptionalProxy` inner classes give those accessors reference-like semantics so field reads and writes look like value assignments.

`STArray` (in `STArray.h`) is a flat vector of `STObject`, used for multi-signer lists, memo arrays, and path sets of inner objects. Unlike `STObject`, it holds concrete (not polymorphic) elements, which is why it can use a plain `std::vector<STObject>` without the `STVar` indirection.

`STVector256` holds a `std::vector<uint256>` and is used for directory node pages and similar multi-hash fields. `STPathSet` and `STAmount` are more domain-specific: `STPathSet` encodes XRPL payment path graphs, while `STAmount` carries either XRP drops or an IOU `{currency, issuer, mantissa, exponent}` value in a single type.

### High-level domain objects

Three classes derive from `STObject` and represent the primary protocol objects:

**`STTx`** (in `STTx.h`) is the serialized transaction. It is `final`, not copy-assignable, and caches the transaction ID (`tid_`) and type (`tx_type_`) on construction. It adds sign/verify support (`sign()`, `checkSign()`), handles both single-signature and multi-signature validation against `Rules`, and can produce SQL metadata rows. The `sterilize()` free function round-trips an `STTx` through canonical serialization to ensure all equivalent wire forms produce identical digests.

**`STLedgerEntry`** (in `STLedgerEntry.h`) wraps an `STObject` as a ledger state entry. Its `key_` (`uint256`) is its position in the `SHAMap` tree; `type_` identifies the entry kind (AccountRoot, Offer, Escrow, etc.). The `SLE` alias is used ubiquitously in the codebase. `setSLEType()` (private) coerces the underlying `STObject` to the correct `SOTemplate` for the given `LedgerEntryType` on deserialization.

**`STValidation`** (in `STValidation.h`) carries a consensus validation vote. It extends `STObject` with a lazy signature-validity cache (`mutable std::optional<bool> valid_`), a trust flag, a `PublicKey`, and a `NodeID`. The constructor accepting a `SerialIter` takes a `lookupNodeID` callable so the caller can resolve ephemeral signing keys to master node identities (manifests). The constructor that creates a new validation signs it inline and verifies all required fields are present before returning.

`STParsedJSON` (in `STParsedJSON.h`) provides the reverse path: parsing a `Json::Value` tree into an `STObject` or `STArray`, used when transactions arrive over JSON-RPC.

## Why a single umbrella header

Most code that processes transactions or ledger entries needs many of these types simultaneously — a transaction contains amounts, account IDs, blob signatures, integer flags, and path sets. Requiring callers to enumerate each include individually would be fragile and create a maintenance burden as new ST subtypes are introduced. `st.h` makes the full type vocabulary available in one line and signals clearly that a translation unit is operating at the full protocol-object level rather than on individual primitives.