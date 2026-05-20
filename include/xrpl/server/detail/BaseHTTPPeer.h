#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/server/Session.h>
#include <xrpl/server/detail/Spawn.h>
#include <xrpl/server/detail/io_list.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

/** Represents an active connection. */
template <class Handler, class Impl>
class BaseHTTPPeer : public IOList::Work, public Session
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using yield_context = boost::asio::yield_context;

    static constexpr auto kBufferSize = 4 * 1024;    // size of read/write buffer
    static constexpr auto kTimeoutSeconds = 30;      // max seconds without completing a message
    static constexpr auto kTimeoutSecondsLocal = 3;  // used for localhost clients

    struct Buffer
    {
        Buffer(void const* ptr, std::size_t len) : data(new char[len]), bytes(len)
        {
            memcpy(data.get(), ptr, len);
        }

        std::unique_ptr<char[]> data;
        std::size_t bytes;
        std::size_t used{0};
    };

    Port const& port_;
    Handler& handler_;
    boost::asio::executor_work_guard<boost::asio::executor> work_;
    boost::asio::strand<boost::asio::executor> strand_;
    endpoint_type remote_address_;
    beast::Journal const journal_;

    std::string id_;
    std::size_t nid_;

    boost::asio::streambuf read_buf_;
    http_request_type message_;
    std::vector<Buffer> wq_;
    std::vector<Buffer> wq2_;
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
        beast::Journal journal,
        endpoint_type remoteAddress,
        ConstBufferSequence const& buffers);

    ~BaseHTTPPeer() override;

    Session&
    session()
    {
        return *this;
    }

    void
    close() override;

protected:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    fail(error_code ec, char const* what);

    void
    startTimer();

    void
    cancelTimer();

    void
    onTimer();

    void
    doRead(yield_context doYield);

    void
    onWrite(error_code const& ec, std::size_t bytesTransferred);

    void
    doWriter(std::shared_ptr<Writer> const& writer, bool keepAlive, yield_context doYield);

    virtual void
    doRequest() = 0;

    virtual void
    doClose() = 0;

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
        return beast::IPAddressConversion::fromAsio(remote_address_);
    }

    http_request_type&
    request() override
    {
        return message_;
    }

    void
    write(void const* buffer, std::size_t bytes) override;

    void
    write(std::shared_ptr<Writer> const& writer, bool keepAlive) override;

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
    beast::Journal journal,
    endpoint_type remoteAddress,
    ConstBufferSequence const& buffers)
    : port_(port)
    , handler_(handler)
    , work_(boost::asio::make_work_guard(executor))
    , strand_(boost::asio::make_strand(executor))
    , remote_address_(std::move(remoteAddress))
    , journal_(journal)
{
    read_buf_.commit(
        boost::asio::buffer_copy(read_buf_.prepare(boost::asio::buffer_size(buffers)), buffers));
    static std::atomic<int> kSid;
    nid_ = ++kSid;
    id_ = std::string("#") + std::to_string(nid_) + " ";
    JLOG(journal_.trace()) << id_ << "accept:    " << remote_address_.address();
}

template <class Handler, class Impl>
BaseHTTPPeer<Handler, Impl>::~BaseHTTPPeer()
{
    handler_.onClose(session(), ec_);
    JLOG(journal_.trace()) << id_ << "destroyed: " << request_count_
                           << ((request_count_ == 1) ? " request" : " requests");
}

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::close()
{
    if (!strand_.running_in_this_thread())
    {
        return post(
            strand_,
            std::bind(
                (void (BaseHTTPPeer::*)(void))&BaseHTTPPeer::close, impl().shared_from_this()));
    }
    boost::beast::get_lowest_layer(impl().stream_).close();
}

//------------------------------------------------------------------------------

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::fail(error_code ec, char const* what)
{
    if (!ec_ && ec != boost::asio::error::operation_aborted)
    {
        ec_ = ec;
        JLOG(journal_.trace()) << id_ << std::string(what) << ": " << ec.message();
        boost::beast::get_lowest_layer(impl().stream_).close();
    }
}

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::startTimer()
{
    boost::beast::get_lowest_layer(impl().stream_)
        .expires_after(
            std::chrono::seconds(
                remote_address_.address().is_loopback() ? kTimeoutSecondsLocal : kTimeoutSeconds));
}

// Convenience for discarding the error code
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::cancelTimer()
{
    boost::beast::get_lowest_layer(impl().stream_).expires_never();
}

// Called when session times out
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::onTimer()
{
    auto ec = boost::system::errc::make_error_code(boost::system::errc::timed_out);
    fail(ec, "timer");
}

//------------------------------------------------------------------------------

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::doRead(yield_context doYield)
{
    complete_ = false;
    error_code ec;
    startTimer();
    boost::beast::http::async_read(impl().stream_, read_buf_, message_, doYield[ec]);
    cancelTimer();
    if (ec == boost::beast::http::error::end_of_stream)
        return doClose();
    if (ec == boost::beast::error::timeout)
        return onTimer();
    if (ec)
        return fail(ec, "http::read");
    doRequest();
}

// Send everything in the write queue.
// The write queue must not be empty upon entry.
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::onWrite(error_code const& ec, std::size_t bytesTransferred)
{
    cancelTimer();
    if (ec == boost::beast::error::timeout)
        return onTimer();
    if (ec)
        return fail(ec, "write");
    bytes_out_ += bytesTransferred;
    {
        std::scoped_lock const lock(mutex_);
        wq2_.clear();
        wq2_.reserve(wq_.size());
        std::swap(wq2_, wq_);
    }
    if (!wq2_.empty())
    {
        std::vector<boost::asio::const_buffer> v;
        v.reserve(wq2_.size());
        for (auto const& b : wq2_)
            v.emplace_back(b.data.get(), b.bytes);
        startTimer();
        return boost::asio::async_write(
            impl().stream_,
            v,
            bind_executor(
                strand_,
                std::bind(
                    &BaseHTTPPeer::onWrite,
                    impl().shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }
    if (!complete_)
        return;
    if (graceful_)
        return doClose();
    util::spawn(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::doRead,
            impl().shared_from_this(),
            std::placeholders::_1));
}

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::doWriter(
    std::shared_ptr<Writer> const& writer,
    bool keepAlive,
    yield_context doYield)
{
    std::function<void(void)> resume;
    {
        auto const p = impl().shared_from_this();
        resume = std::function<void(void)>([this, p, writer, keepAlive]() {
            util::spawn(
                strand_,
                std::bind(
                    &BaseHTTPPeer<Handler, Impl>::doWriter,
                    p,
                    writer,
                    keepAlive,
                    std::placeholders::_1));
        });
    }

    for (;;)
    {
        if (!writer->prepare(kBufferSize, resume))
            return;
        error_code ec;
        auto const bytesTransferred = boost::asio::async_write(
            impl().stream_, writer->data(), boost::asio::transfer_at_least(1), doYield[ec]);
        if (ec)
            return fail(ec, "writer");
        writer->consume(bytesTransferred);
        if (writer->complete())
            break;
    }

    if (!keepAlive)
        return doClose();

    util::spawn(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::doRead,
            impl().shared_from_this(),
            std::placeholders::_1));
}

//------------------------------------------------------------------------------

// Send a copy of the data.
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::write(void const* buf, std::size_t bytes)
{
    if (bytes == 0)
        return;
    if ([&] {
            std::scoped_lock const lock(mutex_);
            wq_.emplace_back(buf, bytes);
            return wq_.size() == 1 && wq2_.size() == 0;
        }())
    {
        if (!strand_.running_in_this_thread())
        {
            return post(
                strand_,
                std::bind(&BaseHTTPPeer::onWrite, impl().shared_from_this(), error_code{}, 0));
        }
        return onWrite(error_code{}, 0);
    }
}

template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::write(std::shared_ptr<Writer> const& writer, bool keepAlive)
{
    util::spawn(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::doWriter,
            impl().shared_from_this(),
            writer,
            keepAlive,
            std::placeholders::_1));
}

// DEPRECATED
// Make the Session asynchronous
template <class Handler, class Impl>
std::shared_ptr<Session>
BaseHTTPPeer<Handler, Impl>::detach()
{
    return impl().shared_from_this();
}

// DEPRECATED
// Called to indicate the response has been written(but not sent)
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::complete()
{
    if (!strand_.running_in_this_thread())
    {
        return post(
            strand_, std::bind(&BaseHTTPPeer<Handler, Impl>::complete, impl().shared_from_this()));
    }

    message_ = {};
    complete_ = true;

    {
        std::scoped_lock const lock(mutex_);
        if (!wq_.empty() && !wq2_.empty())
            return;
    }

    // keep-alive
    util::spawn(
        strand_,
        std::bind(
            &BaseHTTPPeer<Handler, Impl>::doRead,
            impl().shared_from_this(),
            std::placeholders::_1));
}

// DEPRECATED
// Called from the Handler to close the session.
template <class Handler, class Impl>
void
BaseHTTPPeer<Handler, Impl>::close(bool graceful)
{
    if (!strand_.running_in_this_thread())
    {
        return post(
            strand_,
            std::bind(
                (void (BaseHTTPPeer::*)(bool))&BaseHTTPPeer<Handler, Impl>::close,
                impl().shared_from_this(),
                graceful));
    }

    complete_ = true;
    if (graceful)
    {
        graceful_ = true;
        {
            std::scoped_lock const lock(mutex_);
            if (!wq_.empty() || !wq2_.empty())
                return;
        }
        return doClose();
    }

    boost::beast::get_lowest_layer(impl().stream_).close();
}

}  // namespace xrpl
