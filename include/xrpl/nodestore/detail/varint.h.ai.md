# `include/xrpl/nodestore/detail/varint.h`

## Role and Purpose

This header provides a compact variable-length integer encoding scheme for the XRPL NodeStore's on-disk serialization layer. Variable-length integers are essential when embedding small discriminant values (object type codes) or size prefixes directly into binary blobs without reserving a fixed field width — common values like `0`, `1`, or `2` cost only one byte, while larger values expand automatically up to 10 bytes for a full 64-bit value.

The encoding is described in the file itself as a "variant of the base128 varint format from Google Protocol Buffers," though the actual implementation uses **base-127** rather than base-128. This is a subtle but intentional deviation: each 7-bit payload slot encodes digits in the range 0–126 (not 0–127), which means the byte value `0x7F` never appears as a payload byte. The continuation flag is still bit 7 (`0x80`), matching the structural appearance of protobuf LEB128.

## Concrete Usage in the NodeStore

`codec.h` is the immediate consumer. It uses varints in two roles:

1. **Object-type discriminant**: Every serialized `NodeObject` blob is prefixed by a varint type tag — `0` for uncompressed, `1` for LZ4, `2` for compressed inner node, `3` for full inner node. Since these values are all tiny, the discriminant always occupies exactly one byte.

2. **LZ4 decompressed-size prefix**: Before LZ4-compressed payload data, `lz4_compress` writes the original (decompressed) byte count as a varint. `lz4_decompress` reads it back to allocate the output buffer before calling `LZ4_decompress_safe`. The varint encoding avoids wasting a fixed 4 or 8 bytes for a size that is often small.

## API

### `varint_traits<T>`

A compile-time metafunction (SFINAE-guarded to `std::is_unsigned<T>`) that provides `::max` — the largest number of bytes a value of type `T` can ever occupy as a varint. The formula `(8 * sizeof(T) + 6) / 7` correctly bounds base-127 encoding for any unsigned width. `codec.h` uses this to allocate stack-local `std::array` buffers of the right size without dynamic allocation:

```cpp
std::array<std::uint8_t, varint_traits<std::size_t>::max> vi{};
```

### `write_varint(void* p0, std::size_t v)` → bytes written

Encodes `v` into the buffer at `p0` using a do-while that extracts successive base-127 digits in least-significant-first order. Each byte is `v % 127` in bits 0–6, with bit 7 set if more bytes follow. Returns the number of bytes written. The caller is responsible for ensuring the buffer is at least `size_varint(v)` bytes — there is no bounds check on the write path.

### `size_varint(T v)` → byte count

Computes `write_varint`'s return value without actually writing anything. Used by `codec.h` to pre-compute output buffer sizes.

### `read_varint(void const* buf, std::size_t buflen, std::size_t& t)` → bytes consumed or 0 on error

Scans forward through `buf` until it finds a byte without the continuation flag, then decodes the value using Horner's method in reverse order — highest-indexed byte first — so the accumulation `t = t * 127 + (d & 0x7F)` correctly reconstructs the original value. Returns `0` on three error conditions: empty buffer, continuation extends past `buflen`, or arithmetic overflow detected by checking `t <= t0` after each accumulation step.

The zero-value special case (`n == 1 && *p == 0` → return 1) is necessary because the overflow guard `t <= t0` would otherwise trigger when `t` stays zero after processing the single zero byte.

### `varint` tag struct and stream overloads

`varint` is a forward-declared empty struct used exclusively as a template tag. The two stream functions:

```cpp
template <class T, std::enable_if_t<std::is_same<T, varint>::value>* = nullptr>
void read(nudb::detail::istream& is, std::size_t& u);

template <class T, std::enable_if_t<std::is_same<T, varint>::value>* = nullptr>
void write(nudb::detail::ostream& os, std::size_t t);
```

integrate varint I/O with NuDB's streaming layer. The `read` overload advances the `istream` one byte at a time until the continuation bit clears, then calls `read_varint` on the accumulated span. The `write` overload allocates exactly `size_varint(t)` bytes in the `ostream` and calls `write_varint` directly into that region. Call sites in `codec.h` use these as `read<varint>(is, u)` and `write<varint>(os, type)`, with the tag distinguishing them from NuDB's own typed `read`/`write` overloads for `uint8_t`, `uint16_t`, etc.

## Design Notes

The `<class = void>` default template parameter on `read_varint` and `write_varint` — functions that have no actual template behaviour — is an ODR workaround. Making them function templates rather than plain functions allows the header to be included in multiple translation units without violating the One Definition Rule, which would otherwise require moving the implementations into a `.cpp` file.

The decision to use base-127 rather than the more standard base-128 (standard LEB128) wastes one value per byte position, very slightly increasing average encoded size. However, for the tiny type discriminants and moderate sizes actually stored, this has no practical impact — the maximum field size is identical (10 bytes for 64-bit values) and the difference in efficiency is negligible compared to the LZ4 payload compression applied to the object data itself.