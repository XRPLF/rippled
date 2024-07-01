//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLED_COMPRESSIONALGORITHMS_H_INCLUDED
#define RIPPLED_COMPRESSIONALGORITHMS_H_INCLUDED

#include <ripple/basics/contract.h>
#include <algorithm>
#include <cstdint>
#include <lz4.h>
#include <stdexcept>
#include <vector>

namespace ripple {

namespace compression_algorithms {

/** LZ4 block compression.
 * @tparam BufferFactory Callable object or lambda.
 *     Takes the requested buffer size and returns allocated buffer pointer.
 * @param in Data to compress
 * @param inSize Size of the data
 * @param bf Compressed buffer allocator
 * @return Size of compressed data, or zero if failed to compress
 */
template <typename BufferFactory>
std::size_t
lz4Compress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    if (inSize > UINT32_MAX)
        Throw<std::runtime_error>("lz4 compress: invalid size");

    auto const outCapacity = LZ4_compressBound(inSize);

    // Request the caller to allocate and return the buffer to hold compressed
    // data
    auto compressed = bf(outCapacity);

    auto compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(in),
        reinterpret_cast<char*>(compressed),
        inSize,
        outCapacity);
    if (compressedSize == 0)
        Throw<std::runtime_error>("lz4 compress: failed");

    return compressedSize;
}

/**
 * @param in Compressed data
 * @param inSizeUnchecked Size of compressed data
 * @param decompressed Buffer to hold decompressed data
 * @param decompressedSizeUnchecked Size of the decompressed buffer
 * @return size of the decompressed data
 */
inline std::size_t
lz4Decompress(
    std::uint8_t const* in,
    std::size_t inSizeUnchecked,
    std::uint8_t* decompressed,
    std::size_t decompressedSizeUnchecked)
{
    int const inSize = static_cast<int>(inSizeUnchecked);
    int const decompressedSize = static_cast<int>(decompressedSizeUnchecked);

    if (inSize <= 0)
        Throw<std::runtime_error>("lz4Decompress: integer overflow (input)");

    if (decompressedSize <= 0)
        Throw<std::runtime_error>("lz4Decompress: integer overflow (output)");

    if (LZ4_decompress_safe(
            reinterpret_cast<const char*>(in),
            reinterpret_cast<char*>(decompressed),
            inSize,
            decompressedSize) != decompressedSize)
        Throw<std::runtime_error>("lz4Decompress: failed");

    return decompressedSize;
}

/** LZ4 block decompression.
 * @tparam InputStream ZeroCopyInputStream
 * @param in Input source stream
 * @param inSize Size of compressed data
 * @param decompressed Buffer to hold decompressed data
 * @param decompressedSize Size of the decompressed buffer
 * @return size of the decompressed data
 */
template <typename InputStream>
std::size_t
lz4Decompress(
    InputStream& in,
    std::size_t inSize,
    std::uint8_t* decompressed,
    std::size_t decompressedSize)
{
    std::vector<std::uint8_t> compressed;
    std::uint8_t const* chunk = nullptr;
    int chunkSize = 0;
    int copiedInSize = 0;
    auto const currentBytes = in.ByteCount();

    // Use the first chunk if it is >= inSize bytes of the compressed message.
    // Otherwise copy inSize bytes of chunks into compressed buffer and
    // use the buffer to decompress.
    while (in.Next(reinterpret_cast<void const**>(&chunk), &chunkSize))
    {
        if (copiedInSize == 0)
        {
            if (chunkSize >= inSize)
            {
                copiedInSize = inSize;
                break;
            }
            compressed.resize(inSize);
        }

        chunkSize = chunkSize < (inSize - copiedInSize)
            ? chunkSize
            : (inSize - copiedInSize);

        std::copy(chunk, chunk + chunkSize, compressed.data() + copiedInSize);

        copiedInSize += chunkSize;

        if (copiedInSize == inSize)
        {
            chunk = compressed.data();
            break;
        }
    }

    // Put back unused bytes
    if (in.ByteCount() > (currentBytes + copiedInSize))
        in.BackUp(in.ByteCount() - currentBytes - copiedInSize);

    if ((copiedInSize == 0 && chunkSize < inSize) ||
        (copiedInSize > 0 && copiedInSize != inSize))
        Throw<std::runtime_error>("lz4 decompress: insufficient input size");

    return lz4Decompress(chunk, inSize, decompressed, decompressedSize);
}

}  // namespace compression_algorithms

}  // namespace ripple

#endif  // RIPPLED_COMPRESSIONALGORITHMS_H_INCLUDED
