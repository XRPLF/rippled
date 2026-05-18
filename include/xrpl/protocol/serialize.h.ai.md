# `include/xrpl/protocol/serialize.h`

## Role in the System

`serialize.h` is a thin convenience header that sits at the top of the XRPL protocol serialization stack. It exposes two free functions — `serializeBlob` and `serializeHex` — that bridge the lower-level `Serializer` accumulator with the higher-level `STObject` (and any type that speaks the same `add(Serializer&)` protocol). The file's sole purpose is ergonomic: callers that need to turn a live protocol object into raw bytes or a hex string should not have to manually construct a `Serializer`, call `add`, and then extract the buffer every time they do it.

## Key Functions

**`serializeBlob<Object>(o)`** is a function template that works with any type implementing the `add(Serializer&) const` interface. The function constructs a default `Serializer` (pre-reserved to 256 bytes), invokes `o.add(s)` to let the object write its canonical wire encoding, and returns `s.peekData()` — a copy of the internal `Blob`. Because it is templated on `Object` rather than fixed to `STObject`, it can be called on any serializable protocol type (`STTx`, `STLedgerEntry`, metadata objects, etc.) without additional overloads.

**`serializeHex(o)`** is a thin non-template overload that accepts an `STObject const&` and delegates directly to `serializeBlob` followed by `strHex`. It is `inline` to avoid a separate translation unit, and its narrower type signature (concrete `STObject` rather than a template parameter) is intentional: hex output is almost exclusively needed for RPC responses where callers already hold an `STObject`, so no template instantiation overhead is paid.

## Design Decisions

The template/non-template split reflects actual usage patterns in the codebase. In practice, `serializeBlob` is the general-purpose primitive, while `serializeHex` covers the narrow but frequent RPC case of rendering a full transaction, ledger entry, or metadata blob as a hex string for JSON responses — as seen in `LedgerToJson.cpp` (producing `tx_blob` and `tx_meta` fields) and `LedgerData.cpp` (producing `data` entries for ledger objects).

The design deliberately avoids returning a `Serializer` object directly. `peekData()` returns a `Blob const&` (a `std::vector<uint8_t>`) by value copy, which means callers own the buffer without any lifetime tie to the transient `Serializer`. This is a safe, correct default: `Serializer` itself is marked partially deprecated internally (`mData` is tagged `// DEPRECATED`), and the public contract here hides those internal details.

The `add(Serializer&)` protocol is the canonical serialization interface across the XRPL type hierarchy — defined as a pure virtual in `STBase` and implemented by every concrete serializable type. By accepting any `Object` with that method in `serializeBlob`, the header remains open to future protocol types without modification, consistent with the rest of the XRPL type system's extensibility pattern.

## Relationship to Neighboring Files

- **`Serializer.h`** provides the byte accumulator (`Serializer`) and the `Blob`/`Slice` primitives that back the wire encoding. `peekData()` extracts the accumulated bytes as a `Blob const&`.
- **`STObject.h`** is the concrete object type accepted by `serializeHex`; its `add` implementation writes the full field-by-field canonical encoding.
- **`strHex.h`** provides the `strHex` utility that converts a byte range to an uppercase hex string, completing the `serializeHex` pipeline.