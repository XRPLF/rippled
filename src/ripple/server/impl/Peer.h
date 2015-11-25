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

#ifndef RIPPLE_SERVER_PEER_H_INCLUDED
#define RIPPLE_SERVER_PEER_H_INCLUDED

#include <ripple/server/impl/Door.h>
#include <ripple/server/Session.h>
#include <ripple/server/impl/ServerImpl.h>
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/error.h> // for is_short_read?
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/module/core/time/Time.h>
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
#include <type_traits>

namespace ripple {
namespace HTTP {

/** Represents an active connection. */
template <class Impl>
class Peer
    : public Door::Child
    , public Session
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer <clock_type>;
    using yield_context = boost::asio::yield_context;

    enum
    {
        // Size of our read/write buffer
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

    boost::asio::io_service::work work_;
    boost::asio::io_service::strand strand_;
    waitable_timer timer_;
    endpoint_type remote_address_;
    std::string forwarded_for_;
    std::string user_;
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
        beast::Journal journal, endpoint_type remote_address,
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
    do_read (yield_context yield);

    void
    do_write (yield_context yield);

    void
    do_writer (std::shared_ptr <Writer> const& writer,
        bool keep_alive, yield_context yield);

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

    Port const&
    port() override
    {
        return door_.port();
    }

    beast::IP::Endpoint
    remoteAddress() override
    {
        return beast::IPAddressConversion::from_asio(remote_address_);
    }

    std::string
    user() override
    {
        return user_;
    }

    std::string
    forwarded_for() override
    {
        return forwarded_for_;
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
    write (std::shared_ptr <Writer> const& writer,
        bool keep_alive) override;

    std::shared_ptr<Session>
    detach() override;

    void
    complete() override;

    void
    close (bool graceful) override;
};

//------------------------------------------------------------------------------

template <class Impl>
template <class ConstBufferSequence>
Peer<Impl>::Peer (Door& door, boost::asio::io_service& io_service,
    beast::Journal journal, endpoint_type remote_address,
        ConstBufferSequence const& buffers)
    : Child(door)
    , work_ (io_service)
    , strand_ (io_service)
    , timer_ (io_service)
    , remote_address_ (remote_address)
    , journal_ (journal)
{
    read_buf_.commit(boost::asio::buffer_copy(read_buf_.prepare (
        boost::asio::buffer_size (buffers)), buffers));
    static std::atomic <int> sid;
    nid_ = ++sid;
    id_ = std::string("#") + std::to_string(nid_) + " ";
    if (journal_.trace) journal_.trace << id_ <<
        "accept:    " << remote_address_.address();
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
    impl().stream_.lowest_layer().close(ec);
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
        impl().stream_.lowest_layer().close (ec);
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
Peer<Impl>::do_read (yield_context yield)
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
                impl().stream_, read_buf_.prepare (bufferSize),
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
            {
                auto const iter = message_.headers.find ("X-Forwarded-For");
                if (iter != message_.headers.end())
                    forwarded_for_ = iter->second;
                auto const iter2 = message_.headers.find ("X-User");
                if (iter2 != message_.headers.end())
                    user_ = iter2->second;

                return do_request();
            }
            else if (eof)
            {
                ec = boost::asio::error::eof; // incomplete request
            }
        }

        if (ec)
            return fail (ec, "read");
    }
}

// Send everything in the write queue.
// The write queue must not be empty upon entry.
template <class Impl>
void
Peer<Impl>::do_write (yield_context yield)
{
    error_code ec;
    std::size_t bytes = 0;
    for(;;)
    {
        bytes_out_ += bytes;
        void const* data;
        {
            std::lock_guard <std::mutex> lock (mutex_);
            assert(! write_queue_.empty());
            buffer& b1 = write_queue_.front();
            b1.used += bytes;
            if (b1.used >= b1.bytes)
            {
                write_queue_.pop_front();
                if (write_queue_.empty())
                    break;
                buffer& b2 = write_queue_.front();
                data = b2.data.get();
                bytes = b2.bytes;
            }
            else
            {
                data = b1.data.get() + b1.used;
                bytes = b1.bytes - b1.used;
            }
        }

        start_timer();
        bytes = boost::asio::async_write (impl().stream_,
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

template <class Impl>
void
Peer<Impl>::do_writer (std::shared_ptr <Writer> const& writer,
    bool keep_alive, yield_context yield)
{
    std::function <void(void)> resume;
    {
        auto const p = impl().shared_from_this();
        resume = std::function <void(void)>(
            [this, p, writer, keep_alive]()
            {
                boost::asio::spawn (strand_, std::bind (
                    &Peer<Impl>::do_writer, p, writer, keep_alive,
                        std::placeholders::_1));
            });
    }

    for(;;)
    {
        if (! writer->prepare (bufferSize, resume))
            return;
        error_code ec;
        auto const bytes_transferred = boost::asio::async_write (
            impl().stream_, writer->data(), boost::asio::transfer_at_least(1),
                yield[ec]);
        if (ec)
            return fail (ec, "writer");
        writer->consume(bytes_transferred);
        if (writer->complete())
            break;
    }

    if (! keep_alive)
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

template <class Impl>
void
Peer<Impl>::write (std::shared_ptr <Writer> const& writer,
    bool keep_alive)
{
    boost::asio::spawn (strand_, std::bind (
        &Peer<Impl>::do_writer, impl().shared_from_this(),
            writer, keep_alive, std::placeholders::_1));
}

// DEPRECATED
// Make the Session asynchronous
template <class Impl>
std::shared_ptr<Session>
Peer<Impl>::detach()
{
    return impl().shared_from_this();
}

// DEPRECATED
// Called to indicate the response has been written (but not sent)
template <class Impl>
void
Peer<Impl>::complete()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind (&Peer<Impl>::complete,
            impl().shared_from_this()));

    message_ = beast::http::message{};
    complete_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (! write_queue_.empty())
            return;
    }

    // keep-alive
    boost::asio::spawn (strand_, std::bind (&Peer<Impl>::do_read,
        impl().shared_from_this(), std::placeholders::_1));
}

// DEPRECATED
// Called from the Handler to close the session.
template <class Impl>
void
Peer<Impl>::close (bool graceful)
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            (void(Peer::*)(bool))&Peer<Impl>::close,
                impl().shared_from_this(), graceful));

    complete_ = true;
    if (graceful)
    {
        graceful_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (! write_queue_.empty())
                return;
        }
        return do_close();
    }

    error_code ec;
    impl().stream_.lowest_layer().close (ec);
}

}
}

#endif
