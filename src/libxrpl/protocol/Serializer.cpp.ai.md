# `src/libxrpl/protocol/Serializer.cpp`

This file implements the binary serialization backbone of the XRP Ledger protocol. Two complementary classes live here — `Serializer` for writing and `SerialIter` for reading — together encoding and decoding the canonical wire format that underpins every transaction, ledger object, and cryptographic hash in the system.

## `Serializer`: the write side

`Serializer` wraps a `Blob` (`std::vector<unsigned char>`) and provides a family of typed append methods. Every `add*` method returns the **byte offset** at which the data was written, not a success/failure bool. This design choice is intentional: callers that need to patch a previously written field (for instance, filling in a length or a reserved slot after the fact) can use the returned offset as a seek position. The object grows on demand and can be pre-sized with a constructor hint to avoid early reallocations.

Multi-byte integers are always encoded big-endian by explicit byte-by-byte shifts (`(i >> 24) & 0xff`, etc.), which is more portable than relying on `memcpy`-with-endian-conversion. The `add32` and `add64` template overloads are constrained to types whose unsigned counterpart is exactly 32 or 64 bits, so passing a `long` on a platform where it is 64-bit won't silently use the wrong overload.

### HashPrefix safety

The overload `add32(HashPrefix p)` exists because `HashPrefix` is an opaque `enum class : std::uint32_t` representing the domain-separation prefixes prepended to data before signing or hashing (e.g., `TXN` for transaction IDs, `STX` for signing). The function includes a `static_assert` verifying that the underlying type is exactly `std::uint32_t`. Because these prefix values are part of the protocol and encoded into the wire format, any future accidental change to the enum's underlying type would cause silent corruption. The assert turns that into a compile error.

### Field ID encoding

`addFieldID(int type, int name)` implements the compact TLV field-tag scheme used throughout `STObject` serialization. Both type and name are validated (via `XRPL_ASSERT`) to be in the range 1–255. The encoding packs them into 1, 2, or 3 bytes:

- If both fit in 4 bits (< 16): a single byte `(type << 4) | name` — the common case for well-known fields.
- If only the type fits in 4 bits: two bytes, the first being `type << 4` with the name byte following.
- If only the name fits in 4 bits: two bytes, name then type (note the reversed order for the "uncommon type, common name" case).
- If neither fits in 4 bits: three bytes — a leading `0x00` sentinel, then type, then name.

The leading zero is the signal to the decoder that the next two bytes are a two-byte type+name pair. This space-efficient packing is meaningful at scale: XRPL ledger objects contain dozens of fields and millions of objects are stored, so shaving bytes off common field tags accumulates.

### Variable-length fields

`addVL` writes a variable-length-prefixed blob using XRPL's custom three-tier length encoding. `addEncoded(length)` picks the encoding width:

- 0–192: one byte (direct value).
- 193–12,480: two bytes, using a bias-193 offset formula.
- 12,481–918,744: three bytes, using a bias-12,481 offset formula.
- Above 918,744: throws `std::overflow_error`.

The static helper `encodeLengthLength(length)` returns the number of header bytes for a given data length, used in `addVL`'s post-condition assertion to verify the buffer grew by exactly `data_size + header_size` bytes. The corresponding `decodeLengthLength(b1)` and the three `decodeVLLength` overloads are the inverse map, dispatched by inspecting the first byte's range.

### `getSHA512Half`

This method computes SHA-512 over the current buffer and returns the first 256 bits of the result as a `uint256`. This is XRPL's standard hashing primitive — used to derive transaction IDs, inner node hashes in the ledger SHAMap, and signing digests. It delegates to `sha512Half` from `digest.h`, keeping the crypto dependency one level removed from the serialization primitive.

## `SerialIter`: the read side

`SerialIter` is a non-owning cursor into an existing byte buffer. It holds three state values: `p_` (the current read position), `remain_` (bytes left), and `used_` (bytes consumed since construction or the last `reset()`). The class header marks it `// DEPRECATED`, signaling an ongoing migration toward zero-copy `Slice`-based interfaces.

The three-variable design makes `reset()` cheap: `p_ -= used_; remain_ += used_; used_ = 0;`. Saving the original pointer and length separately would also work but would add two words of state; instead, `used_` serves as an offset to reconstruct the original position. This allows a caller to speculatively consume fields, check validity, and rewind without allocating anything.

All `get*` methods throw `std::runtime_error` on underrun — reading past the end of the buffer. This is intentional: protocol data arriving over the network must be treated as potentially malformed, and exceptions propagate cleanly through the STObject decode path.

### Signed vs. unsigned reads

`get16`, `get32`, and `get64` decode unsigned integers using explicit bit-shift assembly. `geti32` and `geti64` instead use `boost::endian::load_big_s32/s64`. The asymmetry exists because manual shift patterns (`(uint64_t(t[0]) << 24) | ...`) do not perform sign extension — for signed types with a set high bit, the result would be wrong. Boost's endian functions handle two's complement big-endian loading correctly for signed types.

### `getRawHelper` and the null-pointer guard

The private template `getRawHelper<T>` is the implementation behind both `getRaw` (returning a `Blob` copy) and `getVLBuffer` (returning a `Buffer` copy). When `size == 0`, the code skips the `memcpy` call entirely. The comment explicitly cites C99 §7.21.1/2: while a zero-byte `memcpy` is nominally defined, empty `Blob` and `Buffer` objects may have a null `data()` pointer, and passing null to `memcpy` even with a zero count is undefined behavior in C++. The guard is precise and purposeful.

`getVLDataLength()` reads the length prefix by first calling `get8()` on the leading byte, then dispatching through `Serializer::decodeLengthLength` to read 0, 1, or 2 additional bytes before assembling the final length via the appropriate `decodeVLLength` overload. The decoder is defined as `static` on `Serializer` because both the read and write paths share the same length encoding tables and it avoids duplicating the arithmetic.

## Relationship to the broader serialization stack

`STObject` and the rest of the `ST*` type hierarchy write themselves into a `Serializer` and read themselves back via `SerialIter`. The `addFieldID`/`getFieldID` pair forms the backbone of the tag-dispatch loop inside `STObject::makeFieldPresent` and the deserializer. `HashPrefix`-tagged hashes computed with `getSHA512Half` flow into the SHAMap node-hash chain and the signing pipeline in `Sign.cpp`. The `Slice`-returning `getSlice` is the preferred modern accessor since it avoids allocation; `getRaw` and `getVL` are retained for backward compatibility with older call sites that have not yet migrated.