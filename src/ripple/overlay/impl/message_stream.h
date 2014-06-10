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

#ifndef RIPPLE_OVERLAY_MESSAGE_STREAM_H_INCLUDED
#define RIPPLE_OVERLAY_MESSAGE_STREAM_H_INCLUDED

#include <ripple/overlay/impl/abstract_protocol_handler.h>
#include <ripple/overlay/Message.h>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace ripple {

/** Turns a stream of bytes into protocol messages and invokes the handler. */
class message_stream
{
private:
    abstract_protocol_handler& handler_;
    std::size_t header_bytes_;
    std::size_t body_bytes_;
    std::uint32_t length_;
    std::uint16_t type_;
    std::vector <std::uint8_t> header_; // VFALCO TODO Use std::array
    std::vector <std::uint8_t> body_;

    static
    boost::system::error_code
    parse_error()
    {
        return boost::system::errc::make_error_code (
            boost::system::errc::invalid_argument);
    }

    template <class Message>
    boost::system::error_code
    invoke()
    {
        boost::system::error_code ec;
        std::shared_ptr <Message> m (std::make_shared <Message>());
        bool const parsed (m->ParseFromArray (body_.data(), length_));
        if (! parsed)
            return parse_error();
        ec = handler_.on_message_begin (type_, m);
        if (! ec)
        {
            ec = handler_.on_message (m);
            handler_.on_message_end (type_, m);
        }
        return ec;
    }

public:
    message_stream (abstract_protocol_handler& handler)
        : handler_(handler)
        , header_bytes_(0)
        , body_bytes_(0)
    {
        header_.resize (Message::kHeaderBytes);
    }

    /** Push a single buffer through.
        The handler is called for each complete protocol message contained
        in the buffer.
    */
    template <class ConstBuffer>
    boost::system::error_code
    write_one (ConstBuffer const& cb)
    {
        using namespace boost::asio;
        boost::system::error_code ec;
        const_buffer buffer (cb);
        std::size_t remain (buffer_size(buffer));
        while (remain)
        {
            if (header_bytes_ < header_.size())
            {
                std::size_t const n (buffer_copy (mutable_buffer (header_.data() +
                    header_bytes_, header_.size() - header_bytes_), buffer));
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
                    case protocol::mtHELLO:           ec = invoke <protocol::TMHello> (); break;
                    case protocol::mtPING:            ec = invoke <protocol::TMPing> (); break;
                    case protocol::mtPROOFOFWORK:     ec = invoke <protocol::TMProofWork> (); break;
                    case protocol::mtCLUSTER:         ec = invoke <protocol::TMCluster> (); break;
                    case protocol::mtGET_PEERS:       ec = invoke <protocol::TMGetPeers> (); break;
                    case protocol::mtPEERS:           ec = invoke <protocol::TMPeers> (); break;
                    case protocol::mtENDPOINTS:       ec = invoke <protocol::TMEndpoints> (); break;
                    case protocol::mtTRANSACTION:     ec = invoke <protocol::TMTransaction> (); break;
                    case protocol::mtGET_LEDGER:      ec = invoke <protocol::TMGetLedger> (); break;
                    case protocol::mtLEDGER_DATA:     ec = invoke <protocol::TMLedgerData> (); break;
                    case protocol::mtPROPOSE_LEDGER:  ec = invoke <protocol::TMProposeSet> (); break;
                    case protocol::mtSTATUS_CHANGE:   ec = invoke <protocol::TMStatusChange> (); break;
                    case protocol::mtHAVE_SET:        ec = invoke <protocol::TMHaveTransactionSet> (); break;
                    case protocol::mtVALIDATION:      ec = invoke <protocol::TMValidation> (); break;
                    case protocol::mtGET_OBJECTS:     ec = invoke <protocol::TMGetObjectByHash> (); break;
                    default:
                        ec = handler_.on_message_unknown(type_);
                        break;
                    }
                    header_bytes_ = 0;
                    body_bytes_ = 0;
                }
            }
        }
        return ec;
    }

    /** Push a set of buffers through.
        The handler is called for each complete protocol message contained
        in the buffers.
    */
    template <class ConstBufferSequence>
    boost::system::error_code
    write (ConstBufferSequence const& buffers)
    {
        boost::system::error_code ec;
        for (auto const& buffer : buffers)
        {
            ec = write_one(buffer);
            if (ec)
                break;
        }
        return ec;
    }
};

} // ripple

#endif
