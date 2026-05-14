/** @file
 *  Implementation of `Message` — the wire-format envelope for overlay peer
 *  messages. Serialization is eager (constructor), compression is lazy
 *  (`std::call_once` on first `getBuffer(Compressed::On)` call). See
 *  `Message.h` for the full public interface contract.
 */
#include <xrpld/overlay/Message.h>

#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/PublicKey.h>

#include <google/protobuf/message.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace xrpl {

Message::Message(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    std::optional<PublicKey> const& validator)
    : category_(static_cast<std::size_t>(TrafficCount::categorize(message, type, false)))
    , validatorKey_(validator)
{
    using namespace xrpl::compression;

    auto const messageBytes = messageSize(message);

    XRPL_ASSERT(messageBytes, "xrpl::Message::Message : non-empty message input");

    buffer_.resize(kHEADER_BYTES + messageBytes);

    setHeader(buffer_.data(), messageBytes, type, Algorithm::None, 0);

    if (messageBytes != 0)
        message.SerializeToArray(buffer_.data() + kHEADER_BYTES, messageBytes);

    XRPL_ASSERT(
        getBufferSize() == totalSize(message),
        "xrpl::Message::Message : message size matches the buffer");
}

/** Return the serialized byte length of a protobuf message.
 *
 *  Dispatches to `ByteSizeLong()` (returns `size_t`, available from protobuf
 *  3.11.0) or the older `ByteSize()` (returns `int`, risks signed overflow on
 *  payloads larger than 2 GiB) depending on the detected library version.
 *  The compile-time guard avoids undefined signed-overflow behavior on
 *  unusually large messages when building against an older protobuf.
 *
 *  @param message Protobuf message to measure.
 *  @return Serialized size in bytes, excluding any wire header.
 */
std::size_t
Message::messageSize(::google::protobuf::Message const& message)
{
#if defined(GOOGLE_PROTOBUF_VERSION) && (GOOGLE_PROTOBUF_VERSION >= 3011000)
    return message.ByteSizeLong();
#else
    return message.ByteSize();
#endif
}

// static
std::size_t
Message::totalSize(::google::protobuf::Message const& message)
{
    return messageSize(message) + compression::kHEADER_BYTES;
}

/** Attempt LZ4 compression of `buffer_` and populate `bufferCompressed_`.
 *
 *  Applies a two-level eligibility policy before invoking the codec:
 *  1. Payload must exceed 70 bytes — smaller messages cannot recover the
 *     4-byte extra header overhead that the compressed wire format adds.
 *  2. The message type must be in the compression whitelist (bulk data:
 *     `mtTRANSACTION`, `mtLEDGER_DATA`, `mtVALIDATOR_LIST`, etc.).
 *     Latency-sensitive control messages (`mtPING`, `mtVALIDATION`,
 *     `mtPROPOSE_LEDGER`, `mtSTATUS_CHANGE`, `mtHAVE_SET`) are excluded.
 *
 *  If the compressed result is not strictly smaller than
 *  `messageBytes - (kHEADER_BYTES_COMPRESSED - kHEADER_BYTES)` —
 *  i.e., it does not recover the 4-byte header overhead — then
 *  `bufferCompressed_` is cleared to zero length so that `getBuffer()`
 *  falls back to returning the uncompressed `buffer_`.
 *
 *  @note Invoked at most once per instance via `std::call_once` in
 *      `getBuffer()`. Not intended to be called directly.
 */
void
Message::compress()
{
    using namespace xrpl::compression;
    auto const messageBytes = buffer_.size() - kHEADER_BYTES;

    auto type = getType(buffer_.data());

    bool const compressible = [&] {
        if (messageBytes <= 70)
            return false;

        // NOLINTNEXTLINE(bugprone-switch-missing-default-case)
        switch (type)
        {
            case protocol::mtMANIFESTS:
            case protocol::mtENDPOINTS:
            case protocol::mtTRANSACTION:
            case protocol::mtGET_LEDGER:
            case protocol::mtLEDGER_DATA:
            case protocol::mtGET_OBJECTS:
            case protocol::mtVALIDATOR_LIST:
            case protocol::mtVALIDATOR_LIST_COLLECTION:
            case protocol::mtREPLAY_DELTA_RESPONSE:
            case protocol::mtTRANSACTIONS:
                return true;
            case protocol::mtPING:
            case protocol::mtCLUSTER:
            case protocol::mtPROPOSE_LEDGER:
            case protocol::mtSTATUS_CHANGE:
            case protocol::mtHAVE_SET:
            case protocol::mtVALIDATION:
            case protocol::mtPROOF_PATH_REQ:
            case protocol::mtPROOF_PATH_RESPONSE:
            case protocol::mtREPLAY_DELTA_REQ:
            case protocol::mtHAVE_TRANSACTIONS:
                break;
        }
        return false;
    }();

    if (compressible)
    {
        auto payload = static_cast<void const*>(buffer_.data() + kHEADER_BYTES);

        auto compressedSize = xrpl::compression::compress(
            payload,
            messageBytes,
            [&](std::size_t inSize) {  // size of required compressed buffer
                bufferCompressed_.resize(inSize + kHEADER_BYTES_COMPRESSED);
                return (bufferCompressed_.data() + kHEADER_BYTES_COMPRESSED);
            });

        if (compressedSize < (messageBytes - (kHEADER_BYTES_COMPRESSED - kHEADER_BYTES)))
        {
            bufferCompressed_.resize(kHEADER_BYTES_COMPRESSED + compressedSize);
            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            setHeader(bufferCompressed_.data(), compressedSize, type, Algorithm::LZ4, messageBytes);
        }
        else
        {
            bufferCompressed_.resize(0);
        }
    }
}

/** Set payload header

    The header is a variable-sized structure that contains information about
    the type of the message and the length and encoding of the payload.

    The first bit determines whether a message is compressed or uncompressed;
    for compressed messages, the next three bits identify the compression
    algorithm.

    All multi-byte values are represented in big endian.

    For uncompressed messages (6 bytes), numbering bits from left to right:

        - The first 6 bits are set to 0.
        - The next 26 bits represent the payload size.
        - The remaining 16 bits represent the message type.

    For compressed messages (10 bytes), numbering bits from left to right:

        - The first 32 bits, together, represent the compression algorithm
          and payload size:
            - The first bit is set to 1 to indicate the message is compressed.
            - The next 3 bits indicate the compression algorithm.
            - The next 2 bits are reserved at this time and set to 0.
            - The remaining 26 bits represent the payload size.
        - The next 16 bits represent the message type.
        - The remaining 32 bits are the uncompressed message size.

    The maximum size of a message at this time is 64 MB. Messages larger than
    this will be dropped and the recipient may, at its option, sever the link.

    @note While nominally a part of the wire protocol, the framing is subject
          to change; future versions of the code may negotiate the use of
          substantially different framing.
*/
void
Message::setHeader(
    std::uint8_t* in,
    std::uint32_t payloadBytes,
    int type,
    Algorithm compression,
    std::uint32_t uncompressedBytes)
{
    auto h = in;

    auto pack = [](std::uint8_t*& in, std::uint32_t size) {
        *in++ = static_cast<std::uint8_t>((size >> 24) & 0x0F);  // leftmost 4 are compression bits
        *in++ = static_cast<std::uint8_t>((size >> 16) & 0xFF);
        *in++ = static_cast<std::uint8_t>((size >> 8) & 0xFF);
        *in++ = static_cast<std::uint8_t>(size & 0xFF);
    };

    pack(in, payloadBytes);

    *in++ = static_cast<std::uint8_t>((type >> 8) & 0xFF);
    *in++ = static_cast<std::uint8_t>(type & 0xFF);

    if (compression != Algorithm::None)
    {
        pack(in, uncompressedBytes);
        *h |= static_cast<std::uint8_t>(compression);
    }
}

std::size_t
Message::getBufferSize()
{
    return buffer_.size();
}

/** Return the wire buffer, triggering compression on first eligible call.
 *
 *  When `tryCompressed == Compressed::On`, `compress()` is invoked via
 *  `std::call_once` so the LZ4 pass runs exactly once regardless of
 *  concurrent callers. If compression was skipped or produced no net gain,
 *  `bufferCompressed_` is empty and `buffer_` (uncompressed) is returned.
 *
 *  @param tryCompressed `Compressed::On` to request the compressed buffer;
 *      `Compressed::Off` to bypass compression entirely.
 *  @return `const` reference to an internal buffer valid for the lifetime
 *      of this `Message`.
 */
std::vector<uint8_t> const&
Message::getBuffer(Compressed tryCompressed)
{
    if (tryCompressed == Compressed::Off)
        return buffer_;

    std::call_once(once_flag_, &Message::compress, this);

    if (!bufferCompressed_.empty())
    {
        return bufferCompressed_;
    }

    return buffer_;
}

/** Decode the 16-bit message type from a wire-format header.
 *
 *  Bytes 4 and 5 (0-indexed) hold the message type in big-endian order for
 *  both the 6-byte uncompressed and 10-byte compressed header formats, so
 *  this function works identically for either variant.
 *
 *  @param in Pointer to the first byte of a complete wire header.
 *  @return Integer message type (e.g. `protocol::mtTRANSACTION`).
 */
int
Message::getType(std::uint8_t const* in)
{
    int const type = (static_cast<int>(*(in + 4)) << 8) + *(in + 5);
    return type;
}

}  // namespace xrpl
