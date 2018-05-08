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
    case protocol::mtHELLO:             return "hello";
    case protocol::mtMANIFESTS:         return "manifests";
    case protocol::mtPING:              return "ping";
    case protocol::mtPROOFOFWORK:       return "proof_of_work";
    case protocol::mtCLUSTER:           return "cluster";
    case protocol::mtGET_PEERS:         return "get_peers";
    case protocol::mtPEERS:             return "peers";
    case protocol::mtENDPOINTS:         return "endpoints";
    case protocol::mtTRANSACTION:       return "tx";
    case protocol::mtGET_LEDGER:        return "get_ledger";
    case protocol::mtLEDGER_DATA:       return "ledger_data";
    case protocol::mtPROPOSE_LEDGER:    return "propose";
    case protocol::mtSTATUS_CHANGE:     return "status";
    case protocol::mtHAVE_SET:          return "have_set";
    case protocol::mtVALIDATION:        return "validation";
    case protocol::mtGET_OBJECTS:       return "get_objects";
    default:
        break;
    };
    return "unknown";
}

namespace detail {

template <class T, class Buffers, class Handler>
std::enable_if_t<std::is_base_of<
    ::google::protobuf::Message, T>::value,
        boost::system::error_code>
invoke (int type, Buffers const& buffers,
    Handler& handler)
{
    ZeroCopyInputStream<Buffers> stream(buffers);
    stream.Skip(Message::kHeaderBytes);
    auto const m (std::make_shared<T>());
    if (! m->ParseFromZeroCopyStream(&stream))
        return boost::system::errc::make_error_code(
            boost::system::errc::invalid_argument);
    auto ec = handler.onMessageBegin (type, m,
       Message::kHeaderBytes + Message::size (buffers));
    if (! ec)
    {
        handler.onMessage (m);
        handler.onMessageEnd (type, m);
    }
    return ec;
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
    boost::system::error_code& ec = result.second;

    auto const type = Message::type(buffers);
    if (type == 0)
        return result;
    auto const size = Message::kHeaderBytes + Message::size(buffers);
    if (boost::asio::buffer_size(buffers) < size)
        return result;

    switch (type)
    {
    case protocol::mtHELLO:         ec = detail::invoke<protocol::TMHello> (type, buffers, handler); break;
    case protocol::mtMANIFESTS:     ec = detail::invoke<protocol::TMManifests> (type, buffers, handler); break;
    case protocol::mtPING:          ec = detail::invoke<protocol::TMPing> (type, buffers, handler); break;
    case protocol::mtCLUSTER:       ec = detail::invoke<protocol::TMCluster> (type, buffers, handler); break;
    case protocol::mtGET_PEERS:     ec = detail::invoke<protocol::TMGetPeers> (type, buffers, handler); break;
    case protocol::mtPEERS:         ec = detail::invoke<protocol::TMPeers> (type, buffers, handler); break;
    case protocol::mtENDPOINTS:     ec = detail::invoke<protocol::TMEndpoints> (type, buffers, handler); break;
    case protocol::mtTRANSACTION:   ec = detail::invoke<protocol::TMTransaction> (type, buffers, handler); break;
    case protocol::mtGET_LEDGER:    ec = detail::invoke<protocol::TMGetLedger> (type, buffers, handler); break;
    case protocol::mtLEDGER_DATA:   ec = detail::invoke<protocol::TMLedgerData> (type, buffers, handler); break;
    case protocol::mtPROPOSE_LEDGER:ec = detail::invoke<protocol::TMProposeSet> (type, buffers, handler); break;
    case protocol::mtSTATUS_CHANGE: ec = detail::invoke<protocol::TMStatusChange> (type, buffers, handler); break;
    case protocol::mtHAVE_SET:      ec = detail::invoke<protocol::TMHaveTransactionSet> (type, buffers, handler); break;
    case protocol::mtVALIDATION:    ec = detail::invoke<protocol::TMValidation> (type, buffers, handler); break;
    case protocol::mtGET_OBJECTS:   ec = detail::invoke<protocol::TMGetObjectByHash> (type, buffers, handler); break;
    default:
        ec = handler.onMessageUnknown (type);
        break;
    }
    if (! ec)
        result.first = size;

    return result;
}

/** Write a protocol message to a streambuf. */
template <class Streambuf>
void
write (Streambuf& streambuf,
    ::google::protobuf::Message const& m, int type,
        std::size_t blockBytes)
{
    auto const size = m.ByteSize();
    std::array<std::uint8_t, 6> v;
    v[0] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
    v[1] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
    v[2] = static_cast<std::uint8_t>((size >>  8) & 0xFF);
    v[3] = static_cast<std::uint8_t>( size        & 0xFF);
    v[4] = static_cast<std::uint8_t>((type >>  8) & 0xFF);
    v[5] = static_cast<std::uint8_t>( type        & 0xFF);
    streambuf.commit(boost::asio::buffer_copy(
        streambuf.prepare(Message::kHeaderBytes),
            boost::asio::buffer(v)));
    ZeroCopyOutputStream<Streambuf> stream (
        streambuf, blockBytes);
    m.SerializeToZeroCopyStream(&stream);
}

} // ripple

#endif
