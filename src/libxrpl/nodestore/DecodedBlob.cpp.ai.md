# `DecodedBlob.cpp` — NodeStore Binary Deserialization

`DecodedBlob` is one half of the NodeStore's binary serialization layer. Together with `EncodedBlob`, it defines the on-disk format that bridges the raw key/value pairs stored in backends (NuDB, RocksDB) and the typed `NodeObject` instances that the rest of the ledger logic consumes. This file implements the read direction: parsing a raw byte buffer back into a `NodeObject`.

## The On-Disk Format

The binary layout is documented inside the constructor and elaborated in `EncodedBlob.h`:

- **Bytes 0–7**: An 8-byte prefix, today always zeroed. Historically these bytes were used to store the ledger index (once or twice, in earlier versions of the code). The comment `// VFALCO NOTE What about bytes 4 through 7 inclusive?` reflects this legacy: the original field was probably 4 bytes, leaving the upper half undefined. `DecodedBlob` ignores all 8 bytes on read.
- **Byte 8**: A single `NodeObjectType` discriminant — `hotUNKNOWN`, `hotLEDGER`, `hotACCOUNT_NODE`, or `hotTRANSACTION_NODE`.
- **Bytes 9+**: The raw serialized payload of the node object.

The minimum useful buffer is therefore 10 bytes (9-byte header plus at least 1 byte of data). A buffer of exactly 9 bytes passes the type byte check but produces `m_dataBytes = 0`, which is still treated as a valid empty-payload object for recognized types.

## Two-Phase Decode-Then-Create Design

The class deliberately separates parsing from allocation. The constructor validates the buffer and extracts metadata; `createObject()` performs the only heap allocation. This allows callers to call `wasOk()` and discard corrupted records cheaply, without ever constructing a `NodeObject`. The constructor itself is allocation-free: `m_objectData` is simply a pointer into the original `value` buffer — no copy occurs until `createObject()` invokes `Blob(m_objectData, m_objectData + m_dataBytes)`.

## Validation Strategy

The constructor uses a `m_success` flag rather than exceptions. The header explains why: "it is possible to determine if the data is corrupted without throwing an exception." Since the NodeStore may surface data read from physical storage backends that could be partially corrupted or misformatted, a silent flag is more appropriate than propagating an exception through the storage read path.

Validation is layered:

1. **Size guards** — `if (valueBytes > 8)` gates the type byte read; `if (valueBytes > 9)` gates data pointer assignment. `m_dataBytes` is computed as `std::max(0, valueBytes - 9)`, guarding against a negative length if `valueBytes` is somehow small.
2. **Type cast** — `safe_cast<NodeObjectType>(byte[8])` converts the raw byte to the enum. In debug builds this asserts if the value is outside the enum's range, catching garbage bytes early.
3. **Type whitelist** — The `switch` statement is the semantic gate: only the four currently known object types flip `m_success` to `true`. An unrecognized byte — whether from a future format version or actual corruption — leaves `m_success = false` and prevents object creation. This makes the decoder safely forward-incompatible.

In `createObject()`, `XRPL_ASSERT(m_success, ...)` fires in debug builds if the caller violates the contract by calling it on a failed parse. The `if (m_success)` guard that follows provides defense in release builds where the assert compiles away, ensuring `createObject()` always returns a null `shared_ptr` rather than undefined behavior on a bad parse.

## Relationship to `EncodedBlob`

`EncodedBlob` is the symmetric encoder. It takes a `NodeObject`, zeroes the 8-byte prefix, writes the type at byte 8, copies the payload from byte 9 onward, and uses an inline stack buffer for payloads under ~1024 bytes to avoid heap allocation in the common case. `DecodedBlob` inverts exactly that layout. The two classes together constitute the complete on-disk schema for a NodeStore entry, and any change to the format must be mirrored in both.