# `include/xrpl/nodestore/detail/codec.h`

## Role in the System

This header is the compression gateway for the XRPL NodeStore. Every `NodeObject` blob written to or read from a NuDB backend passes through either `nodeobject_compress` or `nodeobject_decompress`. Its job is to define the on-disk wire format for stored blobs and provide the logic to encode and decode them, balancing storage efficiency against decoding speed.

The file sits in `nodestore/detail/`, signaling it is an implementation concern rather than a public API — callers are the NuDB backend (`NuDBFactory.cpp`) and the database import tool (`import_test.cpp`).

---

## On-Disk Format: Four Type Tags

Every stored blob begins with a varint type tag, dispatched by both `nodeobject_compress` and `nodeobject_decompress`:

| Type | Meaning |
|------|---------|
| `0`  | Uncompressed raw data (legacy; never written now) |
| `1`  | LZ4-compressed payload |
| `2`  | Compressed inner-node encoding (sparse, with a 16-bit bitmask) |
| `3`  | Full inner-node encoding (all 16 hashes present) |

The type 0 path still exists in the decoder for backward compatibility with older on-disk data, but `nodeobject_compress` has a comment explicitly noting "we always compress now" — `codecType` is hardcoded to `1` for all non-inner-node objects.

---

## Inner-Node Special Encoding (Types 2 and 3)

The most architecturally interesting code handles SHAMap inner nodes. These are exactly 525 bytes: a 13-byte header (`uint32` index, `uint32` unused, `uint8` kind, `uint32` `HashPrefix`) followed by 16 × 32-byte child hash slots. Their fixed size and high zero density make them amenable to a compact custom codec that out-performs general-purpose LZ4 for this common case.

**Detection.** The compressor detects inner nodes by checking two conditions: `in_size == 525` and that the 4-byte prefix field equals `HashPrefix::innerNode` (the `'M','I','N'` hash-prefix constant). This dual check guards against false positives from other 525-byte objects.

**Sparse encoding (type 2).** When fewer than all 16 child slots are populated, `nodeobject_compress` scans the 16 slots using `zero32()` (a zero-initialized 32-byte static sentinel) to identify non-empty slots. It builds a `uint16_t` bitmask where bit `0x8000` is slot 0 and packs only the non-zero hashes contiguously. The result: for a half-filled inner node (8 children), the stored size drops from 512 bytes of hashes to 2 (mask) + 256 (hashes) = 258 bytes, plus the varint type tag.

**Full inner node (type 3).** When all 16 slots are occupied, the bitmask is skipped entirely and all 512 hash bytes are stored directly after the type varint. This avoids spending 2 bytes on a mask that would be `0xFFFF`.

**Reconstruction.** On decompression, both type 2 and type 3 paths allocate 525 bytes and reconstruct the full blob, but with a critical detail: the `index`, `unused`, and `kind` fields are written as zeros (`hotUNKNOWN`), while `prefix` is written from `HashPrefix::innerNode`. This means the stored format intentionally discards the mutable metadata (ledger sequence, object type) and only retains the structural hash prefix and the child hashes themselves.

---

## LZ4 Primitives (`lz4_compress` / `lz4_decompress`)

These two low-level templates wrap the LZ4 C API behind the `BufferFactory` pattern. `lz4_compress` calls `LZ4_compressBound` to determine worst-case output size, then asks the factory for a single allocation large enough for both a leading varint (the original uncompressed size) and the compressed data. The varint is stored first so the decompressor can pre-allocate the output buffer in a single step rather than guessing or resizing.

`lz4_decompress` validates the varint first (including an explicit integer overflow check for both input and output sizes before casting to `int`), allocates the exact output size via the factory, then calls `LZ4_decompress_safe`. It verifies that the returned byte count matches the expected output size and throws `std::runtime_error` on any mismatch.

The `#define LZ4_DISABLE_DEPRECATE_WARNINGS` at the top works around a known incompatibility between clang's deprecation attributes and certain LZ4 API declarations, a pragmatic suppression that avoids polluting build logs.

---

## The `BufferFactory` Pattern

All six functions accept a `BufferFactory&&` — a callable of the form `void*(std::size_t n)` that allocates `n` bytes and returns the pointer. This design decouples allocation policy from codec logic entirely. In production use (NuDB backend), the caller passes a `nudb::detail::buffer`, which provides stack-local scratch space for small allocations and falls back to the heap for larger ones, avoiding a heap round-trip for every read. The codec never frees memory; ownership belongs entirely to the factory object the caller controls.

---

## `filter_inner()` and Codec Verification

`filter_inner` modifies an inner-node blob in place, zeroing the first 9 bytes (the `index`, `unused`, and `kind` fields). This is used by the NuDB import tool before calling `nodeobject_compress` so that the round-trip verification `memcmp` succeeds: since the compressor reconstructs inner nodes with those fields zeroed, comparing the raw original against the decompressed output would fail unless the source is first normalized. The function is a no-op for any blob that is not exactly 525 bytes or does not carry the `HashPrefix::innerNode` marker.

---

## Error Handling

All error paths throw `std::runtime_error` with descriptive messages that include runtime values (`in_size`, index `i`, etc.). Integer overflow is checked explicitly before any `static_cast<int>` conversion into the LZ4 API. Structural invariants (mask non-zero, exact byte counts consumed, exact output sizes) are each individually validated, so a corrupted on-disk blob produces a named error rather than undefined behavior.