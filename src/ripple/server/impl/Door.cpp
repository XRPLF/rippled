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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/server/impl/Door.h>
#include <ripple/server/impl/PlainHTTPPeer.h>
#include <ripple/server/impl/SSLHTTPPeer.h>
#include <boost/asio/buffer.hpp>
#include <beast/placeholders.hpp>
#include <ripple/beast/asio/ssl_bundle.h>
#include <functional>

namespace ripple {

/** Detect SSL client handshakes.
    Analyzes the bytes in the provided buffer to detect the SSL client
    handshake. If the buffer contains insufficient data, more data will be
    read from the stream until there is enough to determine a result.
    No bytes are discarded from buf. Any additional bytes read are retained.
    buf must provide an interface compatible with boost::asio::streambuf
    http://boost.org/doc/libs/1_56_0/doc/html/boost_asio/reference/streambuf.html
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

        buf.commit(boost::asio::async_read (socket,
            buf.prepare(max - bytes), boost::asio::transfer_at_least(1),
                yield[result.first]));
        if (result.first)
            break;
    }
    return result;
}

//------------------------------------------------------------------------------

Door::Detector::Detector(Port const& port,
    Handler& handler, socket_type&& socket,
        endpoint_type remote_address, beast::Journal j)
    : port_(port)
    , handler_(handler)
    , socket_(std::move(socket))
    , timer_(socket_.get_io_service())
    , remote_address_(remote_address)
    , strand_(socket_.get_io_service())
    , j_(j)
{
}

void
Door::Detector::run()
{
    // do_detect must be called before do_timer or else
    // the timer can be canceled before it gets set.
    boost::asio::spawn (strand_, std::bind (&Detector::do_detect,
        shared_from_this(), std::placeholders::_1));

    boost::asio::spawn (strand_, std::bind (&Detector::do_timer,
        shared_from_this(), std::placeholders::_1));
}

void
Door::Detector::close()
{
    error_code ec;
    socket_.close(ec);
    timer_.cancel(ec);
}

void
Door::Detector::do_timer (yield_context yield)
{
    error_code ec; // ignored
    while (socket_.is_open())
    {
        timer_.async_wait (yield[ec]);
        if (timer_.expires_from_now() <= std::chrono::seconds(0))
            socket_.close(ec);
    }
}

void
Door::Detector::do_detect (boost::asio::yield_context yield)
{
    bool ssl;
    error_code ec;
    beast::streambuf buf(16);
    timer_.expires_from_now(std::chrono::seconds(15));
    std::tie(ec, ssl) = detect_ssl(socket_, buf, yield);
    error_code unused;
    timer_.cancel(unused);
    if (! ec)
    {
        if (ssl)
        {
            if(auto sp = ios().emplace<SSLHTTPPeer>(port_, handler_,
                j_, remote_address_, buf.data(),
                    std::move(socket_)))
                sp->run();
            return;
        }
        if(auto sp = ios().emplace<PlainHTTPPeer>(port_, handler_,
            j_, remote_address_, buf.data(),
                std::move(socket_)))
            sp->run();
        return;
    }
    if (ec != boost::asio::error::operation_aborted)
    {
        JLOG(j_.trace()) <<
            "Error detecting ssl: " << ec.message() <<
                " from " << remote_address_;
    }
}

//------------------------------------------------------------------------------

Door::Door (Handler& handler, boost::asio::io_service& io_service,
        Port const& port, beast::Journal j)
    : j_(j)
    , port_(port)
    , handler_(handler)
    , acceptor_(io_service)
    , strand_(io_service)
    , ssl_(
        port_.protocol.count("https") > 0 ||
        //port_.protocol.count("wss") > 0 ||
        port_.protocol.count("wss2")  > 0 ||
        port_.protocol.count("peer")  > 0)
    , plain_(
        port_.protocol.count("http") > 0 ||
        //port_.protocol.count("ws") > 0 ||
        port_.protocol.count("ws2"))
{
    error_code ec;
    endpoint_type const local_address =
        endpoint_type(port.ip, port.port);

    acceptor_.open(local_address.protocol(), ec);
    if (ec)
    {
        JLOG(j_.error()) <<
            "Open port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.set_option(
        boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec)
    {
        JLOG(j_.error()) <<
            "Option for port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.bind(local_address, ec);
    if (ec)
    {
        JLOG(j_.error()) <<
            "Bind port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.listen(boost::asio::socket_base::max_connections, ec);
    if (ec)
    {
        JLOG(j_.error()) <<
            "Listen on port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    JLOG(j_.info()) <<
        "Opened " << port;
}

void
Door::run()
{
    boost::asio::spawn (strand_, std::bind(&Door::do_accept,
        shared_from_this(), std::placeholders::_1));
}

void
Door::close()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &Door::close, shared_from_this()));
    error_code ec;
    acceptor_.close(ec);
}

//------------------------------------------------------------------------------

template <class ConstBufferSequence>
void
Door::create (bool ssl, ConstBufferSequence const& buffers,
    socket_type&& socket, endpoint_type remote_address)
{
    if (ssl)
    {
        if(auto sp = ios().emplace<SSLHTTPPeer>(port_, handler_,
            j_, remote_address, buffers,
                std::move(socket)))
            sp->run();
        return;
    }
    if(auto sp = ios().emplace<PlainHTTPPeer>(port_, handler_,
        j_, remote_address, buffers,
            std::move(socket)))
        sp->run();
}

void
Door::do_accept (boost::asio::yield_context yield)
{
    for(;;)
    {
        error_code ec;
        endpoint_type remote_address;
        socket_type socket (acceptor_.get_io_service());
        acceptor_.async_accept (socket, remote_address, yield[ec]);
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(j_.error()) <<
                "accept: " << ec.message();
        }
        if (ec == boost::asio::error::operation_aborted)
            break;
        if (ec)
            continue;

        if (ssl_ && plain_)
        {
            if(auto sp = ios().emplace<Detector>(port_,
                handler_, std::move(socket), remote_address,
                    j_))
                sp->run();
        }
        else if (ssl_ || plain_)
        {
            create(ssl_, boost::asio::null_buffers{},
                std::move(socket), remote_address);
        }
    }
}

} // ripple
