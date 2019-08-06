//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright(c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_BASEHTTPPEER_H_INCLUDED
#define RIPPLE_SERVER_BASEHTTPPEER_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/server/Session.h>
#include <ripple/server/impl/io_list.h>
#include <ripple/beast/net/IPAddressConversion.h>
#include <ripple/beast/asio/ssl_error.h> // for is_short_read?
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/spawn.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

namespace ripple {

/** Represents an active connection. */
template<class Handler, class Impl>
class BaseHTTPPeer
    : public io_list::work
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
        timeoutSeconds = 30,
        timeoutSecondsLocal = 3 //used for localhost clients
    };

    struct buffer
    {
        buffer(void const* ptr, std::size_t len)
            : data(new char[len])
            , bytes(len)
            , used(0)
        {
            memcpy(data.get(), ptr, len);
        }

        std::unique_ptr <char[]> data;
        std::size_t bytes;
        std::size_t used;
    };

    Port const& port_;
    Handler& handler_;
    boost::asio::executor_work_guard<boost::asio::executor> work_;
    boost::asio::strand<boost::asio::executor> strand_;
    waitable_timer timer_;
    endpoint_type remote_address_;
    beast::Journal journal_;

    std::string id_;
    std::size_t nid_;

    boost::asio::streambuf read_buf_;
    http_request_type message_;
    std::vector<buffer> wq_;
    std::vector<buffer> wq2_;
    std::mutex mutex_;
    bool graceful_ = false;
    bool complete_ = false;
    boost::system::error_code ec_;

    int request_count_ = 0;
    std::size_t bytes_in_ = 0;
    std::size_t bytes_out_ = 0;

    //--------------------------------------------------------------------------

public:
    template <class ConstBufferSequence>
    BaseHTTPPeer(
        Port const& port,
        Handler& handler,
        boost::asio::executor const& executor,
        waitable_timer timer,
        beast::Journal journal,
        endpoint_type remote_address,
        ConstBufferSequence const& buffers);

    virtual
    ~BaseHTTPPeer();

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
    fail(error_code ec, char const* what);

    void
    start_timer();

    void
    cancel_timer();

    void
    on_timer(error_code ec);

    void
    do_read(yield_context do_yield);

    void
    on_write(error_code const& ec,
        std::size_t bytes_transferred);

    void
    do_writer(std::shared_ptr <Writer> const& writer,
        bool keep_alive, yield_context do_yield);

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
        return journal_;
    }

    Port const&
    port() override
    {
        return port_;
    }

    beast::IP::Endpoint
    remoteAddress() override
    {
        return beast::IPAddressConversion::from_asio(remote_address_);
    }

    http_request_type&
    request() override
    {
        return message_;
    }

    void
    write(void const* buffer, std::size_t bytes) override;

    void
    write(std::shared_ptr <Writer> const& writer,
        bool keep_alive) override;

    std::shared_ptr<Session>
    detach() override;

    void
    complete() override;

    void
    close(bool graceful) override;
};

//------------------------------------------------------------------------------

template <class Handler, class Impl>
template <class ConstBufferSequence>
BaseHTTPPeer<Handler, Impl>::BaseHTTPPeer(
    Port const& port,
    Handler& handler,
    boost::asio::executor const& executor,
    waitable_timer timer,
    beast::Journal journal,
    endpoint_type remote_address,
    ConstBufferSequence const& buffers)
    : port_(port)
    , handler_(handler)
    , work_(executor)
    , strand_(executor)
    , timer_(std::move(timer))
    , remote_address_(remote_address)
    , journal_(journal)
{
    read_buf_.commit(boost::asio::buffer_copy(read_buf_.prepare(
        boost::asio::buffer_size(buffers)), buffers));
    static std::atomic <int> sid;
    nid_ = ++sid;
    id_ = std::string("#") + std::to_string(nid_) + " ";
    JLOG(journal_.trace()) << id_ <<
        "accept:    " << remote_address_.address();
}

template<class Handler, class Impl>
BaseHTTPPeer<Handler, Impl>::
~BaseHTTPPeer()
{
    handler_.onClose(session(), ec_);
    JLOG(journal_.trace()) << id_ <<
        "destroyed: " << request_count_ <<
           ((request_count_ == 1) ? " request" : " requests");
}

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
close()
{
    if(! strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void (BaseHTTPPeer::*)(void)) & BaseHTTPPeer::close,
                impl().shared_from_this()));
    error_code ec;
    impl().stream_.lowest_layer().close(ec);
}

//------------------------------------------------------------------------------

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
fail(error_code ec, char const* what)
{
    if(! ec_ && ec != boost::asio::error::operation_aborted)
    {
        ec_ = ec;
        JLOG(journal_.trace()) << id_ <<
            std::string(what) << ": " << ec.message();
        impl().stream_.lowest_layer().close(ec);
    }
}

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
start_timer()
{
    error_code ec;
    timer_.expires_from_now(
        std::chrono::seconds(
            remote_address_.address().is_loopback() ?
                timeoutSecondsLocal :
                timeoutSeconds),
        ec);
    if(ec)
        return fail(ec, "start_timer");
    timer_.async_wait(bind_executor(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::on_timer,
            impl().shared_from_this(),
            std::placeholders::_1)));
}

// Convenience for discarding the error code
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
cancel_timer()
{
    error_code ec;
    timer_.cancel(ec);
}

// Called when session times out
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
on_timer(error_code ec)
{
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(! ec)
        ec = boost::system::errc::make_error_code(
            boost::system::errc::timed_out);
    fail(ec, "timer");
}

//------------------------------------------------------------------------------

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
do_read(yield_context do_yield)
{
    complete_ = false;
    error_code ec;
    start_timer();
    boost::beast::http::async_read(impl().stream_,
        read_buf_, message_, do_yield[ec]);
    cancel_timer();
    if(ec == boost::beast::http::error::end_of_stream)
        return do_close();
    if(ec)
        return fail(ec, "http::read");
    do_request();
}

// Send everything in the write queue.
// The write queue must not be empty upon entry.
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
on_write(error_code const& ec,
    std::size_t bytes_transferred)
{
    cancel_timer();
    if(ec)
        return fail(ec, "write");
    bytes_out_ += bytes_transferred;
    {
        std::lock_guard lock(mutex_);
        wq2_.clear();
        wq2_.reserve(wq_.size());
        std::swap(wq2_, wq_);
    }
    if(! wq2_.empty())
    {
        std::vector<boost::asio::const_buffer> v;
        v.reserve(wq2_.size());
        for(auto const& b : wq2_)
            v.emplace_back(b.data.get(), b.bytes);
        start_timer();
        return boost::asio::async_write(
            impl().stream_,
            v,
            bind_executor(
                strand_,
                std::bind(
                    &BaseHTTPPeer::on_write,
                    impl().shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }
    if(! complete_)
        return;
    if(graceful_)
        return do_close();
    boost::asio::spawn(strand_,
        std::bind(&BaseHTTPPeer<Handler, Impl>::do_read,
            impl().shared_from_this(), std::placeholders::_1));
}

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
do_writer(std::shared_ptr <Writer> const& writer,
    bool keep_alive, yield_context do_yield)
{
    std::function <void(void)> resume;
    {
        auto const p = impl().shared_from_this();
        resume = std::function <void(void)>(
            [this, p, writer, keep_alive]()
            {
                boost::asio::spawn(strand_, std::bind(
                    &BaseHTTPPeer<Handler, Impl>::do_writer, p, writer, keep_alive,
                        std::placeholders::_1));
            });
    }

    for(;;)
    {
        if(! writer->prepare(bufferSize, resume))
            return;
        error_code ec;
        auto const bytes_transferred = boost::asio::async_write(
            impl().stream_, writer->data(), boost::asio::transfer_at_least(1),
                do_yield[ec]);
        if(ec)
            return fail(ec, "writer");
        writer->consume(bytes_transferred);
        if(writer->complete())
            break;
    }

    if(! keep_alive)
        return do_close();

    boost::asio::spawn(strand_, std::bind(&BaseHTTPPeer<Handler, Impl>::do_read,
        impl().shared_from_this(), std::placeholders::_1));
}

//------------------------------------------------------------------------------

// Send a copy of the data.
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
write(
    void const* buffer, std::size_t bytes)
{
    if(bytes == 0)
        return;
    if([&]
        {
            std::lock_guard lock(mutex_);
            wq_.emplace_back(buffer, bytes);
            return wq_.size() == 1 && wq2_.size() == 0;
        }())
    {
        if(! strand_.running_in_this_thread())
            return post(
                strand_,
                std::bind(
                    &BaseHTTPPeer::on_write,
                    impl().shared_from_this(),
                    error_code{},
                    0));
        else
            return on_write(error_code{}, 0);
    }
}

template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
write(std::shared_ptr <Writer> const& writer,
    bool keep_alive)
{
    boost::asio::spawn(bind_executor(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::do_writer,
            impl().shared_from_this(),
            writer,
            keep_alive,
            std::placeholders::_1)));
}

// DEPRECATED
// Make the Session asynchronous
template<class Handler, class Impl>
std::shared_ptr<Session>
BaseHTTPPeer<Handler, Impl>::
detach()
{
    return impl().shared_from_this();
}

// DEPRECATED
// Called to indicate the response has been written(but not sent)
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
complete()
{
    if(! strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                &BaseHTTPPeer<Handler, Impl>::complete,
                impl().shared_from_this()));

    message_ = {};
    complete_ = true;

    {
        std::lock_guard lock(mutex_);
        if(! wq_.empty() && ! wq2_.empty())
            return;
    }

    // keep-alive
    boost::asio::spawn(bind_executor(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::do_read,
            impl().shared_from_this(),
            std::placeholders::_1)));
}

// DEPRECATED
// Called from the Handler to close the session.
template<class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::
close(bool graceful)
{
    if(! strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void (BaseHTTPPeer::*)(bool)) &
                    BaseHTTPPeer<Handler, Impl>::close,
                impl().shared_from_this(),
                graceful));

    complete_ = true;
    if(graceful)
    {
        graceful_ = true;
        {
            std::lock_guard lock(mutex_);
            if(! wq_.empty() || ! wq2_.empty())
                return;
        }
        return do_close();
    }

    error_code ec;
    impl().stream_.lowest_layer().close(ec);
}

} // ripple

#endif
