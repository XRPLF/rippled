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
#include <beast/asio/ssl_bundle.h>
#include <beast/cxx14/memory.h> // <memory>

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

// Detects the legacy peer protocol handshake. */
template <class Socket, class StreamBuf, class Yield>
static
std::pair <boost::system::error_code, bool>
detect_peer_protocol (Socket& socket, StreamBuf& buf, Yield yield)
{
    std::pair<boost::system::error_code, bool> result;
    result.second = false;
    for(;;)
    {
        std::size_t const max = 6; // max bytes needed
        unsigned char data[max];
        auto const n = boost::asio::buffer_copy(
            boost::asio::buffer(data), buf.data());

        /* Protocol messages are framed by a 6 byte header which includes
           a big-endian 4-byte length followed by a big-endian 2-byte type.
           The type for 'hello' is 1.
        */
        if (n>=1 && data[0] != 0)
            break;;
        if (n>=2 && data[1] != 0)
            break;
        if (n>=5 && data[4] != 0)
            break;
        if (n>=6)
        {
            if (data[5] == 1)
                result.second = true;
            break;
        }
        std::size_t const bytes_transferred = boost::asio::async_read(
            socket, buf.prepare(max - n), boost::asio::transfer_at_least(1),
                yield[result.first]);
        if (result.first)
            break;
        buf.commit(bytes_transferred);
    }
    return result;
}

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
    bool const legacy = port().protocol.count("peer") > 0;
    bool const http =
        port().protocol.count("peer") > 0 ||
        //|| port().protocol.count("wss") > 0
        port().protocol.count("https") > 0;
    if (legacy)
    {
        auto const result = detect_peer_protocol(stream_, read_buf_, yield);
        if (result.first)
            return fail (result.first, "detect_legacy_handshake");
        if (result.second)
        {
            std::vector<std::uint8_t> storage (read_buf_.size());
            boost::asio::mutable_buffers_1 buffer (
                boost::asio::mutable_buffer(storage.data(), storage.size()));
            boost::asio::buffer_copy(buffer, read_buf_.data());
            return door_.server().handler().onLegacyPeerHello(
                std::move(ssl_bundle_), buffer, remote_address_);
        }
    }
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
    auto const what = door_.server().handler().onMaybeMove (session(),
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
    cancel_timer();
}

void
SSLPeer::on_shutdown (error_code ec)
{
    stream_.lowest_layer().close(ec);
}

}
}

#endif
