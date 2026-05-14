/** @file
 *  Wire-protocol message decoding for the XRPL overlay P2P network.
 *
 *  Owns the entire pipeline from raw Boost.Asio buffer sequences (as received
 *  from a peer TCP connection) to typed protobuf message objects dispatched
 *  to a handler. No other layer touches on-wire bytes for protocol messages.
 *
 *  The three public entry points for callers are:
 *  - `invokeProtocolMessage()` — the hot-path read-loop entry point used by `PeerImp`.
 *  - `protocolMessageName()` — integer type code to human-readable string (logging).
 *  - `protocolMessageType()` — message object to type constant (outbound header construction).
 */
#pragma once

#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/ZeroCopyStream.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/messages.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>

#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace xrpl {

/** Returns the wire message-type constant for a `TMGetLedger` message object.
 *
 *  Used by outbound code that needs to construct a wire header from an
 *  existing protobuf message object rather than a bare numeric constant.
 *
 *  @param Unused `TMGetLedger` instance (overload-resolution tag).
 *  @return `protocol::mtGET_LEDGER`
 */
inline protocol::MessageType
protocolMessageType(protocol::TMGetLedger const&)
{
    return protocol::mtGET_LEDGER;
}

/** Returns the wire message-type constant for a `TMReplayDeltaRequest` message object.
 *
 *  @param Unused `TMReplayDeltaRequest` instance (overload-resolution tag).
 *  @return `protocol::mtREPLAY_DELTA_REQ`
 */
inline protocol::MessageType
protocolMessageType(protocol::TMReplayDeltaRequest const&)
{
    return protocol::mtREPLAY_DELTA_REQ;
}

/** Returns the wire message-type constant for a `TMProofPathRequest` message object.
 *
 *  @param Unused `TMProofPathRequest` instance (overload-resolution tag).
 *  @return `protocol::mtPROOF_PATH_REQ`
 */
inline protocol::MessageType
protocolMessageType(protocol::TMProofPathRequest const&)
{
    return protocol::mtPROOF_PATH_REQ;
}

/** Maps a numeric protocol message type to a human-readable name for logging.
 *
 *  Covers all known `protocol::MessageType` values. Returns `"unknown"` for
 *  any type not present in the switch, enabling forward-compatible logging
 *  when new message types are added to the protocol.
 *
 *  @param type Integer message-type constant (e.g. `protocol::mtPING`).
 *  @return Lowercase string name, or `"unknown"` if the type is unrecognised.
 */
template <class = void>
std::string
protocolMessageName(int type)
{
    switch (type)
    {
        case protocol::mtMANIFESTS:
            return "manifests";
        case protocol::mtPING:
            return "ping";
        case protocol::mtCLUSTER:
            return "cluster";
        case protocol::mtENDPOINTS:
            return "endpoints";
        case protocol::mtTRANSACTION:
            return "tx";
        case protocol::mtGET_LEDGER:
            return "get_ledger";
        case protocol::mtLEDGER_DATA:
            return "ledger_data";
        case protocol::mtPROPOSE_LEDGER:
            return "propose";
        case protocol::mtSTATUS_CHANGE:
            return "status";
        case protocol::mtHAVE_SET:
            return "have_set";
        case protocol::mtVALIDATOR_LIST:
            return "validator_list";
        case protocol::mtVALIDATOR_LIST_COLLECTION:
            return "validator_list_collection";
        case protocol::mtVALIDATION:
            return "validation";
        case protocol::mtGET_OBJECTS:
            return "get_objects";
        case protocol::mtHAVE_TRANSACTIONS:
            return "have_transactions";
        case protocol::mtTRANSACTIONS:
            return "transactions";
        case protocol::mtSQUELCH:
            return "squelch";
        case protocol::mtPROOF_PATH_REQ:
            return "proof_path_request";
        case protocol::mtPROOF_PATH_RESPONSE:
            return "proof_path_response";
        case protocol::mtREPLAY_DELTA_REQ:
            return "replay_delta_request";
        case protocol::mtREPLAY_DELTA_RESPONSE:
            return "replay_delta_response";
        default:
            break;
    }
    return "unknown";
}

namespace detail {

/** Decoded fields from an XRPL overlay wire-format message header.
 *
 *  Populated by `parseMessageHeader()` and consumed by `parseMessageContent()`
 *  and `invoke()`. Keeping the fields pre-computed avoids re-parsing the header
 *  bytes during the content-decode phase.
 *
 *  Two header layouts are supported:
 *  - **Uncompressed** (6 bytes): top 6 bits of byte 0 are zero; 26-bit payload
 *    size follows, then 2-byte message type.
 *  - **Compressed** (10 bytes): high bit of byte 0 is set; top nibble encodes
 *    the algorithm (bits 2–3 must be zero); 26-bit wire payload size; 2-byte
 *    message type; 4-byte uncompressed size.
 */
struct MessageHeader
{
    /** Total bytes on the wire for this message (header + payload). */
    std::uint32_t total_wire_size = 0;

    /** Size of the header in bytes: 6 for uncompressed, 10 for compressed. */
    std::uint32_t header_size = 0;

    /** Size of the payload as it appears on the wire (compressed or raw). */
    std::uint32_t payload_wire_size = 0;

    /** Size of the payload after decompression.
     *
     *  Equal to `payload_wire_size` for uncompressed messages.
     *  Used to pre-allocate the decompression target buffer.
     */
    std::uint32_t uncompressed_size = 0;

    /** Protobuf message type constant (e.g. `protocol::mtPING`). */
    std::uint16_t message_type = 0;

    /** Compression algorithm used for the payload.
     *
     *  `Algorithm::None` indicates an uncompressed message.
     *  Currently only `Algorithm::LZ4` is supported for compressed messages.
     */
    compression::Algorithm algorithm = compression::Algorithm::None;
};

/** Returns a byte iterator pointing to the first byte of a buffer sequence.
 *
 *  Fixes the value type to `uint8_t` so callers avoid verbose template
 *  instantiations throughout `parseMessageHeader`.
 *
 *  @tparam BufferSequence A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @param bufs The buffer sequence to iterate.
 *  @return A `buffers_iterator` positioned at the first byte.
 */
template <typename BufferSequence>
auto
buffersBegin(BufferSequence const& bufs)
{
    return boost::asio::buffers_iterator<BufferSequence, std::uint8_t>::begin(bufs);
}

/** Returns a byte iterator pointing past the last byte of a buffer sequence.
 *
 *  Companion to `buffersBegin()`; fixes the value type to `uint8_t` for
 *  consistent instantiation across `parseMessageHeader`.
 *
 *  @tparam BufferSequence A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @param bufs The buffer sequence to iterate.
 *  @return A `buffers_iterator` positioned one past the last byte.
 */
template <typename BufferSequence>
auto
buffersEnd(BufferSequence const& bufs)
{
    return boost::asio::buffers_iterator<BufferSequence, std::uint8_t>::end(bufs);
}

/** Parse an XRPL overlay message header from a buffer sequence.
 *
 *  Walks the first bytes of `bufs` and populates a `MessageHeader` with all
 *  pre-computed decode fields. Three distinct outcomes are possible:
 *
 *  - **Seated optional, `ec` clear**: header parsed successfully.
 *  - **Null optional, `ec == errc::success`**: not enough bytes yet — caller
 *    should wait for more data and retry.
 *  - **Null optional, `ec == errc::no_message`**: the format-guard bits did not
 *    match either the uncompressed (top 6 bits zero) or compressed (high bit set)
 *    layout — the stream is malformed and the connection should be dropped.
 *  - **Null optional, `ec == errc::protocol_error`**: compressed-format bits 2–3
 *    were set, or the algorithm nibble is not `LZ4` — invalid compression framing.
 *
 *  @tparam BufferSequence A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @param ec Set to an error code when the optional is null (see above); cleared
 *      on success.
 *  @param bufs Non-empty buffer sequence containing the received bytes.
 *  @param size Total number of valid bytes available in `bufs`.
 *  @return A `MessageHeader` on success, or `std::nullopt` with `ec` set.
 *  @note `bufs` must not be empty; this is asserted internally.
 */
template <class BufferSequence>
std::optional<MessageHeader>
parseMessageHeader(boost::system::error_code& ec, BufferSequence const& bufs, std::size_t size)
{
    using namespace xrpl::compression;

    MessageHeader hdr;
    auto iter = buffersBegin(bufs);
    XRPL_ASSERT(iter != buffersEnd(bufs), "xrpl::detail::parseMessageHeader : non-empty buffer");

    if (*iter & 0x80)
    {
        hdr.header_size = kHEADER_BYTES_COMPRESSED;

        if (size < hdr.header_size)
        {
            ec = make_error_code(boost::system::errc::success);
            return std::nullopt;
        }

        if (*iter & 0x0C)
        {
            ec = make_error_code(boost::system::errc::protocol_error);
            return std::nullopt;
        }

        hdr.algorithm = static_cast<compression::Algorithm>(*iter & 0xF0);

        if (hdr.algorithm != compression::Algorithm::LZ4)
        {
            ec = make_error_code(boost::system::errc::protocol_error);
            return std::nullopt;
        }

        for (int i = 0; i != 4; ++i)
            hdr.payload_wire_size = (hdr.payload_wire_size << 8) + *iter++;

        // clear the top four bits (the compression bits).
        hdr.payload_wire_size &= 0x0FFFFFFF;

        hdr.total_wire_size = hdr.header_size + hdr.payload_wire_size;

        for (int i = 0; i != 2; ++i)
            hdr.message_type = (hdr.message_type << 8) + *iter++;

        for (int i = 0; i != 4; ++i)
            hdr.uncompressed_size = (hdr.uncompressed_size << 8) + *iter++;

        return hdr;
    }

    if ((*iter & 0xFC) == 0)
    {
        hdr.header_size = kHEADER_BYTES;

        if (size < hdr.header_size)
        {
            ec = make_error_code(boost::system::errc::success);
            return std::nullopt;
        }

        hdr.algorithm = Algorithm::None;

        for (int i = 0; i != 4; ++i)
            hdr.payload_wire_size = (hdr.payload_wire_size << 8) + *iter++;

        hdr.uncompressed_size = hdr.payload_wire_size;
        hdr.total_wire_size = hdr.header_size + hdr.payload_wire_size;

        for (int i = 0; i != 2; ++i)
            hdr.message_type = (hdr.message_type << 8) + *iter++;

        return hdr;
    }

    ec = make_error_code(boost::system::errc::no_message);
    return std::nullopt;
}

/** Deserialize a protobuf message from the payload portion of a buffer sequence.
 *
 *  Uses a `ZeroCopyInputStream` adapter to skip the header bytes and feed the
 *  remaining bytes directly to protobuf, avoiding an intermediate copy for
 *  uncompressed messages even when the data spans multiple non-contiguous
 *  Asio buffer segments.
 *
 *  For compressed messages (`header.algorithm != None`) a contiguous
 *  `std::vector<uint8_t>` of `header.uncompressed_size` bytes is allocated,
 *  LZ4-decompressed into it, and then parsed via `ParseFromArray`. The extra
 *  allocation is unavoidable because LZ4 requires a contiguous output region.
 *
 *  @tparam T A `google::protobuf::Message` subclass; enforced by `enable_if`.
 *  @tparam Buffers A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @param header Pre-parsed header describing sizes and compression state.
 *  @param buffers Buffer sequence beginning at the start of the wire message.
 *  @return A `shared_ptr<T>` on success, or an empty `shared_ptr` if
 *      decompression or protobuf parsing fails.
 */
template <
    class T,
    class Buffers,
    class = std::enable_if_t<std::is_base_of_v<::google::protobuf::Message, T>>>
std::shared_ptr<T>
parseMessageContent(MessageHeader const& header, Buffers const& buffers)
{
    auto m = std::make_shared<T>();

    ZeroCopyInputStream<Buffers> stream(buffers);
    stream.Skip(header.header_size);

    if (header.algorithm != compression::Algorithm::None)
    {
        std::vector<std::uint8_t> payload;
        payload.resize(header.uncompressed_size);

        auto const payloadSize = xrpl::compression::decompress(
            stream,
            header.payload_wire_size,
            payload.data(),
            header.uncompressed_size,
            header.algorithm);

        if (payloadSize == 0 || !m->ParseFromArray(payload.data(), payloadSize))
            return {};
    }
    else if (!m->ParseFromZeroCopyStream(&stream))
    {
        return {};
    }

    return m;
}

/** Parse and dispatch a single typed protobuf message to a handler.
 *
 *  Calls `parseMessageContent<T>()`, then invokes the three handler hooks in
 *  order: `onMessageBegin()` (wire/uncompressed sizes and compression flag for
 *  traffic accounting), `onMessage()` (typed processing), and `onMessageEnd()`
 *  (post-message hook). Separating begin/end allows the handler to bracket
 *  dispatch with timing or resource-charge logic.
 *
 *  @tparam T A `google::protobuf::Message` subclass; enforced by `enable_if`.
 *  @tparam Buffers A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @tparam Handler Duck-typed handler; concrete implementation is `PeerImp`.
 *  @param header Pre-parsed header with size and compression metadata.
 *  @param buffers Buffer sequence for the full wire message.
 *  @param handler Handler whose `onMessage(shared_ptr<T>)` overload is called.
 *  @return `true` on success; `false` if `parseMessageContent` fails (parse
 *      error or decompression failure), causing `invokeProtocolMessage` to
 *      return `errc::bad_message`.
 */
template <
    class T,
    class Buffers,
    class Handler,
    class = std::enable_if_t<std::is_base_of_v<::google::protobuf::Message, T>>>
bool
invoke(MessageHeader const& header, Buffers const& buffers, Handler& handler)
{
    auto const m = parseMessageContent<T>(header, buffers);
    if (!m)
        return false;

    using namespace xrpl::compression;
    handler.onMessageBegin(
        header.message_type,
        m,
        header.payload_wire_size,
        header.uncompressed_size,
        header.algorithm != Algorithm::None);
    handler.onMessage(m);
    handler.onMessageEnd(header.message_type, m);

    return true;
}

}  // namespace detail

/** Decode and dispatch up to one protocol message from a buffer sequence.
 *
 *  This is the single function called from `PeerImp`'s async read loop. It
 *  runs the full three-stage pipeline: parse header → validate → dispatch.
 *
 *  **Validation gates (in order):**
 *  1. Header parse: returns early with the `error_code` set by
 *     `parseMessageHeader()` if the header is incomplete or malformed.
 *  2. Size limit: both `payload_wire_size` and `uncompressed_size` must not
 *     exceed `kMAXIMUM_MESSAGE_SIZE` (64 MiB); guards against memory exhaustion.
 *  3. Compression check: if `handler.compressionEnabled()` is false and a
 *     compressed header was received, returns `protocol_error` — prevents a
 *     peer from forcing CPU work without negotiation.
 *  4. Incomplete payload: if `total_wire_size > size`, sets `hint` to the
 *     exact byte shortfall and returns zero consumed bytes (not an error).
 *
 *  **Dispatch:** an exhaustive switch on `message_type` calls
 *  `detail::invoke<TM…>()` for each known protobuf type. Unknown types invoke
 *  `handler.onMessageUnknown()` and succeed, enabling forward-compatible
 *  protocol evolution without hard failures.
 *
 *  @tparam Buffers A `ConstBufferSequence` meeting Boost.Asio requirements.
 *  @tparam Handler Duck-typed handler; concrete implementation is `PeerImp`.
 *      Must provide `compressionEnabled()`, `onMessageBegin()`, `onMessage()`
 *      overloads, `onMessageEnd()`, and `onMessageUnknown()`.
 *  @param buffers Buffer sequence containing received bytes.
 *  @param handler Handler that processes each decoded message.
 *  @param hint Output: if the message is incomplete, set to the number of
 *      additional bytes needed, reducing subsequent `async_read` syscall
 *      overhead. Zero means "no hint".
 *  @return `{bytes_consumed, error_code}`. Zero bytes consumed with a clear
 *      error code means the message is incomplete — retry after reading more
 *      data. A non-zero error code means the peer should be disconnected.
 */
template <class Buffers, class Handler>
std::pair<std::size_t, boost::system::error_code>
invokeProtocolMessage(Buffers const& buffers, Handler& handler, std::size_t& hint)
{
    std::pair<std::size_t, boost::system::error_code> result = {0, {}};

    auto const size = boost::asio::buffer_size(buffers);

    if (size == 0)
        return result;

    auto header = detail::parseMessageHeader(result.second, buffers, size);

    if (!header)
        return result;

    if (header->payload_wire_size > kMAXIMUM_MESSAGE_SIZE ||
        header->uncompressed_size > kMAXIMUM_MESSAGE_SIZE)
    {
        result.second = make_error_code(boost::system::errc::message_size);
        return result;
    }

    if (!handler.compressionEnabled() && header->algorithm != compression::Algorithm::None)
    {
        result.second = make_error_code(boost::system::errc::protocol_error);
        return result;
    }

    if (header->total_wire_size > size)
    {
        hint = header->total_wire_size - size;
        return result;
    }

    bool success = false;

    switch (header->message_type)
    {
        case protocol::mtMANIFESTS:
            success = detail::invoke<protocol::TMManifests>(*header, buffers, handler);
            break;
        case protocol::mtPING:
            success = detail::invoke<protocol::TMPing>(*header, buffers, handler);
            break;
        case protocol::mtCLUSTER:
            success = detail::invoke<protocol::TMCluster>(*header, buffers, handler);
            break;
        case protocol::mtENDPOINTS:
            success = detail::invoke<protocol::TMEndpoints>(*header, buffers, handler);
            break;
        case protocol::mtTRANSACTION:
            success = detail::invoke<protocol::TMTransaction>(*header, buffers, handler);
            break;
        case protocol::mtGET_LEDGER:
            success = detail::invoke<protocol::TMGetLedger>(*header, buffers, handler);
            break;
        case protocol::mtLEDGER_DATA:
            success = detail::invoke<protocol::TMLedgerData>(*header, buffers, handler);
            break;
        case protocol::mtPROPOSE_LEDGER:
            success = detail::invoke<protocol::TMProposeSet>(*header, buffers, handler);
            break;
        case protocol::mtSTATUS_CHANGE:
            success = detail::invoke<protocol::TMStatusChange>(*header, buffers, handler);
            break;
        case protocol::mtHAVE_SET:
            success = detail::invoke<protocol::TMHaveTransactionSet>(*header, buffers, handler);
            break;
        case protocol::mtVALIDATION:
            success = detail::invoke<protocol::TMValidation>(*header, buffers, handler);
            break;
        case protocol::mtVALIDATOR_LIST:
            success = detail::invoke<protocol::TMValidatorList>(*header, buffers, handler);
            break;
        case protocol::mtVALIDATOR_LIST_COLLECTION:
            success =
                detail::invoke<protocol::TMValidatorListCollection>(*header, buffers, handler);
            break;
        case protocol::mtGET_OBJECTS:
            success = detail::invoke<protocol::TMGetObjectByHash>(*header, buffers, handler);
            break;
        case protocol::mtHAVE_TRANSACTIONS:
            success = detail::invoke<protocol::TMHaveTransactions>(*header, buffers, handler);
            break;
        case protocol::mtTRANSACTIONS:
            success = detail::invoke<protocol::TMTransactions>(*header, buffers, handler);
            break;
        case protocol::mtSQUELCH:
            success = detail::invoke<protocol::TMSquelch>(*header, buffers, handler);
            break;
        case protocol::mtPROOF_PATH_REQ:
            success = detail::invoke<protocol::TMProofPathRequest>(*header, buffers, handler);
            break;
        case protocol::mtPROOF_PATH_RESPONSE:
            success = detail::invoke<protocol::TMProofPathResponse>(*header, buffers, handler);
            break;
        case protocol::mtREPLAY_DELTA_REQ:
            success = detail::invoke<protocol::TMReplayDeltaRequest>(*header, buffers, handler);
            break;
        case protocol::mtREPLAY_DELTA_RESPONSE:
            success = detail::invoke<protocol::TMReplayDeltaResponse>(*header, buffers, handler);
            break;
        default:
            handler.onMessageUnknown(header->message_type);
            success = true;
            break;
    }

    result.first = header->total_wire_size;

    if (!success)
        result.second = make_error_code(boost::system::errc::bad_message);

    return result;
}

}  // namespace xrpl
