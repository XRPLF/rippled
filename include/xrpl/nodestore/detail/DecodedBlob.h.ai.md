# `DecodedBlob.h` — NodeStore Binary Format Parser

## Role in the System

`DecodedBlob` is the deserialization half of the NodeStore binary format, paired symmetrically with `EncodedBlob`. Its job is to parse the raw key/value bytes that a storage backend (NuDB, RocksDB) has retrieved from disk and reconstruct the components needed to build a live `NodeObject`. The class comment makes an important guarantee explicit: this **defines the database on-disk format** for a `NodeObject`, making it the canonical reference for that contract.

## The On-Disk Format

The constructor comment in `DecodedBlob.cpp` documents the binary layout directly:

```
Bytes 0–7     Unused (historically stored ledger index in older versions)
Byte  8       NodeObjectType (one-byte enum)
Bytes 9–end   Object payload (the raw serialized ledger data)
```

The 8-byte prefix is a legacy artifact. Earlier code used those bytes to store a ledger index. `EncodedBlob` now writes them as eight zero bytes, and `DecodedBlob` silently ignores them. A developer comment in the implementation (`VFALCO NOTE What about bytes 4 through 7 inclusive?`) reflects the surviving uncertainty around the original intent, but the current code treats all eight as padding.

## Non-Throwing Validation Pattern

Rather than throwing on malformed input, `DecodedBlob` stores a success flag (`m_success`) and exposes it through `wasOk()`. This is a deliberate design choice for a storage layer that must handle real-world corruption gracefully. Callers follow a two-step idiom: construct, check, then conditionally materialize:

```cpp
DecodedBlob decoded(hash.data(), result.first, result.second);
if (!decoded.wasOk()) {
    status = dataCorrupt;
    return;
}
auto object = decoded.createObject();
```

`createObject()` asserts `m_success` (via `XRPL_ASSERT`) rather than throwing, making it a programming error to call it on a failed decode. This separates the "is this valid?" question from the "give me the object" action.

Validation is intentional and minimal: the constructor checks that `valueBytes > 8` (sufficient to read the type byte), then that `valueBytes > 9` (there is at least some payload), and that `m_objectType` maps to one of the four recognized `NodeObjectType` values — `hotUNKNOWN`, `hotLEDGER`, `hotACCOUNT_NODE`, or `hotTRANSACTION_NODE`. Notably, `hotDUMMY` (value 512, defined as an invalid sentinel) falls through the switch's default case and leaves `m_success = false`. The class header is honest that not all corruption is detected; this is a fast sanity check, not a cryptographic integrity proof.

## Memory Ownership Model

`DecodedBlob` is a non-owning view into the raw buffer provided by the caller. The private members `m_key` and `m_objectData` are raw `const` pointers that alias the incoming `key` and `value` arguments — no copies are made during parsing. The caller must keep the backing buffer alive while a `DecodedBlob` is live.

`createObject()` breaks that constraint by copying the payload bytes into a newly allocated `Blob`:

```cpp
Blob data(m_objectData, m_objectData + m_dataBytes);
object = NodeObject::createObject(m_objectType, std::move(data), uint256::fromVoid(m_key));
```

The resulting `NodeObject` owns its data independently. This means storage backends can safely release their fetch buffers immediately after `createObject()` returns.

## Relationship to `EncodedBlob`

`EncodedBlob` performs the inverse operation: given a `NodeObject`, it serializes the type byte into offset 8 and copies the payload starting at offset 9, producing the exact byte layout that `DecodedBlob` expects to parse back. The test in `Basics_test.cpp` exercises this round-trip directly — encoding a batch of objects and decoding each one, asserting `wasOk()` and verifying field-by-field equality. Together these two classes define and enforce the full on-disk contract for node objects in the XRPL nodestore.