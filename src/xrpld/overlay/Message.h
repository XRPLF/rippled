/** @file
 *  Wire-protocol framing envelope for overlay peer messages.
 *
 *  Defines `Message`, which serializes a protobuf object once, optionally
 *  compresses it once, and lets every concurrent peer send share the same
 *  read-only buffer. Also defines `kMAXIMUM_MESSAGE_SIZE`, the hard 64 MiB
 *  cap enforced by the 26-bit payload length field in both wire formats.
 */

#pragma once

#include <xrpld/overlay/Compression.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/messages.h>

#include <algorithm>
#include <cstdint>

namespace xrpl {

/** Hard upper bound on the serialized payload size for any overlay message.
 *
 *  The wire-format header encodes payload length in 26 bits, which physically
 *  cannot represent a value larger than 64 MiB. The receiving path drops any
 *  message that exceeds this limit and may disconnect the sender.
 */
constexpr std::size_t kMAXIMUM_MESSAGE_SIZE = megabytes(64);

// VFALCO NOTE If we forward declare Message and write out shared_ptr
//             instead of using the in-class type alias, we can remove the
//             entire ripple.pb.h from the main headers.
//

/** Serialization envelope for a single overlay protocol message.
 *
 *  Encapsulates the serialized bytes of one protobuf message together with
 *  a 6-byte (uncompressed) or 10-byte (compressed) wire header. The object
 *  is designed to be held in a `shared_ptr` and placed into every peer's
 *  outbound send queue: the protobuf is serialized exactly once at
 *  construction, and LZ4 compression — if eligible — is performed at most
 *  once via `std::call_once`, regardless of how many peers share the pointer.
 *
 *  Traffic accounting (`TrafficCount::categorize`) is also done once at
 *  construction; the result is readable via `getCategory()` without
 *  re-inspecting the protobuf type on each send.
 *
 *  The optional `validatorKey_` field is present for `mtVALIDATION` and
 *  `mtPROPOSE_LEDGER` messages so that `PeerImp::send()` can check the
 *  per-peer squelch state before transmitting.
 *
 *  @note Thread-safe for concurrent `getBuffer()` calls after construction.
 *      `compress()` is guarded by `once_flag_`; `buffer_` is immutable after
 *      the constructor returns.
 */
class Message : public std::enable_shared_from_this<Message>
{
    using Compressed = compression::Compressed;
    using Algorithm = compression::Algorithm;

public:
    /** Serialize a protobuf message into a wire-ready buffer.
     *
     *  The protobuf object is serialized immediately into `buffer_` with a
     *  6-byte uncompressed header prepended. Traffic category is computed
     *  once via `TrafficCount::categorize` and stored in `category_`.
     *  Compression is deferred until the first call to `getBuffer(Compressed::On)`.
     *
     *  @param message  Protobuf message to serialize; must be non-empty.
     *  @param type     Wire protocol message type tag (e.g. `mtTRANSACTION`).
     *  @param validator For `mtVALIDATION` and `mtPROPOSE_LEDGER` messages,
     *      the public key of the originating validator. `PeerImp::send()` uses
     *      this to gate squelch-suppressed sends. Pass the default `{}` for
     *      all other message types.
     */
    Message(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::optional<PublicKey> const& validator = {});

    /** Return the byte length of the uncompressed wire buffer (header + payload). */
    std::size_t
    getBufferSize();

    /** Return the serialized size of the protobuf payload, excluding the header.
     *
     *  Uses `ByteSizeLong()` on protobuf >= 3.11, `ByteSize()` otherwise.
     *
     *  @param message Protobuf message to measure.
     *  @return Number of bytes the message occupies when serialized.
     */
    static std::size_t
    messageSize(::google::protobuf::Message const& message);

    /** Return the total wire size of the message: header plus serialized payload.
     *
     *  Equivalent to `messageSize(message) + compression::kHEADER_BYTES`.
     *  Useful for pre-flight size checks before constructing a `Message`.
     *
     *  @param message Protobuf message to measure.
     *  @return Total uncompressed wire size in bytes.
     */
    static std::size_t
    totalSize(::google::protobuf::Message const& message);

    /** Return the wire buffer, optionally in compressed form.
     *
     *  When `tryCompressed == Compressed::On`, compression is attempted via
     *  `std::call_once` so the LZ4 pass runs at most once regardless of
     *  concurrent callers. If the message is ineligible for compression (type
     *  not whitelisted, payload ≤ 70 bytes, or compressed result offers no net
     *  saving over the 4-byte header overhead), the uncompressed `buffer_` is
     *  returned as a fallback.
     *
     *  @param tryCompressed `Compressed::On` to request the compressed buffer;
     *      `Compressed::Off` to always receive the uncompressed buffer.
     *  @return `const` reference to an internal buffer valid for the lifetime
     *      of this `Message`. Callers must keep the `shared_ptr<Message>` alive
     *      for any async I/O that reads from this reference.
     */
    std::vector<uint8_t> const&
    getBuffer(Compressed tryCompressed);

    /** Return the `TrafficCount` category index assigned at construction.
     *
     *  The category is computed once by `TrafficCount::categorize()` and used
     *  by `PeerImp::send()` to record outbound traffic metrics per message
     *  type without re-inspecting the protobuf payload.
     *
     *  @return Index into `TrafficCount::category_t` for this message.
     */
    std::size_t
    getCategory() const
    {
        return category_;
    }

    /** Return the validator public key embedded in this message, if any.
     *
     *  Non-empty only for `mtVALIDATION` and `mtPROPOSE_LEDGER` messages
     *  constructed with an explicit `validator` argument. `PeerImp::send()`
     *  calls `squelch_.expireSquelch(*key)` against this value to decide
     *  whether to transmit or suppress the message for a given peer.
     *
     *  @return Optional public key of the originating validator.
     */
    std::optional<PublicKey> const&
    getValidatorKey() const
    {
        return validatorKey_;
    }

private:
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> bufferCompressed_;
    std::size_t category_;
    std::once_flag once_flag_;
    std::optional<PublicKey> validatorKey_;

    /** Write the wire-format header into an already-allocated buffer region.
     *
     *  Encodes a 6-byte header for uncompressed messages or a 10-byte header
     *  for compressed messages. All multi-byte fields are big-endian.
     *
     *  Uncompressed layout (6 bytes): 6 reserved zero bits | 26-bit payload
     *  size | 16-bit message type.
     *
     *  Compressed layout (10 bytes): 1 compression flag | 3-bit algorithm |
     *  2 reserved bits | 26-bit compressed payload size | 16-bit message type
     *  | 32-bit original uncompressed size.
     *
     *  @param in               Pointer to the start of the header region; must
     *      have at least `kHEADER_BYTES` (uncompressed) or
     *      `kHEADER_BYTES_COMPRESSED` (compressed) bytes available.
     *  @param payloadBytes     Size of the payload that follows the header,
     *      not counting the header itself.
     *  @param type             Protocol message type (16-bit value).
     *  @param compression      Algorithm to encode in the header; `None`
     *      produces a 6-byte uncompressed header.
     *  @param uncompressedBytes Original payload size before compression;
     *      written into the trailing 4 bytes of the compressed header.
     *      Ignored when `compression == Algorithm::None`.
     */
    static void
    setHeader(
        std::uint8_t* in,
        std::uint32_t payloadBytes,
        int type,
        Algorithm compression,
        std::uint32_t uncompressedBytes);

    /** Attempt LZ4 compression and populate `bufferCompressed_`.
     *
     *  Invoked at most once per `Message` instance via `std::call_once` in
     *  `getBuffer()`. Applies a two-stage eligibility policy before invoking
     *  the codec: the payload must exceed 70 bytes and the message type must
     *  appear in the compression whitelist. If the compressed result does not
     *  beat the uncompressed size by more than the 4-byte extra header
     *  overhead, `bufferCompressed_` is cleared so `getBuffer()` falls back
     *  to `buffer_`.
     *
     *  @note Safe to call concurrently — `std::call_once` serializes the
     *      single execution; subsequent calls are no-ops.
     */
    void
    compress();

    /** Decode the message type field from a wire-format header.
     *
     *  Reads bytes 4 and 5 (0-indexed) of the header, which hold the 16-bit
     *  message type in big-endian order for both the 6-byte and 10-byte
     *  header formats.
     *
     *  @param in Pointer to the first byte of the header.
     *  @return Integer message type (e.g. `protocol::mtTRANSACTION`).
     */
    static int
    getType(std::uint8_t const* in);
};

}  // namespace xrpl
