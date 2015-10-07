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

#ifndef RIPPLE_SERVER_SSLPEER_H_INCLUDED
#define RIPPLE_SERVER_SSLPEER_H_INCLUDED

#include <ripple/server/impl/Peer.h>
#include <beast/asio/ssl_bundle.h>
#include <memory>

namespace ripple {
namespace HTTP {

class SSLPeer
    : public Peer <SSLPeer>
    , public std::enable_shared_from_this <SSLPeer>
{
private:
    friend class Peer <SSLPeer>;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream <socket_type&>;

    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    stream_type& stream_;

public:
    template <class ConstBufferSequence>
    SSLPeer (Door& door, beast::Journal journal, endpoint_type remote_address,
        ConstBufferSequence const& buffers, socket_type&& socket);

    void
    run();

private:
    void
    do_handshake (yield_context yield);

    void
    do_request();

    void
    do_close();

    void
    on_shutdown (error_code ec);
};

//------------------------------------------------------------------------------

template <class ConstBufferSequence>
SSLPeer::SSLPeer (Door& door, beast::Journal journal,
    endpoint_type remote_address, ConstBufferSequence const& buffers,
        socket_type&& socket)
    : Peer (door, socket.get_io_service(), journal, remote_address, buffers)
    , ssl_bundle_(std::make_unique<beast::asio::ssl_bundle>(
        port().context, std::move(socket)))
    , stream_(ssl_bundle_->stream)
{
}

// Called when the acceptor accepts our socket.
void
SSLPeer::run()
{
    door_.server().handler().onAccept (session());
    if (! stream_.lowest_layer().is_open())
        return;

    boost::asio::spawn (strand_, std::bind (&SSLPeer::do_handshake,
        shared_from_this(), std::placeholders::_1));
}

void
SSLPeer::do_handshake (yield_context yield)
{
    error_code ec;
    stream_.set_verify_mode (boost::asio::ssl::verify_none);
    start_timer();
    read_buf_.consume(stream_.async_handshake(
        stream_type::server, read_buf_.data(), yield[ec]));
    cancel_timer();
    if (ec)
        return fail (ec, "handshake");
    bool const http =
        port().protocol.count("peer") > 0 ||
        //|| port().protocol.count("wss") > 0
        port().protocol.count("https") > 0;
    if (http)
    {
        boost::asio::spawn (strand_, std::bind (&SSLPeer::do_read,
            shared_from_this(), std::placeholders::_1));
        return;
    }
    // this will be destroyed
}

void
SSLPeer::do_request()
{
    ++request_count_;
    auto const what = door_.server().handler().onHandoff (session(),
        std::move(ssl_bundle_), std::move(message_), remote_address_);
    if (what.moved)
        return;
    if (what.response)
        return write(what.response, what.keep_alive);
    // legacy
    door_.server().handler().onRequest (session());
}

void
SSLPeer::do_close()
{
    start_timer();
    stream_.async_shutdown (strand_.wrap (std::bind (
        &SSLPeer::on_shutdown, shared_from_this(),
            std::placeholders::_1)));
}

void
SSLPeer::on_shutdown (error_code ec)
{
    cancel_timer();
    stream_.lowest_layer().close(ec);
}

}
}

#endif
