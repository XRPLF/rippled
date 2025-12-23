#ifndef XRPL_COMPRESSION_H_INCLUDED
#define XRPL_COMPRESSION_H_INCLUDED

#include <xrpl/basics/CompressionAlgorithms.h>
#include <xrpl/basics/Log.h>

namespace ripple {

namespace compression {

std::size_t constexpr headerBytes = 6;
std::size_t constexpr headerBytesCompressed = 10;

// All values other than 'none' must have the high bit. The low order four bits
// must be 0.
enum class Algorithm : std::uint8_t { None = 0x00, LZ4 = 0x90 };

enum class Compressed : std::uint8_t { On, Off };

/** Decompress input stream.
 * @tparam InputStream ZeroCopyInputStream
 * @param in Input source stream
 * @param inSize Size of compressed data
 * @param decompressed Buffer to hold decompressed message
 * @param algorithm Compression algorithm type
 * @return Size of decompressed data or zero if failed to decompress
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
            return ripple::compression_algorithms::lz4Decompress(
                in, inSize, decompressed, decompressedSize);
        else
        {
            // LCOV_EXCL_START
            JLOG(debugLog().warn())
                << "decompress: invalid compression algorithm "
                << static_cast<int>(algorithm);
            UNREACHABLE(
                "ripple::compression::decompress : invalid compression "
                "algorithm");
            // LCOV_EXCL_STOP
        }
    }
    catch (...)
    {
    }
    return 0;
}

/** Compress input data.
 * @tparam BufferFactory Callable object or lambda.
 *     Takes the requested buffer size and returns allocated buffer pointer.
 * @param in Data to compress
 * @param inSize Size of the data
 * @param bf Compressed buffer allocator
 * @param algorithm Compression algorithm type
 * @return Size of compressed data, or zero if failed to compress
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
            return ripple::compression_algorithms::lz4Compress(
                in, inSize, std::forward<BufferFactory>(bf));
        else
        {
            // LCOV_EXCL_START
            JLOG(debugLog().warn()) << "compress: invalid compression algorithm"
                                    << static_cast<int>(algorithm);
            UNREACHABLE(
                "ripple::compression::compress : invalid compression "
                "algorithm");
            // LCOV_EXCL_STOP
        }
    }
    catch (...)
    {
    }
    return 0;
}
}  // namespace compression

}  // namespace ripple

#endif  // XRPL_COMPRESSION_H_INCLUDED
