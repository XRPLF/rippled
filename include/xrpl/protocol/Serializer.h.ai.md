# `include/xrpl/protocol/Serializer.h`

This file defines two complementary classes — `Serializer` (write side) and `SerialIter` (read side) — that together implement the XRPL canonical binary serialization format. Every transaction, ledger object, and signed message that travels across the XRP Ledger network is encoded using this format, making these classes among the most foundational in the protocol stack.

## Role in the System

The XRPL binary format must be deterministic across all nodes: the same object must hash to the same value everywhere, which requires byte-exact serialization in a well-defined byte order. `Serializer` builds those byte streams by appending typed values in big-endian order. `SerialIter` then consumes them linearly, acting as a forward-only cursor. Both classes are marked `DEPRECATED` in varying places, reflecting an ongoing migration toward lower-level types (`Slice`, `Buffer`) that avoid copying, but they remain the primary mechanism for constructing signable and hashable blobs throughout the codebase.

## `Serializer` — The Write Side

`Serializer` wraps a single private `Blob` (`std::vector<unsigned char>`, itself marked DEPRECATED) and exposes an append-only API. Every `add*` method returns the byte offset at which it wrote, enabling callers to later locate specific fields for overwriting or inspection. The default constructor pre-reserves 256 bytes to avoid initial reallocations on typical transaction sizes.

**Integer appending** is split into three tiers:

- `add8` / `add16` are simple, non-templated methods.
- `add32<T>` and `add64<T>` are C++20-constrained templates accepting any type whose unsigned counterpart is exactly `uint32_t` or `uint64_t`. This lets the caller pass either `int32_t` or `uint32_t` to `add32` without casting, while the constraint blocks accidental narrowing from wider types at compile time.
- `addInteger<Integer>` dispatches to the above via explicit template specializations in the `.cpp` file, covering `unsigned char`, `uint16_t`, `uint32_t`, `uint64_t`, and `int32_t`.

A dedicated `add32(HashPrefix p)` overload exists for the four-byte hash space separators that prefix all XRPL hashing operations (e.g. `TXN` for transaction IDs, `STX` for signing). The static assert inside that overload guards that `HashPrefix`'s underlying type is permanently `uint32_t` — the comment notes this is an integral part of the protocol and must never change.

**Variable-length encoding** (`addVL`) prepends a 1–3 byte length header before the payload data:
- 0–192 bytes → 1-byte header (the length itself)
- 193–12,480 bytes → 2-byte header (`193 + high + low`)
- 12,481–918,744 bytes → 3-byte header (`241 + top + mid + low`)
- Larger → `std::overflow_error`

This compact encoding trades a lookup table for a small amount of arithmetic and ensures common short fields (account IDs, hashes, small blobs) need only a single prefix byte. `encodeLengthLength()` and `decodeLengthLength()` are the inverse functions; the three-overload family of `decodeVLLength` decodes the value from 1, 2, or 3 bytes respectively.

**Field ID encoding** (`addFieldID`) implements the XRPL field type/name tagging scheme. Type IDs and field name IDs are integers in [1, 255]. When both fit in the range [1, 15], they are packed into a single byte (`(type << 4) | name`). When only one fits, two bytes are used; when neither fits, three bytes with a zero lead byte disambiguate. This compact scheme means the vast majority of protocol fields — all standard `STI_*` types and the most common fields — are tagged in one byte, minimising overhead for the most frequent case.

**`getBitString<Bits,Tag>`** provides direct memcpy-based reading at a given offset into a `base_uint` (the fixed-width integer type underlying `uint256`, `uint128`, etc.), used for random-access extraction after construction.

**`getSHA512Half()`** is deprecated but still present. It computes the XRPL "SHA-512 half" hash (first 32 bytes of SHA-512) over the accumulated buffer, which was formerly the primary way to hash signable data before the digest utilities were factored out.

## `SerialIter` — The Read Side

`SerialIter` is a forward-only cursor over an external byte buffer. It stores three state values: `p_` (current position pointer), `remain_` (bytes not yet consumed), and `used_` (bytes consumed). The `reset()` method rewinds by subtracting `used_` from `p_` — this works because the underlying buffer is not owned and must outlive the iterator, a contract enforced by design (no copying of the input).

All `get*` methods throw `std::runtime_error` on underflow rather than returning error codes. This is a deliberate asymmetry with the `Serializer` getter (`get8` returns `bool`): `SerialIter` is expected to be used in parsing paths where malformed data is an exceptional condition and callers do not check returns at every step.

`getBitString<Bits,Tag>()` is templated and returns `base_uint<Bits,Tag>` constructed via `fromVoid`, providing zero-copy extraction of all the fixed-width types the protocol uses — `uint128`, `uint160`, `uint192`, `uint256`. The convenience wrappers `get128()`, `get160()`, `get192()`, `get256()` call it with the appropriate sizes.

`getFieldID()` mirrors `addFieldID` exactly, decoding the 1–3 byte compact encoding back into separate type and name integers. `getVLDataLength()` reads and decodes the VL header, advancing the cursor to the start of the payload; `getVL()` combines this with `getRaw` to return the full payload as a `Blob` (deprecated: returns a copy), while `getVLBuffer()` returns a non-copying `Buffer`.

`getSlice()` is the preferred zero-copy accessor: it returns a `Slice` pointing into the original buffer without allocating, advancing the cursor over the consumed bytes.

## Design Tradeoffs and Deprecation Trajectory

The `Serializer` class grew organically from XRPL's early codebase and carries significant surface area: multiple accessors for the same data (`peekData`, `getData`, `modData`, `getDataPtr`, `getDataLength`, `getLength`), vector-like methods (`begin`, `end`, `reserve`, `resize`, `capacity`), and even comparison operators against raw `Blob`. This breadth enabled easy integration across the codebase but also made the class a catch-all that mixes serialization concerns with container behavior.

The `DEPRECATED` annotations throughout — on `mData` itself, on `getSHA512Half`, on `SerialIter::getRaw` and `SerialIter::getVL` — signal that newer code should prefer zero-copy patterns using `Slice` and `Buffer` directly. `SerialIter::getSlice` is the forward-looking alternative to `getRaw`. The `Serializer` class itself remains necessary wherever mutable accumulation into a `Blob` is needed, but the long-term direction is to avoid materializing copies where the data can instead be processed in place.