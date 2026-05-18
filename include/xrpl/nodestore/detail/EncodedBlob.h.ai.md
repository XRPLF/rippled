# `EncodedBlob` — NodeObject Serialization for Database Storage

`EncodedBlob` is a short-lived serialization adapter in the `xrpl::NodeStore` detail layer. Its sole job is to transform an in-memory `NodeObject` into the raw binary format that backend storage engines (NuDB, RocksDB) expect, without involving any intermediate string or vector. The class lives in `include/xrpl/nodestore/detail/` alongside its inverse counterpart `DecodedBlob`, together defining the on-disk format for all ledger node objects.

## Wire Format

The serialized payload produced by `EncodedBlob` has a fixed 9-byte header followed by the raw object data:

| Bytes | Content |
|---|---|
| 0–7 | Eight prefix bytes, always zero (historically stored the ledger index; zeroed since that was removed) |
| 8 | `NodeObjectType` cast to a single `uint8_t` — one of `hotLEDGER`, `hotACCOUNT_NODE`, `hotTRANSACTION_NODE`, etc. |
| 9–N | The raw payload blob from `NodeObject::getData()` |

The 32-byte database key is the object's `uint256` hash, stored separately from the payload. The two accessor methods `getKey()` and `getData()` expose these distinct pieces to backend drivers as `void const*` pointers suitable for direct hand-off to NuDB or RocksDB slice APIs.

## Stack-First Allocation Strategy

The most important design decision in this class is how it manages memory. `EncodedBlob` is always constructed on the stack — typically as a local variable in a backend's `store` or `do_insert` method — so the object itself costs nothing to allocate. The class takes full advantage of this by embedding a 1033-byte inline buffer (`payload_`) directly in the struct:

```cpp
std::array<std::uint8_t, boost::alignment::align_up(9 + 1024, alignof(std::uint32_t))>
    payload_{};
```

The `boost::alignment::align_up` call sizes the array to exactly cover the 9-byte header plus 1024 bytes of payload, rounded up to `uint32_t` alignment so that no compiler padding bytes follow. If the serialized size fits within this budget, `ptr_` points directly into `payload_` — no heap allocation occurs. If the payload exceeds 1024 bytes, the constructor falls back to `new uint8_t[size_]`. The comment in the source documents that roughly 94% of real-world node objects fall below the 1024-byte threshold, meaning heap allocation is rare in practice.

```cpp
ptr_((size_ <= payload_.size()) ? payload_.data() : new std::uint8_t[size_])
```

`ptr_` is declared `uint8_t* const` — it is set once at construction and never changes. This immutability makes the ownership semantics unambiguous: the destructor just tests `ptr_ != payload_.data()` to decide whether to `delete[]` the pointer. There is no copy constructor or move constructor; the class is inherently non-copyable because copying would require duplicating conditional ownership of a raw heap pointer.

## Invariant Enforcement

The destructor contains an `XRPL_ASSERT` that cross-checks both conditions simultaneously:

```cpp
XRPL_ASSERT(
    ((ptr_ == payload_.data()) && (size_ <= payload_.size())) ||
    ((ptr_ != payload_.data()) && (size_ > payload_.size())),
    ...);
```

This catches any state where the pointer and size have drifted out of sync — a situation that would otherwise lead to either a double-free or a memory leak. The assert fires in debug builds; in release builds the `if (ptr_ != payload_.data()) delete[] ptr_` branch handles cleanup unconditionally.

The constructor also defends against a null `shared_ptr` using `XRPL_ASSERT` followed by an explicit `throw`, which is an intentional redundancy: the assert fires early in debug builds, while the exception protects against the same bug in release builds where asserts are stripped.

## Relationship to `DecodedBlob`

`DecodedBlob` is the inverse transformation. It takes a raw `(key, value, valueBytes)` tuple from a database read, parses the 9-byte header, validates the type byte, and produces a `NodeObject` via `createObject()`. Where `EncodedBlob` produces the wire format, `DecodedBlob` consumes it — together they form the complete serialization boundary between the in-memory ledger graph and persistent storage.

## Usage Pattern

In both the NuDB and RocksDB backends, `EncodedBlob` is constructed immediately before the backend's insert call and discarded immediately after:

```cpp
// NuDBBackend::do_insert
EncodedBlob const e(no);
auto const result = nodeobject_compress(e.getData(), e.getSize(), bf);
db_.insert(e.getKey(), result.first, result.second, ec);

// RocksDBBackend::storeBatch
EncodedBlob const encoded(e);
wb.Put(
    rocksdb::Slice(...encoded.getKey()..., m_keyBytes),
    rocksdb::Slice(...encoded.getData()..., encoded.getSize()));
```

This pattern ensures the object's lifetime is scoped tightly around the I/O call, so any heap-allocated overflow buffer is freed immediately. The `[[nodiscard]]` attributes on the accessors prevent callers from accidentally discarding the return values of `getKey()`, `getSize()`, or `getData()`, which would make the entire operation a no-op.