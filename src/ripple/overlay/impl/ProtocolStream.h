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

#ifndef RIPPLE_OVERLAY_PROTOCOLSTREAM_H_INCLUDED
#define RIPPLE_OVERLAY_PROTOCOLSTREAM_H_INCLUDED

#include "ripple.pb.h"
#include <ripple/overlay/Message.h>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace ripple {

/** Turns a stream of bytes into protocol messages and invokes the handler. */
class ProtocolStream
{
private:
    using error_code = boost::system::error_code;

    std::size_t header_bytes_ = 0;
    std::size_t body_bytes_ = 0;
    std::uint32_t length_;
    std::uint16_t type_;
    std::vector <std::uint8_t> header_; // VFALCO TODO Use std::array
    std::vector <std::uint8_t> body_;

public:
    ProtocolStream()
    {
        header_.resize (Message::kHeaderBytes);
    }

    /** Write data to the protocol stream.
        This may call the handler for up to one complete message.
        @return The number of bytes consumed and the error code if any.
    */
    template <class ConstBufferSequence, class ProtocolHandler>
    std::pair<std::size_t, error_code>
    write (ConstBufferSequence const& buffers, ProtocolHandler& handler);

private:
    static
    error_code
    parse_error()
    {
        return boost::system::errc::make_error_code (
            boost::system::errc::invalid_argument);
    }

    template <class ProtocolHandler>
    error_code
    write (boost::asio::const_buffer buffer, ProtocolHandler& handler,
        bool& did_one, std::size_t& bytes_consumed);

    template <class Message, class ProtocolHandler>
    error_code
    invoke (ProtocolHandler& handler);
};

template <class ConstBufferSequence, class ProtocolHandler>
auto
ProtocolStream::write (ConstBufferSequence const& buffers,
    ProtocolHandler& handler) ->
        std::pair<std::size_t, error_code>
{
    std::pair<std::size_t, error_code> result;
    result.first = 0;
    for (auto const& buffer : buffers)
    {
        bool did_one;
        std::size_t bytes_consumed;
        result.second = write (buffer, handler,
            did_one, bytes_consumed);
        result.first += bytes_consumed;
        if (did_one || result.second)
            break;
    }
    return result;
}

template <class ProtocolHandler>
auto
ProtocolStream::write (boost::asio::const_buffer buffer,
    ProtocolHandler& handler, bool& did_one,
        std::size_t& bytes_consumed) ->
            error_code
{
    using namespace boost::asio;

    error_code ec;
    did_one = false;
    bytes_consumed = 0;
    std::size_t const size = buffer_size(buffer);
    while (bytes_consumed < size)
    {
        if (header_bytes_ < header_.size())
        {
            std::size_t const n = buffer_copy(
                mutable_buffer (header_.data() + header_bytes_,
                    header_.size() - header_bytes_), buffer);
            header_bytes_ += n;
            buffer = buffer + n;
            bytes_consumed += n;
            if (header_bytes_ >= header_.size())
            {
                assert (header_bytes_ == header_.size());
                length_ = Message::getLength (header_);
                type_ = Message::getType (header_);
                body_.resize (length_);
            }
        }

        if (header_bytes_ >= header_.size())
        {
            std::size_t const n = buffer_copy(
                mutable_buffer(body_.data() + body_bytes_,
                    body_.size() - body_bytes_), buffer);
            body_bytes_ += n;
            buffer = buffer + n;
            bytes_consumed += n;
            if (body_bytes_ >= length_)
            {
                assert (body_bytes_ == length_);
                switch (type_)
                {
                case protocol::mtHELLO:
                    invoke <protocol::TMHello> (handler);
                    break;

                case protocol::mtPING:
                    invoke <protocol::TMPing> (handler);
                    break;

                case protocol::mtPROOFOFWORK:
                    invoke <protocol::TMProofWork> (handler);
                    break;

                case protocol::mtCLUSTER:
                    invoke <protocol::TMCluster> (handler);
                    break;

                case protocol::mtGET_PEERS:
                    invoke <protocol::TMGetPeers> (handler);
                    break;

                case protocol::mtPEERS:
                    invoke <protocol::TMPeers> (handler);
                    break;

                case protocol::mtENDPOINTS:
                    invoke <protocol::TMEndpoints> (handler);
                    break;

                case protocol::mtTRANSACTION:
                    invoke <protocol::TMTransaction> (handler);
                    break;

                case protocol::mtGET_LEDGER:
                    invoke <protocol::TMGetLedger> (handler);
                    break;

                case protocol::mtLEDGER_DATA:
                    invoke <protocol::TMLedgerData> (handler);
                    break;

                case protocol::mtPROPOSE_LEDGER:
                    invoke <protocol::TMProposeSet> (handler);
                    break;

                case protocol::mtSTATUS_CHANGE:
                    invoke <protocol::TMStatusChange> (handler);
                    break;

                case protocol::mtHAVE_SET:
                    invoke <protocol::TMHaveTransactionSet> (handler);
                    break;

                case protocol::mtVALIDATION:
                    invoke <protocol::TMValidation> (handler);
                    break;

                case protocol::mtGET_OBJECTS:
                    invoke <protocol::TMGetObjectByHash> (handler);
                    break;

                default:
                    ec = handler.onMessageUnknown(type_);
                    break;
                }
                header_bytes_ = 0;
                body_bytes_ = 0;
                did_one = true;
                break;
            }
        }
    }
    return ec;
}

template <class Message, class ProtocolHandler>
auto
ProtocolStream::invoke (ProtocolHandler& handler) ->
    error_code
{
    error_code ec;
    std::shared_ptr <Message> const m (
        std::make_shared <Message>());
    bool const parsed (m->ParseFromArray (body_.data(), length_));
    if (! parsed)
        return parse_error();
    ec = handler.onMessageBegin (type_, m);
    if (! ec)
    {
        handler.onMessage (m);
        handler.onMessageEnd (type_, m);
    }
    return ec;
}

} // ripple

#endif
