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

#ifndef RIPPLE_HTTP_PEER_H_INCLUDED
#define RIPPLE_HTTP_PEER_H_INCLUDED

#include <ripple/http/Server.h>
#include <ripple/http/Session.h>
#include <ripple/http/impl/Types.h>
#include <ripple/http/impl/ServerImpl.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/ssl.h> // for is_short_read?
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/module/core/core.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/spawn.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {
namespace HTTP {

/** Represents an active connection. */
template <class Impl>
class Peer
    : public ServerImpl::Child
    , public Session
{
protected:
    using clock_type = std::chrono::system_clock;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer <clock_type>;

    enum
    {
        // Size of our receive buffer
        bufferSize = 4 * 1024,

        // Max seconds without completing a message
        timeoutSeconds = 30

    };

    struct buffer
    {
        buffer (void const* ptr, std::size_t len)
            : data (new char[len])
            , bytes (len)
            , used (0)
        {
            memcpy (data.get(), ptr, len);
        }

        std::unique_ptr <char[]> data;
        std::size_t bytes;
        std::size_t used;
    };

    Door& door_;
    boost::asio::io_service::work work_;
    boost::asio::io_service::strand strand_;
    waitable_timer timer_;
    endpoint_type endpoint_;
    beast::Journal journal_;

    std::string id_;
    std::size_t nid_;

    boost::asio::streambuf read_buf_;
    beast::http::message message_;
    beast::http::body body_;
    std::list <buffer> write_queue_;
    std::mutex mutex_;
    bool graceful_ = false;
    bool complete_ = false;
    std::shared_ptr <Peer> detach_ref_;
    boost::system::error_code ec_;

    clock_type::time_point when_;
    std::string when_str_;
    int request_count_ = 0;
    std::size_t bytes_in_ = 0;
    std::size_t bytes_out_ = 0;

    //--------------------------------------------------------------------------

public:
    template <class ConstBufferSequence>
    Peer (Door& door, boost::asio::io_service& io_service,
        beast::Journal journal, endpoint_type endpoint,
            ConstBufferSequence const& buffers);

    virtual
    ~Peer();

    Session&
    session()
    {
        return *this;
    }

    void close() override;

protected:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    fail (error_code ec, char const* what);

    void
    start_timer();

    void
    cancel_timer();

    void
    on_timer (error_code ec);

    void
    do_read (boost::asio::yield_context yield);

    void
    do_write (boost::asio::yield_context yield);

    virtual
    void
    do_request() = 0;

    virtual
    void
    do_close() = 0;

    // Session

    beast::Journal
    journal() override
    {
        return door_.server().journal();
    }

    beast::IP::Endpoint
    remoteAddress() override
    {
        return from_asio (endpoint_);
    }

    beast::http::message&
    request() override
    {
        return message_;
    }

    beast::http::body const&
    body() override
    {
        return body_;
    }

    void
    write (void const* buffer, std::size_t bytes) override;

    void
    detach() override;

    void
    complete() override;

    void
    close (bool graceful) override;
};

//------------------------------------------------------------------------------

class PlainPeer
    : public Peer <PlainPeer>
    , public std::enable_shared_from_this <PlainPeer>
{
private:
    friend class Peer <PlainPeer>;
    using socket_type = boost::asio::ip::tcp::socket;
    socket_type socket_;

public:
    template <class ConstBufferSequence>
    PlainPeer (Door& door, beast::Journal journal,
        endpoint_type endpoint, ConstBufferSequence const& buffers,
            socket_type&& socket);

    void
    accept();

private:
    void
    do_request();

    void
    do_close();
};

template <class ConstBufferSequence>
PlainPeer::PlainPeer (Door& door,
        beast::Journal journal, endpoint_type endpoint,
            ConstBufferSequence const& buffers,
                boost::asio::ip::tcp::socket&& socket)
    : Peer (door, socket.get_io_service(), journal, endpoint, buffers)
    , socket_(std::move(socket))
{
}

void
PlainPeer::accept ()
{
    door_.server().handler().onAccept (session());
    if (! socket_.is_open())
        return;

    boost::asio::spawn (strand_, std::bind (&PlainPeer::do_read,
        shared_from_this(), std::placeholders::_1));
}

void
PlainPeer::do_request()
{
    // Perform half-close when Connection: close and not SSL
    error_code ec;
    if (! message_.keep_alive())
        socket_.shutdown (socket_type::shutdown_receive, ec);

    if (! ec)
    {
        ++request_count_;
        door_.server().handler().onRequest (session());
        return;
    }

    if (ec)
        fail (ec, "request");
}

void
PlainPeer::do_close()
{
    error_code ec;
    socket_.shutdown (socket_type::shutdown_send, ec);
}

//------------------------------------------------------------------------------

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
    SSLPeer (Door& door, beast::Journal journal,
        endpoint_type endpoint, ConstBufferSequence const& buffers,
            next_layer_type&& socket);

    void
    accept();

private:
    void
    do_handshake (boost::asio::yield_context yield);

    void
    do_request();

    void
    do_close();

    void
    on_shutdown (error_code ec);
};

template <class ConstBufferSequence>
SSLPeer::SSLPeer (Door& door, beast::Journal journal,
    endpoint_type endpoint, ConstBufferSequence const& buffers,
        boost::asio::ip::tcp::socket&& socket)
    : Peer (door, socket.get_io_service(), journal, endpoint, buffers)
    , next_layer_ (std::move(socket))
    , socket_ (next_layer_, door_.port().context->get())
{
}

// Called when the acceptor accepts our socket.
void
SSLPeer::accept ()
{
    door_.server().handler().onAccept (session());
    if (! next_layer_.is_open())
        return;

    boost::asio::spawn (strand_, std::bind (&SSLPeer::do_handshake,
        shared_from_this(), std::placeholders::_1));
}

void
SSLPeer::do_handshake (boost::asio::yield_context yield)
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
    door_.server().handler().onRequest (session());
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

//------------------------------------------------------------------------------

template <class Impl>
template <class ConstBufferSequence>
Peer<Impl>::Peer (Door& door, boost::asio::io_service& io_service,
    beast::Journal journal, endpoint_type endpoint,
        ConstBufferSequence const& buffers)
    : door_(door)
    , work_ (io_service)
    , strand_ (io_service)
    , timer_ (io_service)
    , endpoint_ (endpoint)
    , journal_ (journal)
{
    door_.add(*this);
    read_buf_.commit(boost::asio::buffer_copy(read_buf_.prepare (
        boost::asio::buffer_size (buffers)), buffers));
    static std::atomic <int> sid;
    nid_ = ++sid;
    id_ = std::string("#") + std::to_string(nid_) + " ";
    if (journal_.trace) journal_.trace << id_ <<
        "accept:    " << endpoint.address();
    when_ = clock_type::now();
    when_str_ = beast::Time::getCurrentTime().formatted (
        "%Y-%b-%d %H:%M:%S").toStdString();
}

template <class Impl>
Peer<Impl>::~Peer()
{
    Stat stat;
    stat.id = nid_;
    stat.when = std::move (when_str_);
    stat.elapsed = std::chrono::duration_cast <
        std::chrono::seconds> (clock_type::now() - when_);
    stat.requests = request_count_;
    stat.bytes_in = bytes_in_;
    stat.bytes_out = bytes_out_;
    stat.ec = std::move (ec_);
    door_.server().report (std::move (stat));
    door_.server().handler().onClose (session(), ec_);
    if (journal_.trace) journal_.trace << id_ <<
        "destroyed: " << request_count_ <<
            ((request_count_ == 1) ? " request" : " requests");
    door_.remove (*this);
}

template <class Impl>
void
Peer<Impl>::close()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            (void(Peer::*)(void))&Peer::close,
                impl().shared_from_this()));
    error_code ec;
    timer_.cancel(ec);
    impl().socket_.lowest_layer().close(ec);
}

//------------------------------------------------------------------------------

template <class Impl>
void
Peer<Impl>::fail (error_code ec, char const* what)
{
    if (! ec_ && ec != boost::asio::error::operation_aborted)
    {
        ec_ = ec;
        if (journal_.trace) journal_.trace << id_ <<
            std::string(what) << ": " << ec.message();
        impl().socket_.lowest_layer().close (ec);
    }
}

template <class Impl>
void
Peer<Impl>::start_timer()
{
    error_code ec;
    timer_.expires_from_now (std::chrono::seconds(timeoutSeconds), ec);
    if (ec)
        return fail (ec, "start_timer");
    timer_.async_wait (strand_.wrap (std::bind (
        &Peer<Impl>::on_timer, impl().shared_from_this(),
            beast::asio::placeholders::error)));
}

// Convenience for discarding the error code
template <class Impl>
void
Peer<Impl>::cancel_timer()
{
    error_code ec;
    timer_.cancel(ec);
}

// Called when session times out
template <class Impl>
void
Peer<Impl>::on_timer (error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (! ec)
        ec = boost::system::errc::make_error_code (
            boost::system::errc::timed_out);
    fail (ec, "timer");
}

//------------------------------------------------------------------------------

template <class Impl>
void
Peer<Impl>::do_read (boost::asio::yield_context yield)
{
    complete_ = false;

    error_code ec;
    bool eof = false;
    body_.clear();
    beast::http::parser parser (message_, body_, true);
    for(;;)
    {
        if (read_buf_.size() == 0)
        {
            start_timer();
            auto const bytes_transferred = boost::asio::async_read (
                impl().socket_, read_buf_.prepare (bufferSize),
                    boost::asio::transfer_at_least(1), yield[ec]);
            cancel_timer();

            eof = ec == boost::asio::error::eof;
            if (eof)
            {
                ec = error_code{};
            }
            else if (! ec)
            {
                bytes_in_ += bytes_transferred;
                read_buf_.commit (bytes_transferred);
            }
        }

        if (! ec)
        {
            if (! eof)
            {
                // VFALCO TODO Currently parsing errors are treated the
                //             same as the connection dropping. Instead, we
                // should request that the handler compose a proper HTTP error
                // response. This requires refactoring HTTPReply() into
                // something sensible.
                std::size_t used;
                std::tie (ec, used) = parser.write (read_buf_.data());
                if (! ec)
                    read_buf_.consume (used);
            }
            else
            {
                ec = parser.write_eof();
            }
        }

        if (! ec)
        {
            if (parser.complete())
                return do_request();
            else if (eof)
                ec = boost::asio::error::eof; // incomplete request
        }

        if (ec)
            return fail (ec, "read");
    }
}

// Send everything in the write queue.
// The write queue must not be empty upon entry.
template <class Impl>
void
Peer<Impl>::do_write (boost::asio::yield_context yield)
{
    error_code ec;
    std::size_t bytes = 0;
    for(;;)
    {
        bytes_out_ += bytes;

        bool empty;
        void const* data;
        {
            std::lock_guard <std::mutex> lock (mutex_);
            buffer& b = write_queue_.front();
            b.used += bytes;
            if (b.used < b.bytes)
            {
                empty = false;
            }
            else
            {
                write_queue_.pop_front();
                empty = write_queue_.empty();
            }
            data = b.data.get() + b.used;
            bytes = b.bytes - b.used;
        }
        if (empty)
            break;

        start_timer();
        bytes = boost::asio::async_write (impl().socket_,
            boost::asio::buffer (data, bytes),
                boost::asio::transfer_at_least(1), yield[ec]);
        cancel_timer();

        if (ec)
            return fail (ec, "write");
    }

    if (! complete_)
        return;

    if (graceful_)
        return do_close();

    boost::asio::spawn (strand_, std::bind (&Peer<Impl>::do_read,
        impl().shared_from_this(), std::placeholders::_1));
}

//------------------------------------------------------------------------------

// Send a copy of the data.
template <class Impl>
void
Peer<Impl>::write (void const* buffer, std::size_t bytes)
{
    if (bytes == 0)
        return;

    bool empty;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        empty = write_queue_.empty();
        write_queue_.emplace_back (buffer, bytes);
    }

    if (empty)
        boost::asio::spawn (strand_, std::bind (&Peer<Impl>::do_write,
            impl().shared_from_this(), std::placeholders::_1));
}

// Make the Session asynchronous
template <class Impl>
void
Peer<Impl>::detach ()
{
    // Maintain an additional reference while detached
    if (! detach_ref_)
        detach_ref_ = impl().shared_from_this();
}

// Called to indicate the response has been written (but not sent)
template <class Impl>
void
Peer<Impl>::complete()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind (&Peer<Impl>::complete,
            impl().shared_from_this()));

    // Reattach
    detach_ref_.reset();

    message_ = beast::http::message{};
    complete_ = true;

    if (! write_queue_.empty())
        return;

    // keep-alive
    boost::asio::spawn (strand_, std::bind (&Peer<Impl>::do_read,
        impl().shared_from_this(), std::placeholders::_1));
}

// Called from the Handler to close the session.
template <class Impl>
void
Peer<Impl>::close (bool graceful)
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            (void(Peer::*)(bool))&Peer<Impl>::close,
                impl().shared_from_this(), graceful));

    // Reattach
    detach_ref_.reset();

    complete_ = true;

    if (graceful)
    {
        graceful_ = true;

        if (! write_queue_.empty())
            return;
    }

    error_code ec;
    timer_.cancel (ec);
    impl().socket_.lowest_layer().close (ec);
}

}
}

#endif
