/**
 * @file Compression.h
 * @brief Overlay compression dispatch layer and wire-format constants.
 *
 * Sits between `Message` (which decides *whether* to compress) and
 * `CompressionAlgorithms.h` (which implements the codec). Centralises
 * algorithm selection, exception suppression, and the wire-format constants
 * that are shared across the send (`Message.cpp`) and receive
 * (`ProtocolMessage.h`) paths.
 */

#pragma once

#include <xrpl/basics/CompressionAlgorithms.h>
#include <xrpl/basics/Log.h>

namespace xrpl::compression {

/** Size in bytes of an uncompressed XRPL message header.
 *
 *  Layout: 4 bytes payload length (top 6 bits reserved/zero) + 2 bytes
 *  protobuf message type.
 */
std::size_t constexpr kHEADER_BYTES = 6;

/** Size in bytes of a compressed XRPL message header.
 *
 *  Layout: 4 bytes (compression algorithm in top 4 bits, payload length in
 *  remaining bits) + 2 bytes message type + 4 bytes original uncompressed
 *  size. The extra 4 bytes over `kHEADER_BYTES` is why `Message::compress()`
 *  only takes the compressed path when savings exceed 4 bytes.
 */
std::size_t constexpr kHEADER_BYTES_COMPRESSED = 10;

/** Compression algorithm identifier encoded in the wire-format header.
 *
 *  The encoding is load-bearing: `ProtocolMessage::parseMessageHeader()`
 *  detects compression via `*iter & 0x80`, extracts the algorithm via
 *  `*iter & 0xF0`, and validates reserved bits via `*iter & 0x0C == 0`.
 *  Enum values can therefore be extracted by masking the first wire byte with
 *  no further translation.
 *
 *  @note All values other than `None` must have the high bit set and the low
 *      nibble zero. For example, a future algorithm should use `0xA0`, `0xB0`,
 *      etc. `None = 0x00` signals an uncompressed message (high bit clear).
 *  @note Adding a new value here requires updating the dispatch branches in
 *      `compress()` and `decompress()`; the `UNREACHABLE` guards will catch
 *      any omission at runtime.
 */
enum class Algorithm : std::uint8_t { None = 0x00, LZ4 = 0x90 };

/** Controls whether a caller requests the compressed form of a message buffer.
 *
 *  Passed to `Message::getBuffer()` to select between the uncompressed
 *  `buffer_` and the lazily-compressed `bufferCompressed_`.
 */
enum class Compressed : std::uint8_t { On, Off };

/** Decompress a scatter-gather input stream into a contiguous output buffer.
 *
 *  Delegates to `lz4Decompress`, which accepts data arriving in discontiguous
 *  Boost.Asio chunks: it uses the first chunk directly when it is large enough
 *  to hold the entire compressed payload, and falls back to a contiguous copy
 *  otherwise. Any exception thrown by the underlying codec is caught and
 *  suppressed; the overlay receive path cannot propagate exceptions.
 *
 *  @tparam InputStream A `ZeroCopyInputStream` wrapping a `ConstBufferSequence`
 *      (typically `ZeroCopyInputStream<Buffers>` from `ZeroCopyStream.h`).
 *  @param in Scatter-gather stream positioned at the start of the compressed
 *      payload (i.e., after the 10-byte compressed header has been consumed).
 *  @param inSize Number of compressed bytes to read from `in`.
 *  @param decompressed Caller-allocated output buffer; must be at least
 *      `decompressedSize` bytes.
 *  @param decompressedSize Expected size of the decompressed message, taken
 *      from the 4-byte uncompressed-size field in the compressed wire header.
 *  @param algorithm Algorithm tag parsed from the wire header; defaults to
 *      `LZ4` (the only currently supported algorithm).
 *  @return Number of bytes written to `decompressed`, or `0` on any failure
 *      (including codec error or unrecognised algorithm).
 */
template <typename InputStream>
std::size_t
decompress(
    InputStream& in,
    std::size_t inSize,
    std::uint8_t* decompressed,
    std::size_t decompressedSize,
    Algorithm algorithm = Algorithm::LZ4)
{
    try
    {
        if (algorithm == Algorithm::LZ4)
        {
            return xrpl::compression_algorithms::lz4Decompress(
                in, inSize, decompressed, decompressedSize);
        }

        // LCOV_EXCL_START
        JLOG(debugLog().warn()) << "decompress: invalid compression algorithm "
                                << static_cast<int>(algorithm);
        UNREACHABLE(
            "xrpl::compression::decompress : invalid compression "
            "algorithm");
        // LCOV_EXCL_STOP
    }
    catch (...)  // NOLINT(bugprone-empty-catch)
    {
    }
    return 0;
}

/** Compress a contiguous input buffer using a lazy output allocator.
 *
 *  Delegates to `lz4Compress`, which calls `bf(capacity)` exactly once with
 *  the bound computed by `LZ4_compressBound(inSize)` to obtain an output
 *  pointer, then writes compressed bytes there. In practice `Message::compress`
 *  passes a lambda that resizes `bufferCompressed_` and returns a pointer
 *  offset by `kHEADER_BYTES_COMPRESSED`, so the codec writes directly into the
 *  final wire buffer leaving the header region intact. Any exception thrown by
 *  the codec is caught and suppressed; the overlay send path cannot propagate
 *  exceptions.
 *
 *  @tparam BufferFactory Callable with signature `void*(std::size_t capacity)`.
 *      Called once with the worst-case compressed size; must return a pointer
 *      to a buffer of at least `capacity` bytes.
 *  @param in Contiguous buffer holding the serialized protobuf payload (i.e.,
 *      the uncompressed message body, not including any header bytes).
 *  @param inSize Number of bytes at `in` to compress.
 *  @param bf Lazy allocator for the compressed output buffer; see above.
 *  @param algorithm Algorithm to use; defaults to `LZ4`.
 *  @return Number of compressed bytes written via `bf`, or `0` on any failure
 *      (including codec error or unrecognised algorithm).
 */
template <class BufferFactory>
std::size_t
compress(
    void const* in,
    std::size_t inSize,
    BufferFactory&& bf,
    Algorithm algorithm = Algorithm::LZ4)
{
    try
    {
        if (algorithm == Algorithm::LZ4)
        {
            return xrpl::compression_algorithms::lz4Compress(
                in, inSize, std::forward<BufferFactory>(bf));
        }

        // LCOV_EXCL_START
        JLOG(debugLog().warn()) << "compress: invalid compression algorithm"
                                << static_cast<int>(algorithm);
        UNREACHABLE(
            "xrpl::compression::compress : invalid compression "
            "algorithm");
        // LCOV_EXCL_STOP
    }
    catch (...)  // NOLINT(bugprone-empty-catch)
    {
    }
    return 0;
}
}  // namespace xrpl::compression
