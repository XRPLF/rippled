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
    */
    /** @{ */
    template <class ProtocolHandler>
    boost::system::error_code
    write (boost::asio::const_buffer buffer, ProtocolHandler& handler);

    template <class ConstBufferSequence, class ProtocolHandler>
    boost::system::error_code
    write (ConstBufferSequence const& buffers, ProtocolHandler& handler);
    /** @} */

private:
    static
    boost::system::error_code
    parse_error()
    {
        return boost::system::errc::make_error_code (
            boost::system::errc::invalid_argument);
    }

    template <class Message, class ProtocolHandler>
    boost::system::error_code
    invoke (ProtocolHandler& handler);
};

template <class ProtocolHandler>
boost::system::error_code
ProtocolStream::write (boost::asio::const_buffer buffer,
    ProtocolHandler& handler)
{
    using namespace boost::asio;
    boost::system::error_code ec;
    std::size_t remain (buffer_size(buffer));
    while (remain)
    {
        if (header_bytes_ < header_.size())
        {
            std::size_t const n (buffer_copy (
                mutable_buffer (header_.data() + header_bytes_,
                    header_.size() - header_bytes_), buffer));
            header_bytes_ += n;
            buffer = buffer + n;
            remain = remain - n;
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
            std::size_t const n (buffer_copy (mutable_buffer (body_.data() +
                body_bytes_, body_.size() - body_bytes_), buffer));
            body_bytes_ += n;
            buffer = buffer + n;
            remain = remain - n;
            if (body_bytes_ >= length_)
            {
                assert (body_bytes_ == length_);
                switch (type_)
                {
                case protocol::mtHELLO:           ec = invoke <protocol::TMHello> (handler); break;
                case protocol::mtPING:            ec = invoke <protocol::TMPing> (handler); break;
                case protocol::mtPROOFOFWORK:     ec = invoke <protocol::TMProofWork> (handler); break;
                case protocol::mtCLUSTER:         ec = invoke <protocol::TMCluster> (handler); break;
                case protocol::mtGET_PEERS:       ec = invoke <protocol::TMGetPeers> (handler); break;
                case protocol::mtPEERS:           ec = invoke <protocol::TMPeers> (handler); break;
                case protocol::mtENDPOINTS:       ec = invoke <protocol::TMEndpoints> (handler); break;
                case protocol::mtTRANSACTION:     ec = invoke <protocol::TMTransaction> (handler); break;
                case protocol::mtGET_LEDGER:      ec = invoke <protocol::TMGetLedger> (handler); break;
                case protocol::mtLEDGER_DATA:     ec = invoke <protocol::TMLedgerData> (handler); break;
                case protocol::mtPROPOSE_LEDGER:  ec = invoke <protocol::TMProposeSet> (handler); break;
                case protocol::mtSTATUS_CHANGE:   ec = invoke <protocol::TMStatusChange> (handler); break;
                case protocol::mtHAVE_SET:        ec = invoke <protocol::TMHaveTransactionSet> (handler); break;
                case protocol::mtVALIDATION:      ec = invoke <protocol::TMValidation> (handler); break;
                case protocol::mtGET_OBJECTS:     ec = invoke <protocol::TMGetObjectByHash> (handler); break;
                default:
                    ec = handler.on_message_unknown(type_);
                    break;
                }
                header_bytes_ = 0;
                body_bytes_ = 0;
            }
        }
    }
    return ec;
}

template <class ConstBufferSequence, class ProtocolHandler>
boost::system::error_code
ProtocolStream::write (ConstBufferSequence const& buffers,
    ProtocolHandler& handler)
{
    boost::system::error_code ec;
    for (auto const& buffer : buffers)
    {
        ec = write(buffer, handler);
        if (ec)
            break;
    }
    return ec;
}

template <class Message, class ProtocolHandler>
boost::system::error_code
ProtocolStream::invoke (ProtocolHandler& handler)
{
    boost::system::error_code ec;
    std::shared_ptr <Message> const m (
        std::make_shared <Message>());
    bool const parsed (m->ParseFromArray (body_.data(), length_));
    if (! parsed)
        return parse_error();
    ec = handler.on_message_begin (type_, m);
    if (! ec)
    {
        ec = handler.on_message (m);
        handler.on_message_end (type_, m);
    }
    return ec;
}

} // ripple

#endif
