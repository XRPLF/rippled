/** @file
 *  Compression codec for NodeStore blobs written to and read from NuDB.
 *
 *  Every `NodeObject` value stored in the NuDB backend passes through either
 *  `nodeobjectCompress` or `nodeobjectDecompress`. The on-disk format is a
 *  leading varint type tag followed by a type-specific payload:
 *
 *  | Tag | Format |
 *  |-----|--------|
 *  | 0   | Uncompressed (legacy; readable but never written) |
 *  | 1   | LZ4-compressed payload |
 *  | 2   | Sparse inner-node (16-bit presence bitmask + non-zero hashes) |
 *  | 3   | Full inner-node (all 16 hashes, no bitmask) |
 *
 *  SHAMap inner nodes (exactly 525 bytes with `HashPrefix::InnerNode`) receive
 *  a specialized encoding that out-performs LZ4 on their typical hash density.
 *  All other objects are LZ4-compressed (type 1). The codec reconstructs inner
 *  nodes with `index`, `unused`, and `kind` fields zeroed, so those fields are
 *  not preserved across a round-trip.
 *
 *  All functions follow the `BufferFactory` pattern: callers supply a callable
 *  `void*(std::size_t)` that allocates output memory. The codec never frees
 *  memory; ownership remains with the caller's factory object.
 *
 *  @note This header is an implementation detail of the NuDB backend and the
 *      NodeStore import tool. It is not part of the public NodeStore API.
 *
 *  @see nodeobjectCompress, nodeobjectDecompress, filterInner
 */

#pragma once

// Disable lz4 deprecation warning due to incompatibility with clang attributes
#define LZ4_DISABLE_DEPRECATE_WARNINGS

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/detail/varint.h>
#include <xrpl/protocol/HashPrefix.h>

#include <nudb/detail/field.hpp>

#include <lz4.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace xrpl::NodeStore {

/** Decompress an LZ4-compressed blob produced by `lz4Compress`.
 *
 *  Reads a leading varint that encodes the original uncompressed size,
 *  allocates exactly that many bytes via `bf`, then calls
 *  `LZ4_decompress_safe` into the allocated buffer.
 *
 *  @tparam BufferFactory Callable with signature `void*(std::size_t n)` that
 *      allocates `n` bytes and returns a pointer to them. The codec does not
 *      free this memory; lifetime is governed by the caller.
 *  @param in Pointer to the compressed input buffer (varint prefix + LZ4 data).
 *  @param inSize Number of bytes at `in`.
 *  @param bf Factory used to allocate the decompressed output buffer.
 *  @return Pair of (pointer to decompressed data, decompressed byte count).
 *      The pointer is the buffer returned by `bf`.
 *  @throws std::runtime_error if `inSize` would overflow `int`, if the leading
 *      varint is missing or occupies the entire buffer, if the decompressed
 *      size would overflow `int`, or if `LZ4_decompress_safe` returns a byte
 *      count that does not match the expected output size.
 */
template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4Decompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    if (static_cast<int>(inSize) < 0)
        Throw<std::runtime_error>("lz4_decompress: integer overflow (input)");

    std::size_t outSize = 0;

    auto const n = readVarint(reinterpret_cast<std::uint8_t const*>(in), inSize, outSize);

    if (n == 0 || n >= inSize)
        Throw<std::runtime_error>("lz4_decompress: invalid blob");

    if (static_cast<int>(outSize) <= 0)
        Throw<std::runtime_error>("lz4_decompress: integer overflow (output)");

    void* const out = bf(outSize);

    if (LZ4_decompress_safe(
            reinterpret_cast<char const*>(in) + n,
            reinterpret_cast<char*>(out),
            static_cast<int>(inSize - n),
            static_cast<int>(outSize)) != static_cast<int>(outSize))
        Throw<std::runtime_error>("lz4_decompress: LZ4_decompress_safe");

    return {out, outSize};
}

/** Compress a raw blob using LZ4 and prepend the uncompressed size as a varint.
 *
 *  Allocates a single output buffer via `bf` sized for the varint prefix plus
 *  `LZ4_compressBound(inSize)` bytes (worst-case LZ4 output), then writes the
 *  varint followed by the compressed payload. The returned size reflects the
 *  actual compressed size, not the worst-case bound.
 *
 *  @tparam BufferFactory Callable with signature `void*(std::size_t n)` that
 *      allocates `n` bytes and returns a pointer to them. The codec does not
 *      free this memory; lifetime is governed by the caller.
 *  @param in Pointer to the uncompressed input data.
 *  @param inSize Number of bytes at `in`.
 *  @param bf Factory used to allocate the output buffer.
 *  @return Pair of (pointer to compressed output, compressed byte count
 *      including the varint prefix). The pointer is the buffer returned by `bf`.
 *  @throws std::runtime_error if `LZ4_compress_default` returns 0 (compression
 *      failure).
 */
template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4Compress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;
    std::pair<void const*, std::size_t> result;
    std::array<std::uint8_t, varint_traits<std::size_t>::kMAX> vi{};
    auto const n = writeVarint(vi.data(), inSize);
    auto const outMax = LZ4_compressBound(inSize);
    std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(n + outMax));
    result.first = out;
    std::memcpy(out, vi.data(), n);
    auto const outSize = LZ4_compress_default(
        reinterpret_cast<char const*>(in), reinterpret_cast<char*>(out + n), inSize, outMax);
    if (outSize == 0)
        Throw<std::runtime_error>("lz4 compress");
    result.second = n + outSize;
    return result;
}

/** Decompress a NodeStore blob encoded by `nodeobjectCompress`.
 *
 *  Reads the leading varint type tag and dispatches to the appropriate decoder:
 *  - Type 0: uncompressed legacy data — returned as a non-owning view into `in`.
 *  - Type 1: delegates to `lz4Decompress`.
 *  - Type 2: sparse inner-node — reads a 16-bit bitmask, then reconstructs a
 *      525-byte SHAMap inner-node blob with only the non-zero child hashes
 *      filled in and `index`/`unused`/`kind` fields zeroed.
 *  - Type 3: full inner-node — reads all 512 bytes of child hashes directly and
 *      reconstructs the 525-byte blob with metadata fields zeroed.
 *
 *  @tparam BufferFactory Callable with signature `void*(std::size_t n)` that
 *      allocates `n` bytes and returns a pointer to them. Not invoked for type 0
 *      (the returned pointer into `in` is valid only as long as `in` is alive).
 *  @param in Pointer to the encoded input buffer.
 *  @param inSize Number of bytes at `in`.
 *  @param bf Factory used to allocate decoded output for types 1–3.
 *  @return Pair of (pointer to decoded data, decoded byte count).
 *  @throws std::runtime_error if the type varint is missing, if any size check
 *      fails during inner-node reconstruction, or if the type tag is unrecognized.
 *  @note For type 0 the returned pointer aliases `in`; for types 1–3 it points
 *      into the buffer supplied by `bf`.
 */
template <class BufferFactory>
std::pair<void const*, std::size_t>
nodeobjectDecompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using namespace nudb::detail;

    std::uint8_t const* p = reinterpret_cast<std::uint8_t const*>(in);
    std::size_t type = 0;
    auto const vn = readVarint(p, inSize, type);
    if (vn == 0)
        Throw<std::runtime_error>("nodeobject decompress");
    p += vn;
    inSize -= vn;

    std::pair<void const*, std::size_t> result;
    switch (type)
    {
        case 0:  // uncompressed
        {
            result.first = p;
            result.second = inSize;
            break;
        }
        case 1:  // lz4
        {
            result = lz4Decompress(p, inSize, bf);
            break;
        }
        case 2:  // compressed v1 inner node
        {
            auto const hs = field<std::uint16_t>::size;  // Mask
            if (inSize < hs + 32)
            {
                Throw<std::runtime_error>(
                    "nodeobject codec v1: short inner node size: " + std::string("in_size = ") +
                    std::to_string(inSize) + " hs = " + std::to_string(hs));
            }
            istream is(p, inSize);
            std::uint16_t mask = 0;
            read<std::uint16_t>(is, mask);  // Mask
            inSize -= hs;
            result.second = 525;
            void* const out = bf(result.second);
            result.first = out;
            ostream os(out, result.second);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
            write<std::uint32_t>(os, static_cast<std::uint32_t>(HashPrefix::InnerNode));
            if (mask == 0)
                Throw<std::runtime_error>("nodeobject codec v1: empty inner node");
            std::uint16_t bit = 0x8000;
            for (int i = 16; i--; bit >>= 1)
            {
                if (mask & bit)
                {
                    if (inSize < 32)
                    {
                        Throw<std::runtime_error>(
                            "nodeobject codec v1: short inner node subsize: " +
                            std::string("in_size = ") + std::to_string(inSize) +
                            " i = " + std::to_string(i));
                    }
                    std::memcpy(os.data(32), is(32), 32);
                    inSize -= 32;
                }
                else
                {
                    std::memset(os.data(32), 0, 32);
                }
            }
            if (inSize > 0)
            {
                Throw<std::runtime_error>(
                    "nodeobject codec v1: long inner node, in_size = " + std::to_string(inSize));
            }
            break;
        }
        case 3:  // full v1 inner node
        {
            if (inSize != 16 * 32)
            {  // hashes
                Throw<std::runtime_error>(
                    "nodeobject codec v1: short full inner node, in_size = " +
                    std::to_string(inSize));
            }
            istream is(p, inSize);
            result.second = 525;
            void* const out = bf(result.second);
            result.first = out;
            ostream os(out, result.second);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
            write<std::uint32_t>(os, static_cast<std::uint32_t>(HashPrefix::InnerNode));
            write(os, is(512), 512);
            break;
        }
        default:
            Throw<std::runtime_error>("nodeobject codec: bad type=" + std::to_string(type));
    };
    return result;
}

/** Return a pointer to a zero-initialized 32-byte static buffer.
 *
 *  Used by `nodeobjectCompress` as a sentinel to detect empty child-hash
 *  slots in a SHAMap inner node via `memcmp`. The buffer is function-local
 *  static so it is initialized exactly once and lives for the process lifetime.
 *
 *  @return Pointer to a 32-byte buffer whose contents are all zero bytes.
 */
template <class = void>
void const*
zero32()
{
    static std::array<char, 32> kV{};
    return kV.data();
}

/** Compress a raw NodeStore blob into the NodeStore on-disk wire format.
 *
 *  Detects SHAMap inner nodes (exactly 525 bytes with `HashPrefix::InnerNode`
 *  at byte offset 9) and applies a specialized encoding:
 *  - Sparse (type 2): fewer than 16 child slots occupied — stores a 16-bit
 *      presence bitmask (bit 0x8000 = slot 0) followed by only the non-zero
 *      hashes packed contiguously.
 *  - Full (type 3): all 16 slots occupied — stores all 512 hash bytes directly,
 *      skipping the bitmask.
 *
 *  All other blobs are LZ4-compressed (type 1) via `lz4Compress`. Type 0
 *  (uncompressed) is never written; the `kCODEC_TYPE` constant is fixed at 1.
 *
 *  @tparam BufferFactory Callable with signature `void*(std::size_t n)` that
 *      allocates `n` bytes and returns a pointer to them. The codec does not
 *      free this memory; lifetime is governed by the caller.
 *  @param in Pointer to the uncompressed NodeStore blob.
 *  @param inSize Number of bytes at `in`.
 *  @param bf Factory used to allocate the encoded output buffer.
 *  @return Pair of (pointer to encoded data, encoded byte count).
 *  @throws std::runtime_error if LZ4 compression fails.
 *  @note Inner-node reconstruction zeros `index`, `unused`, and `kind` fields,
 *      so those fields are not preserved across a compress/decompress round-trip.
 *      Call `filterInner` on the source blob before round-trip verification.
 */
template <class BufferFactory>
std::pair<void const*, std::size_t>
nodeobjectCompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;

    if (inSize == 525)
    {
        istream is(in, inSize);
        std::uint32_t index = 0;
        std::uint32_t unused = 0;
        std::uint8_t kind = 0;
        std::uint32_t prefix = 0;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t>(is, kind);
        read<std::uint32_t>(is, prefix);
        if (safeCast<HashPrefix>(prefix) == HashPrefix::InnerNode)
        {
            std::size_t n = 0;
            std::uint16_t mask = 0;
            std::array<std::uint8_t, 512> vh{};
            for (unsigned bit = 0x8000; bit; bit >>= 1)
            {
                void const* const h = is(32);
                if (std::memcmp(h, zero32(), 32) == 0)
                    continue;
                std::memcpy(vh.data() + (32 * n), h, 32);
                mask |= bit;
                ++n;
            }
            std::pair<void const*, std::size_t> result;
            if (n < 16)
            {
                auto const type = 2U;
                auto const vs = sizeVarint(type);
                result.second = vs + field<std::uint16_t>::size +  // mask
                    (n * 32);                                      // hashes
                std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(result.second));
                result.first = out;
                ostream os(out, result.second);
                write<varint>(os, type);
                write<std::uint16_t>(os, mask);
                write(os, vh.data(), n * 32);
                return result;
            }
            auto const type = 3U;
            auto const vs = sizeVarint(type);
            result.second = vs + (n * 32);  // hashes
            std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(result.second));
            result.first = out;
            ostream os(out, result.second);
            write<varint>(os, type);
            write(os, vh.data(), n * 32);
            return result;
        }
    }

    std::array<std::uint8_t, varint_traits<std::size_t>::kMAX> vi{};

    constexpr std::size_t kCODEC_TYPE = 1;
    auto const vn = writeVarint(vi.data(), kCODEC_TYPE);
    std::pair<void const*, std::size_t> result;
    switch (kCODEC_TYPE)
    {
        case 1:  // lz4
        {
            std::uint8_t* p = nullptr;
            auto const lzr = NodeStore::lz4Compress(in, inSize, [&p, &vn, &bf](std::size_t n) {
                p = reinterpret_cast<std::uint8_t*>(bf(vn + n));
                return p + vn;
            });
            std::memcpy(p, vi.data(), vn);
            result.first = p;
            result.second = vn + lzr.second;
            break;
        }
        default:
            Throw<std::logic_error>("nodeobject codec: unknown=" + std::to_string(kCODEC_TYPE));
    };
    return result;
}

/** Normalize an inner-node blob in place before codec round-trip verification.
 *
 *  `nodeobjectCompress` reconstructs inner nodes with `index`, `unused`, and
 *  `kind` zeroed (those fields are not stored on disk). Comparing a raw source
 *  blob against the decompressed output would therefore fail unless the source
 *  is first normalized by zeroing the same fields. This function performs that
 *  normalization in place.
 *
 *  The function is a no-op for any blob that is not exactly 525 bytes or does
 *  not carry the `HashPrefix::InnerNode` marker at byte offset 9.
 *
 *  @param in Pointer to the blob to normalize. Modified in place when the blob
 *      is identified as a SHAMap inner node.
 *  @param inSize Number of bytes at `in`.
 *  @note This function is used by the NodeStore import tool prior to calling
 *      `nodeobjectCompress` so that the verification `memcmp` succeeds.
 */
template <class = void>
void
filterInner(void* in, std::size_t inSize)
{
    using namespace nudb::detail;

    if (inSize == 525)
    {
        istream is(in, inSize);
        std::uint32_t index = 0;
        std::uint32_t unused = 0;
        std::uint8_t kind = 0;
        std::uint32_t prefix = 0;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t>(is, kind);
        read<std::uint32_t>(is, prefix);
        if (safeCast<HashPrefix>(prefix) == HashPrefix::InnerNode)
        {
            ostream os(in, 9);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
        }
    }
}

}  // namespace xrpl::NodeStore
