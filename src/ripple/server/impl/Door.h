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

#ifndef RIPPLE_SERVER_DOOR_H_INCLUDED
#define RIPPLE_SERVER_DOOR_H_INCLUDED

#include <ripple/server/impl/io_list.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/server/impl/PlainHTTPPeer.h>
#include <ripple/server/impl/SSLHTTPPeer.h>
#include <ripple/beast/asio/ssl_bundle.h>
#include <beast/core/multi_buffer.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/container/flat_map.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace ripple {

/** A listening socket. */
template<class Handler>
class Door
    : public io_list::work
    , public std::enable_shared_from_this<Door<Handler>>
{
private:
    using clock_type = std::chrono::steady_clock;
    using timer_type = boost::asio::basic_waitable_timer<clock_type>;
    using error_code = boost::system::error_code;
    using yield_context = boost::asio::yield_context;
    using protocol_type = boost::asio::ip::tcp;
    using acceptor_type = protocol_type::acceptor;
    using endpoint_type = protocol_type::endpoint;
    using socket_type = protocol_type::socket;

    // Detects SSL on a socket
    class Detector
        : public io_list::work
        , public std::enable_shared_from_this<Detector>
    {
    private:
        Port const& port_;
        Handler& handler_;
        socket_type socket_;
        timer_type timer_;
        endpoint_type remote_address_;
        boost::asio::io_service::strand strand_;
        beast::Journal j_;

    public:
        Detector (Port const& port, Handler& handler,
            socket_type&& socket, endpoint_type remote_address,
                beast::Journal j);
        void run();
        void close() override;

    private:
        void do_timer (yield_context yield);
        void do_detect (yield_context yield);
    };

    beast::Journal j_;
    Port const& port_;
    Handler& handler_;
    acceptor_type acceptor_;
    boost::asio::io_service::strand strand_;
    bool ssl_;
    bool plain_;

public:
    Door(Handler& handler, boost::asio::io_service& io_service,
        Port const& port, beast::Journal j);

    // Work-around because we can't call shared_from_this in ctor
    void run();

    /** Close the Door listening socket and connections.
        The listening socket is closed, and all open connections
        belonging to the Door are closed.
        Thread Safety:
            May be called concurrently
    */
    void close();

    endpoint_type get_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    template <class ConstBufferSequence>
    void create (bool ssl, ConstBufferSequence const& buffers,
        socket_type&& socket, endpoint_type remote_address);

    void do_accept (yield_context yield);
};

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
    @param do_yield A do_yield context
    @return The error code if an error occurs, otherwise `true` if
            the data read indicates the SSL client handshake.
*/
template <class Socket, class StreamBuf, class Yield>
std::pair <boost::system::error_code, bool>
detect_ssl (Socket& socket, StreamBuf& buf, Yield do_yield)
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
                do_yield[result.first]));
        if (result.first)
            break;
    }
    return result;
}

template<class Handler>
Door<Handler>::Detector::
Detector(Port const& port,
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

template<class Handler>
void
Door<Handler>::Detector::
run()
{
    // do_detect must be called before do_timer or else
    // the timer can be canceled before it gets set.
    boost::asio::spawn(strand_, std::bind (&Detector::do_detect,
        this->shared_from_this(), std::placeholders::_1));

    boost::asio::spawn(strand_, std::bind (&Detector::do_timer,
        this->shared_from_this(), std::placeholders::_1));
}

template<class Handler>
void
Door<Handler>::Detector::
close()
{
    error_code ec;
    socket_.close(ec);
    timer_.cancel(ec);
}

template<class Handler>
void
Door<Handler>::Detector::
do_timer(yield_context do_yield)
{
    error_code ec; // ignored
    while (socket_.is_open())
    {
        timer_.async_wait (do_yield[ec]);
        if (timer_.expires_from_now() <= std::chrono::seconds(0))
            socket_.close(ec);
    }
}

template<class Handler>
void
Door<Handler>::Detector::
do_detect(boost::asio::yield_context do_yield)
{
    bool ssl;
    error_code ec;
    beast::multi_buffer buf(16);
    timer_.expires_from_now(std::chrono::seconds(15));
    std::tie(ec, ssl) = detect_ssl(socket_, buf, do_yield);
    error_code unused;
    timer_.cancel(unused);
    if (! ec)
    {
        if (ssl)
        {
            if(auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                port_, handler_, j_, remote_address_,
                    buf.data(), std::move(socket_)))
                sp->run();
            return;
        }
        if(auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
            port_, handler_, j_, remote_address_,
                buf.data(), std::move(socket_)))
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

template<class Handler>
Door<Handler>::
Door(Handler& handler, boost::asio::io_service& io_service,
        Port const& port, beast::Journal j)
    : j_(j)
    , port_(port)
    , handler_(handler)
    , acceptor_(io_service)
    , strand_(io_service)
    , ssl_(
        port_.protocol.count("https") > 0 ||
        port_.protocol.count("wss") > 0 ||
        port_.protocol.count("wss2")  > 0 ||
        port_.protocol.count("peer")  > 0)
    , plain_(
        port_.protocol.count("http") > 0 ||
        port_.protocol.count("ws") > 0 ||
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

template<class Handler>
void
Door<Handler>::
run()
{
    boost::asio::spawn(strand_, std::bind(&Door<Handler>::do_accept,
        this->shared_from_this(), std::placeholders::_1));
}

template<class Handler>
void
Door<Handler>::
close()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &Door<Handler>::close, this->shared_from_this()));
    error_code ec;
    acceptor_.close(ec);
}

//------------------------------------------------------------------------------

template<class Handler>
template<class ConstBufferSequence>
void
Door<Handler>::
create(bool ssl, ConstBufferSequence const& buffers,
    socket_type&& socket, endpoint_type remote_address)
{
    if (ssl)
    {
        if(auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
            port_, handler_, j_, remote_address,
                buffers, std::move(socket)))
            sp->run();
        return;
    }
    if(auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
        port_, handler_, j_, remote_address,
            buffers, std::move(socket)))
        sp->run();
}

template<class Handler>
void
Door<Handler>::
do_accept(boost::asio::yield_context do_yield)
{
    for(;;)
    {
        error_code ec;
        endpoint_type remote_address;
        socket_type socket (acceptor_.get_io_service());
        acceptor_.async_accept (socket, remote_address, do_yield[ec]);
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
            if(auto sp = ios().template emplace<Detector>(
                port_, handler_, std::move(socket),
                    remote_address, j_))
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

#endif
