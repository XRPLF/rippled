/** @file
 *  Variable-length integer (varint) encoding for the NodeStore serialization
 *  layer.
 *
 *  Provides a base-127 variant of the Protocol Buffers LEB128 varint format.
 *  Small values (0–126) occupy exactly one byte; larger values expand up to
 *  10 bytes for a full 64-bit quantity. Used by `codec.h` for two purposes:
 *  the one-byte object-type discriminant prefix on every stored blob, and the
 *  decompressed-size prefix that precedes LZ4-compressed payloads.
 *
 *  @note The encoding uses base-127, not the standard base-128, so the byte
 *      value `0x7F` never appears as a payload byte. The continuation flag
 *      remains bit 7 (`0x80`), matching the structural appearance of protobuf
 *      varints.
 *
 *  @note All multi-definition functions (`readVarint`, `writeVarint`) are
 *      function templates with a defaulted `<class = void>` parameter solely
 *      to satisfy the ODR when the header is included in multiple translation
 *      units. They carry no template behaviour beyond that.
 */
#pragma once

#include <nudb/detail/stream.hpp>

#include <cstdint>
#include <type_traits>

namespace xrpl::NodeStore {

/** Tag type used to select the varint overloads of `read` and `write`.
 *
 *  Pass as the explicit template argument at call sites:
 *  @code
 *  read<varint>(is, u);
 *  write<varint>(os, type);
 *  @endcode
 *  The tag distinguishes these overloads from NuDB's built-in typed
 *  `read`/`write` functions for `uint8_t`, `uint16_t`, etc.
 */
struct varint;

/** Compile-time upper bound on the encoded byte width of type `T`.
 *
 *  `kMAX` is the maximum number of bytes that any value of unsigned type `T`
 *  can occupy when encoded as a base-127 varint. Use it to allocate
 *  stack-local buffers without dynamic allocation:
 *  @code
 *  std::array<std::uint8_t, varint_traits<std::size_t>::kMAX> buf{};
 *  @endcode
 *
 *  @tparam T An unsigned integer type. Instantiation with a signed type is
 *      disabled via SFINAE.
 */
template <class T, bool = std::is_unsigned_v<T>>
struct varint_traits;

/** Specialisation enabled for unsigned types. */
template <class T>
struct varint_traits<T, true>
{
    explicit varint_traits() = default;

    /** Maximum encoded byte count for type `T` under base-127 encoding. */
    static std::size_t constexpr kMAX = (8 * sizeof(T) + 6) / 7;
};

/** Decode a base-127 varint from a raw byte buffer.
 *
 *  Scans `buf` for continuation bytes (bit 7 set), then decodes using
 *  Horner's method from most-significant to least-significant byte so that
 *  `t = t * 127 + (byte & 0x7F)` reconstructs the original value.
 *
 *  @param buf    Pointer to the first byte of the encoded varint.
 *  @param buflen Number of bytes available in `buf`.
 *  @param t      Output parameter set to the decoded value on success;
 *      unmodified on error.
 *  @return Number of bytes consumed from `buf`, or `0` on error. Error
 *      conditions: `buflen == 0`, the continuation chain extends past
 *      `buflen`, or arithmetic overflow during accumulation.
 *  @note The zero value is handled as a special case because the
 *      overflow guard (`t <= t0`) would otherwise trigger spuriously when
 *      `t` remains zero after processing a single zero byte.
 */
template <class = void>
std::size_t
readVarint(void const* buf, std::size_t buflen, std::size_t& t)
{
    if (buflen == 0)
        return 0;
    t = 0;
    std::uint8_t const* p = reinterpret_cast<std::uint8_t const*>(buf);
    std::size_t n = 0;
    while (p[n] & 0x80)
    {
        if (++n >= buflen)
            return 0;
    }
    if (++n > buflen)
        return 0;
    // Special case for 0
    if (n == 1 && *p == 0)
    {
        t = 0;
        return 1;
    }
    auto const used = n;
    while (n > 0)
    {
        --n;
        auto const d = p[n];
        auto const t0 = t;
        t *= 127;
        t += d & 0x7f;
        if (t <= t0)
            return 0;  // overflow
    }
    return used;
}

/** Compute the encoded byte width of `v` without writing anything.
 *
 *  Mirrors the byte count that `writeVarint` would return for the same value.
 *  Use this to pre-compute output buffer sizes before encoding.
 *
 *  @tparam T An unsigned integer type.
 *  @param  v The value whose encoded size is needed.
 *  @return Number of bytes required to encode `v` as a base-127 varint
 *      (always >= 1).
 */
template <class T, std::enable_if_t<std::is_unsigned_v<T>>* = nullptr>
std::size_t
sizeVarint(T v)
{
    std::size_t n = 0;
    do
    {
        v /= 127;
        ++n;
    } while (v != 0);
    return n;
}

/** Encode `v` into the buffer at `p0` as a base-127 varint.
 *
 *  Writes bytes in least-significant-first order. Each byte carries a 7-bit
 *  payload in bits 0–6 (range 0–126); bit 7 is set on all bytes except the
 *  last, signalling that more bytes follow.
 *
 *  @param p0 Destination buffer. Must have capacity of at least
 *      `sizeVarint(v)` bytes; no bounds check is performed.
 *  @param v  The value to encode.
 *  @return   Number of bytes written (same as `sizeVarint(v)`).
 */
template <class = void>
std::size_t
writeVarint(void* p0, std::size_t v)
{
    // NOLINTNEXTLINE(misc-const-correctness)
    std::uint8_t* p = reinterpret_cast<std::uint8_t*>(p0);
    do
    {
        std::uint8_t d = v % 127;
        v /= 127;
        if (v != 0)
            d |= 0x80;
        *p++ = d;
    } while (v != 0);
    return p - reinterpret_cast<std::uint8_t*>(p0);
}

/** Read a varint from a NuDB input stream into `u`.
 *
 *  Advances the stream one byte at a time until a byte without the
 *  continuation bit is consumed, then delegates to `readVarint` over the
 *  accumulated span.
 *
 *  @tparam T Must be `varint`; the tag selects this overload over NuDB's
 *      built-in typed `read` functions.
 *  @param is The NuDB input stream to read from.
 *  @param u  Output parameter set to the decoded value.
 */
template <class T, std::enable_if_t<std::is_same_v<T, varint>>* = nullptr>
void
read(nudb::detail::istream& is, std::size_t& u)
{
    auto p0 = is(1);
    auto p1 = p0;
    while (*p1++ & 0x80)
        is(1);
    readVarint(p0, p1 - p0, u);
}

/** Write `t` as a varint into a NuDB output stream.
 *
 *  Reserves exactly `sizeVarint(t)` bytes in the stream and encodes `t`
 *  directly into that region via `writeVarint`.
 *
 *  @tparam T Must be `varint`; the tag selects this overload over NuDB's
 *      built-in typed `write` functions.
 *  @param os The NuDB output stream to write into.
 *  @param t  The value to encode.
 */
template <class T, std::enable_if_t<std::is_same_v<T, varint>>* = nullptr>
void
write(nudb::detail::ostream& os, std::size_t t)
{
    writeVarint(os.data(sizeVarint(t)), t);
}

}  // namespace xrpl::NodeStore
