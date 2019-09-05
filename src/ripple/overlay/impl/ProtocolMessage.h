//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_PROTOCOLMESSAGE_H_INCLUDED
#define RIPPLE_OVERLAY_PROTOCOLMESSAGE_H_INCLUDED

#include <ripple/basics/ByteUtilities.h>
#include <ripple/protocol/messages.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/impl/ZeroCopyStream.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/system/error_code.hpp>
#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace ripple {

/** Returns the name of a protocol message given its type. */
template <class = void>
std::string
protocolMessageName (int type)
{
    switch (type)
    {
    case protocol::mtMANIFESTS:             return "manifests";
    case protocol::mtPING:                  return "ping";
    case protocol::mtCLUSTER:               return "cluster";
    case protocol::mtGET_SHARD_INFO:        return "get_shard_info";
    case protocol::mtSHARD_INFO:            return "shard_info";
    case protocol::mtGET_PEER_SHARD_INFO:   return "get_peer_shard_info";
    case protocol::mtPEER_SHARD_INFO:       return "peer_shard_info";
    case protocol::mtGET_PEERS:             return "get_peers";
    case protocol::mtPEERS:                 return "peers";
    case protocol::mtENDPOINTS:             return "endpoints";
    case protocol::mtTRANSACTION:           return "tx";
    case protocol::mtGET_LEDGER:            return "get_ledger";
    case protocol::mtLEDGER_DATA:           return "ledger_data";
    case protocol::mtPROPOSE_LEDGER:        return "propose";
    case protocol::mtSTATUS_CHANGE:         return "status";
    case protocol::mtHAVE_SET:              return "have_set";
    case protocol::mtVALIDATION:            return "validation";
    case protocol::mtGET_OBJECTS:           return "get_objects";
    default:
        break;
    }
    return "unknown";
}

namespace detail {

struct MessageHeader
{
    /** The size of the message on the wire.

        @note This is the sum of sizes of the header and the payload.
    */
    std::uint32_t total_wire_size = 0;

    /** The size of the header associated with this message. */
    std::uint32_t header_size = 0;

    /** The size of the payload on the wire. */
    std::uint32_t payload_wire_size = 0;

    /** The type of the message. */
    std::uint16_t message_type = 0;
};

template <class BufferSequence>
boost::optional<MessageHeader> parseMessageHeader(
    BufferSequence const& bufs,
    std::size_t size)
{
    auto iter = boost::asio::buffers_iterator<BufferSequence, std::uint8_t>::begin(bufs);

    MessageHeader hdr;

    // Version 1 header: uncompressed payload.
    // The top six bits of the first byte are 0.
    if ((*iter & 0xFC) == 0)
    {
        hdr.header_size = 6;

        if (size < hdr.header_size)
            return {};

        for (int i = 0; i != 4; ++i)
            hdr.payload_wire_size = (hdr.payload_wire_size << 8) + *iter++;

        hdr.total_wire_size = hdr.header_size + hdr.payload_wire_size;

        for (int i = 0; i != 2; ++i)
            hdr.message_type = (hdr.message_type << 8) + *iter++;

        return hdr;
    }

    return {};
}

template <class T, class Buffers, class Handler,
    class = std::enable_if_t<std::is_base_of<::google::protobuf::Message, T>::value>>
bool
invoke (
    MessageHeader const& header,
    Buffers const& buffers,
    Handler& handler)
{
    auto const m = std::make_shared<T>();

    ZeroCopyInputStream<Buffers> stream(buffers);
    stream.Skip(header.header_size);

    if (! m->ParseFromZeroCopyStream(&stream))
        return false;

    handler.onMessageBegin (header.message_type, m, header.payload_wire_size);
    handler.onMessage (m);
    handler.onMessageEnd (header.message_type, m);

    return true;
}

}

/** Calls the handler for up to one protocol message in the passed buffers.

    If there is insufficient data to produce a complete protocol
    message, zero is returned for the number of bytes consumed.

    @return The number of bytes consumed, or the error code if any.
*/
template <class Buffers, class Handler>
std::pair <std::size_t, boost::system::error_code>
invokeProtocolMessage (Buffers const& buffers, Handler& handler)
{
    std::pair<std::size_t,boost::system::error_code> result = { 0, {} };

    auto const size = boost::asio::buffer_size(buffers);

    if (size == 0)
        return result;

    auto header = detail::parseMessageHeader(buffers, size);

    // If we can't parse the header then it may be that we don't have enough
    // bytes yet, or because the message was cut off.
    if (!header)
        return result;

    // We implement a maximum size for protocol messages. Sending a message
    // whose size exceeds this may result in the connection being dropped. A
    // larger message size may be supported in the future or negotiated as
    // part of a protocol upgrade.
    if (header->payload_wire_size > megabytes(64))
    {
        result.second = make_error_code(boost::system::errc::message_size);
        return result;
    }

    // We don't have the whole message yet. This isn't an error but we have
    // nothing to do.
    if (header->total_wire_size > size)
        return result;

    bool success;

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
    case protocol::mtGET_SHARD_INFO:
        success = detail::invoke<protocol::TMGetShardInfo>(*header, buffers, handler);
        break;
    case protocol::mtSHARD_INFO:
        success = detail::invoke<protocol::TMShardInfo>(*header, buffers, handler);
        break;
    case protocol::mtGET_PEER_SHARD_INFO:
        success = detail::invoke<protocol::TMGetPeerShardInfo>(*header, buffers, handler);
        break;
    case protocol::mtPEER_SHARD_INFO:
        success = detail::invoke<protocol::TMPeerShardInfo>(*header, buffers, handler);
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
    case protocol::mtGET_OBJECTS:
        success = detail::invoke<protocol::TMGetObjectByHash>(*header, buffers, handler);
        break;
    default:
        handler.onMessageUnknown (header->message_type);
        success = true;
        break;
    }

    result.first = header->total_wire_size;

    if (!success)
        result.second = make_error_code(boost::system::errc::bad_message);

    return result;
}

} // ripple

#endif
