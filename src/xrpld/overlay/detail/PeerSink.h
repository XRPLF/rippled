//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#ifndef RIPPLE_OVERLAY_PEERSINK_H_INCLUDED
#define RIPPLE_OVERLAY_PEERSINK_H_INCLUDED

#include <xrpld/overlay/detail/Handshake.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace ripple {

class PeerSink
{
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using error_code = boost::system::error_code;

public:
    PeerSink(
        std::unique_ptr<stream_type>&& stream_ptr,
        beast::Journal const journal)
        : stream_ptr_(std::move(stream_ptr))
        , strand_(stream_ptr_->next_layer().socket().get_executor())
        , journal_(journal)
    {
    }
    PeerSink(PeerSink&&) = default;
    PeerSink&
    operator=(PeerSink&&) = default;

    PeerSink(PeerSink const&) = delete;
    PeerSink&
    operator=(PeerSink const&) = delete;

    bool
    is_open()
    {
        return stream_ptr_->next_layer().socket().is_open();
    }

    boost::asio::executor const&
    get_executor()
    {
        return stream_ptr_->next_layer().socket().get_executor();
    }

    void
    close()
    {
        if (!is_open())
            return;
        error_code ec;
        stream_ptr_->shutdown(ec);
        stream_ptr_->next_layer().socket().close(ec);
    }

    template <typename Buf, typename Handler>
    void
    async_write(Buf const& buffers, Handler handler)
    {
        boost::asio::async_write(*stream_ptr_, buffers, handler);
    }

    template <typename Buf, typename CompleteCondition, typename Handler>
    void
    async_write(Buf const& buffers, CompleteCondition all, Handler handler)
    {
        boost::asio::async_write(*stream_ptr_, buffers, all, handler);
    }

    template <typename Seq, typename Handler>
    void
    async_read_some(Seq const& buffers, Handler handler)
    {
        stream_ptr_->async_read_some(buffers, handler);
    }

    std::optional<uint256>
    getSharedValue()
    {
        return makeSharedValue(*stream_ptr_, journal_);
    }

private:
    std::unique_ptr<stream_type> stream_ptr_;
    boost::asio::strand<boost::asio::executor> strand_;
    beast::Journal const journal_;
};
}  // namespace ripple

#endif