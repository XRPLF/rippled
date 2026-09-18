#pragma once

#include <xrpl/basics/contract.h>

#include <lz4.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace xrpl::compression_algorithms {

/**
 * LZ4 block compression.
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
        reinterpret_cast<char const*>(in),
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

    // NOLINTNEXTLINE(readability-suspicious-call-argument)
    if (LZ4_decompress_safe(
            reinterpret_cast<char const*>(in),
            reinterpret_cast<char*>(decompressed),
            inSize,
            decompressedSize) != decompressedSize)
    {
        Throw<std::runtime_error>("lz4Decompress: failed");
    }

    return decompressedSize;
}

/**
 * LZ4 block decompression.
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
    std::size_t copiedInSize = 0;
    auto const currentBytes = in.ByteCount();

    // Use the first chunk if it is >= inSize bytes of the compressed message.
    // Otherwise copy inSize bytes of chunks into compressed buffer and
    // use the buffer to decompress.
    while (in.Next(reinterpret_cast<void const**>(&chunk), &chunkSize))
    {
        if (chunkSize < 0)
            Throw<std::runtime_error>("lz4 decompress: invalid chunk size");

        auto const chunkSizeAsSizeT = static_cast<std::size_t>(chunkSize);

        if (copiedInSize == 0)
        {
            if (chunkSizeAsSizeT >= inSize)
            {
                copiedInSize = inSize;
                break;
            }
            compressed.resize(inSize);
        }
        auto const bytesToCopy =
            std::min(chunkSizeAsSizeT, inSize - copiedInSize);
        std::copy(chunk, chunk + bytesToCopy, compressed.data() + copiedInSize);

        copiedInSize += bytesToCopy;

        if (copiedInSize == inSize)
        {
            chunk = compressed.data();
            break;
        }
    }

    // Put back unused bytes
    auto const copiedInByteCount =
        static_cast<decltype(currentBytes)>(copiedInSize);

    if (in.ByteCount() > currentBytes + copiedInByteCount)
    {
        auto const unusedBytes =
            in.ByteCount() - currentBytes - copiedInByteCount;
        in.BackUp(static_cast<int>(unusedBytes));
    }

    if (copiedInSize != inSize)
        Throw<std::runtime_error>("lz4 decompress: insufficient input size");

    return lz4Decompress(chunk, inSize, decompressed, decompressedSize);
}

}  // namespace xrpl::compression_algorithms
