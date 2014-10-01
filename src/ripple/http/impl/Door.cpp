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

#include <ripple/http/impl/Door.h>
#include <ripple/http/impl/Peer.h>
#include <boost/asio/buffer.hpp>
#include <beast/asio/placeholders.h>
#include <boost/logic/tribool.hpp>
#include <functional>

#include <beast/streams/debug_ostream.h>

namespace ripple {
namespace HTTP {

/** Detect SSL client handshakes.
    Analyzes the bytes in the provided buffer to detect the SSL client
    handshake. If the buffer contains insufficient data, more data will be
    read from the stream until there is enough to determine a result.
    No bytes are discarded from buf. Any additional bytes read are retained.
    buf must provide an interface compatible with boost::asio::streambuf
        http://www.boost.org/doc/libs/1_56_0/doc/html/boost_asio/reference/streambuf.html
    See
        http://www.ietf.org/rfc/rfc2246.txt
        Section 7.4. Handshake protocol
    @param socket The stream to read from
    @param buf A buffer to hold the received data
    @param yield A yield context
    @return The error code if an error occurs, otherwise `true` if
            the data read indicates the SSL client handshake.
*/
template <class Socket, class StreamBuf, class Yield>
std::pair <boost::system::error_code, bool>
detect_ssl (Socket& socket, StreamBuf& buf, Yield yield)
{
    std::pair <boost::system::error_code, bool> result;
    result.second = false;
    for(;;)
    {
        std::size_t const max = 4; // the most bytes we could need
        unsigned char data[max];
        auto const bytes = boost::asio::buffer_copy (
            boost::asio::buffer(data), buf.data());

        if (bytes > 0)
        {
            if (data[0] != 0x16) // message type 0x16 = "SSL Handshake"
                break;
        }

        if (bytes >= max)
        {
            result.second = true;
            break;
        }

        std::size_t const bytes_transferred = boost::asio::async_read (socket,
            buf.prepare(max - bytes), boost::asio::transfer_at_least(1),
                yield[result.first]);
        if (result.first)
            break;
        buf.commit (bytes_transferred);
    }
    return result;
}

//------------------------------------------------------------------------------

Door::connection::connection (Door& door, socket_type&& socket,
        endpoint_type endpoint)
    : door_ (door)
    , socket_ (std::move(socket))
    , endpoint_ (endpoint)
    , strand_ (door.io_service_)
    , timer_ (door.io_service_)
{
}

// Work-around because we can't call shared_from_this in ctor
void
Door::connection::run()
{
    boost::asio::spawn (strand_, std::bind (&connection::do_detect,
        shared_from_this(), std::placeholders::_1));

    boost::asio::spawn (strand_, std::bind (&connection::do_timer,
        shared_from_this(), std::placeholders::_1));
}

void
Door::connection::do_timer (yield_context yield)
{
    error_code ec; // ignored
    while (socket_.is_open())
    {
        timer_.async_wait (yield[ec]);
        if (timer_.expires_from_now() <= std::chrono::seconds(0))
            socket_.close();
    }
}

void
Door::connection::do_detect (boost::asio::yield_context yield)
{
    bool ssl;
    error_code ec;
    boost::asio::streambuf buf;
    timer_.expires_from_now (std::chrono::seconds(15));
    std::tie(ec, ssl) = detect_ssl (socket_, buf, yield);
    if (! ec)
    {
        if (ssl)
        {
            auto const peer = std::make_shared <SSLPeer> (door_.server_,
                door_.port_, door_.server_.journal(), endpoint_,
                    buf.data(), std::move(socket_));
            peer->accept();
            return;
        }

        auto const peer = std::make_shared <PlainPeer> (door_.server_,
            door_.port_, door_.server_.journal(), endpoint_,
                buf.data(), std::move(socket_));
        peer->accept();
        return;
    }

    socket_.close();
    timer_.cancel();
}

//------------------------------------------------------------------------------

Door::Door (boost::asio::io_service& io_service,
        ServerImpl& impl, Port const& port)
    : io_service_ (io_service)
    , timer_ (io_service)
    , acceptor_ (io_service, to_asio (port))
    , port_ (port)
    , server_ (impl)
{
    server_.add (*this);

    error_code ec;

    acceptor_.set_option (acceptor::reuse_address (true), ec);
    if (ec)
    {
        server_.journal().error <<
            "Error setting acceptor socket option: " << ec.message();
    }

    if (! ec)
    {
        server_.journal().info << "Bound to endpoint " <<
            to_string (acceptor_.local_endpoint());
    }
    else
    {
        server_.journal().error << "Error binding to endpoint " <<
            to_string (acceptor_.local_endpoint()) <<
            ", '" << ec.message() << "'";
    }
}

Door::~Door ()
{
    server_.remove (*this);
}

void
Door::listen()
{
    boost::asio::spawn (io_service_, std::bind (&Door::do_accept,
        shared_from_this(), std::placeholders::_1));
}

void
Door::cancel ()
{
    acceptor_.cancel();
}

//------------------------------------------------------------------------------

void
Door::do_accept (boost::asio::yield_context yield)
{
    for(;;)
    {
        error_code ec;
        endpoint_type endpoint;
        socket_type socket (io_service_);
        acceptor_.async_accept (socket, endpoint, yield[ec]);
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
                server_.journal().error <<
                    "accept: " << ec.message();
            break;
        }

        if (port_.security == Port::Security::no_ssl)
        {
            auto const peer = std::make_shared <PlainPeer> (server_,
                port_, server_.journal(), endpoint,
                    boost::asio::null_buffers(), std::move(socket));
            peer->accept();
        }
        else if (port_.security == Port::Security::require_ssl)
        {
            auto const peer = std::make_shared <SSLPeer> (server_,
                port_, server_.journal(), endpoint,
                    boost::asio::null_buffers(), std::move(socket));
            peer->accept();
        }
        else
        {
            auto const c = std::make_shared <connection> (
                *this, std::move(socket), endpoint);
            c->run();
        }
    }
}

}
}
