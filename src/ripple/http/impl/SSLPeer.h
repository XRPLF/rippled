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

#ifndef RIPPLE_HTTP_SSLPEER_H_INCLUDED
#define RIPPLE_HTTP_SSLPEER_H_INCLUDED

#include <ripple/http/impl/Peer.h>

namespace ripple {
namespace HTTP {

class SSLPeer
    : public Peer <SSLPeer>
    , public std::enable_shared_from_this <SSLPeer>
{
private:
    friend class Peer <SSLPeer>;
    using next_layer_type = boost::asio::ip::tcp::socket;
    using socket_type = boost::asio::ssl::stream <next_layer_type&>;
    next_layer_type next_layer_;
    socket_type socket_;

public:
    template <class ConstBufferSequence>
    SSLPeer (ServerImpl& impl, Port const& port, beast::Journal journal,
        endpoint_type endpoint, ConstBufferSequence const& buffers,
            next_layer_type&& socket);

    void
    accept();

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
SSLPeer::SSLPeer (ServerImpl& server, Port const& port,
    beast::Journal journal, endpoint_type endpoint,
        ConstBufferSequence const& buffers,
            boost::asio::ip::tcp::socket&& socket)
    : Peer (server, port, journal, endpoint, buffers)
    , next_layer_ (std::move(socket))
    , socket_ (next_layer_, port.context->get())
{
}

// Called when the acceptor accepts our socket.
void
SSLPeer::accept ()
{
    server_.handler().onAccept (session());
    if (! next_layer_.is_open())
        return;

    boost::asio::spawn (strand_, std::bind (&SSLPeer::do_handshake,
        shared_from_this(), std::placeholders::_1));
}

void
SSLPeer::do_handshake (yield_context yield)
{
    error_code ec;
    std::size_t const bytes_transferred = socket_.async_handshake (
        socket_type::server, read_buf_.data(), yield[ec]);
    if (ec)
        return fail (ec, "handshake");
    read_buf_.consume (bytes_transferred);
    boost::asio::spawn (strand_, std::bind (&SSLPeer::do_read,
        shared_from_this(), std::placeholders::_1));
}

void
SSLPeer::do_request()
{
    ++request_count_;
    server_.handler().onRequest (session());
}

void
SSLPeer::do_close()
{
    error_code ec;
    socket_.async_shutdown (strand_.wrap (std::bind (
        &SSLPeer::on_shutdown, shared_from_this(),
            std::placeholders::_1)));
}

void
SSLPeer::on_shutdown (error_code ec)
{
    socket_.next_layer().close(ec);
}

}
}

#endif
