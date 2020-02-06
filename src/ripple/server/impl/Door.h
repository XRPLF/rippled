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
#include <boost/beast/core/detect_ssl.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
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
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::beast::tcp_stream;

    // Detects SSL on a socket
    class Detector
        : public io_list::work
        , public std::enable_shared_from_this<Detector>
    {
    private:
        Port const& port_;
        Handler& handler_;
        boost::asio::io_context& ioc_;
        stream_type stream_;
        socket_type &socket_;
        timer_type timer_;
        endpoint_type remote_address_;
        boost::asio::io_context::strand strand_;
        beast::Journal const j_;

    public:
        Detector(
            Port const& port,
            Handler& handler,
            boost::asio::io_context& ioc,
            stream_type&& stream,
            endpoint_type remote_address,
            beast::Journal j);
        void run();
        void close() override;

    private:
        void do_timer (yield_context yield);
        void do_detect (yield_context yield);
    };

    beast::Journal const j_;
    Port const& port_;
    Handler& handler_;
    boost::asio::io_context& ioc_;
    acceptor_type acceptor_;
    boost::asio::io_context::strand strand_;
    bool ssl_;
    bool plain_;

public:
    Door(Handler& handler, boost::asio::io_context& io_context,
        Port const& port, beast::Journal j);

    // Work-around because we can't call shared_from_this in ctor
    void run();

    /** Close the Door listening socket and connections.
        The listening socket is closed, and all open connections
        belonging to the Door are closed.
        Thread Safety:
            May be called concurrently
    */
    void close() override;

    endpoint_type get_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    template <class ConstBufferSequence>
    void create (bool ssl, ConstBufferSequence const& buffers,
        stream_type&& stream, endpoint_type remote_address);

    void do_accept (yield_context yield);
};

template <class Handler>
Door<Handler>::Detector::Detector(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    stream_type&& stream,
    endpoint_type remote_address,
    beast::Journal j)
    : port_(port)
    , handler_(handler)
    , ioc_(ioc)
    , stream_(std::move(stream))
    , socket_(stream_.socket())
    , timer_(ioc_)
    , remote_address_(remote_address)
    , strand_(ioc_)
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
    boost::beast::multi_buffer buf(16);
    timer_.expires_from_now(std::chrono::seconds(15));
    boost::system::error_code ec;
    bool const ssl = async_detect_ssl(socket_, buf, do_yield[ec]);
    error_code unused;
    timer_.cancel(unused);
    if (! ec)
    {
        if (ssl)
        {
            if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
                 port_, handler_, ioc_, j_, remote_address_,
                     buf.data(), std::move(stream_)))
                sp->run();
            return;
        }
        if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
             port_, handler_, ioc_, j_, remote_address_,
                 buf.data(), std::move(stream_)))
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
Door(Handler& handler, boost::asio::io_context& io_context,
        Port const& port, beast::Journal j)
    : j_(j)
    , port_(port)
    , handler_(handler)
    , ioc_(io_context)
    , acceptor_(io_context)
    , strand_(io_context)
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
    stream_type&& stream, endpoint_type remote_address)
{
    if (ssl)
    {
        if (auto sp = ios().template emplace<SSLHTTPPeer<Handler>>(
             port_, handler_, ioc_, j_, remote_address,
                 buffers, std::move(stream)))
            sp->run();
        return;
    }
    if (auto sp = ios().template emplace<PlainHTTPPeer<Handler>>(
         port_, handler_, ioc_, j_, remote_address,
             buffers, std::move(stream)))
        sp->run();
}

template<class Handler>
void
Door<Handler>::
do_accept(boost::asio::yield_context do_yield)
{
    while (acceptor_.is_open())
    {
        error_code ec;
        endpoint_type remote_address;
        stream_type stream (ioc_);
        socket_type &socket = stream.socket();
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
            if (auto sp = ios().template emplace<Detector>(
                 port_, handler_, ioc_, std::move(stream),
                     remote_address, j_))
                sp->run();
        }
        else if (ssl_ || plain_)
        {
            create(ssl_, boost::asio::null_buffers{},
                std::move(stream), remote_address);
        }
    }
}

} // ripple

#endif
