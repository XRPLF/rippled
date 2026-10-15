#pragma once

#include <xrpld/app/misc/detail/Work.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/BuildInfo.h>

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace xrpl::detail {

template <class Impl>
class WorkBase : public Work
{
protected:
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;

public:
    using CallbackType = std::function<void(ErrorCode const&, EndpointType const&, ResponseType&&)>;

protected:
    using SocketType = boost::asio::ip::tcp::socket;
    using ResolverType = boost::asio::ip::tcp::resolver;
    using ResultsType = boost::asio::ip::tcp::resolver::results_type;
    using RequestType = boost::beast::http::request<boost::beast::http::empty_body>;

    std::string host_;
    std::string path_;
    std::string port_;
    CallbackType cb_;
    boost::asio::io_context& ios_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    ResolverType resolver_;
    SocketType socket_;
    RequestType req_;
    ResponseType res_;
    boost::beast::multi_buffer readBuf_;
    EndpointType lastEndpoint_;
    bool lastStatus_;

private:
    WorkBase(
        std::string host,
        std::string path,
        std::string port,
        boost::asio::io_context& ios,
        EndpointType lastEndpoint,
        bool lastStatus,
        CallbackType cb);

public:
    ~WorkBase() override;

    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    run() override;

    void
    cancel() override;

    void
    fail(ErrorCode const& ec);

    void
    onResolve(ErrorCode const& ec, ResultsType results);

    void
    onConnect(ErrorCode const& ec, EndpointType const& endpoint);

    void
    onStart();

    void
    onRequest(ErrorCode const& ec);

    void
    onResponse(ErrorCode const& ec);

private:
    void
    close();

    friend Impl;
};

//------------------------------------------------------------------------------

template <class Impl>
WorkBase<Impl>::WorkBase(
    std::string host,
    std::string path,
    std::string port,
    boost::asio::io_context& ios,
    EndpointType lastEndpoint,
    bool lastStatus,
    CallbackType cb)
    : host_(std::move(host))
    , path_(std::move(path))
    , port_(std::move(port))
    , cb_(std::move(cb))
    , ios_(ios)
    , strand_(boost::asio::make_strand(ios))
    , resolver_(ios)
    , socket_(ios)
    , lastEndpoint_{std::move(lastEndpoint)}
    , lastStatus_(lastStatus)
{
}

template <class Impl>
WorkBase<Impl>::~WorkBase()
{
    if (cb_)
        cb_(make_error_code(boost::system::errc::not_a_socket), lastEndpoint_, std::move(res_));
    close();
}

template <class Impl>
void
WorkBase<Impl>::run()
{
    if (!strand_.running_in_this_thread())
    {
        return boost::asio::post(
            ios_, boost::asio::bind_executor(strand_, [self = impl().shared_from_this()] {
                self->run();
            }));
    }

    resolver_.async_resolve(
        host_,
        port_,
        boost::asio::bind_executor(
            strand_, [self = impl().shared_from_this()](ErrorCode const& ec, ResultsType results) {
                self->onResolve(ec, results);
            }));
}

template <class Impl>
void
WorkBase<Impl>::cancel()
{
    if (!strand_.running_in_this_thread())
    {
        return boost::asio::post(
            ios_,

            boost::asio::bind_executor(
                strand_, [self = impl().shared_from_this()] { self->cancel(); }));
    }

    ErrorCode ec;
    resolver_.cancel();
    socket_.cancel(ec);
}

template <class Impl>
void
WorkBase<Impl>::fail(ErrorCode const& ec)
{
    if (cb_)
    {
        cb_(ec, lastEndpoint_, std::move(res_));
        cb_ = nullptr;
    }
}

template <class Impl>
void
WorkBase<Impl>::onResolve(ErrorCode const& ec, ResultsType results)
{
    if (ec)
        return fail(ec);

    boost::asio::async_connect(
        socket_,
        results,
        boost::asio::bind_executor(
            strand_,
            [self = impl().shared_from_this()](ErrorCode const& ec, EndpointType const& endpoint) {
                // Call the base-class overload explicitly: the derived Impl
                // hides it with its own single-argument onConnect(ec).
                self->WorkBase::onConnect(ec, endpoint);
            }));
}

template <class Impl>
void
WorkBase<Impl>::onConnect(ErrorCode const& ec, EndpointType const& endpoint)
{
    lastEndpoint_ = endpoint;

    if (ec)
        return fail(ec);

    impl().onConnect(ec);
}

template <class Impl>
void
WorkBase<Impl>::onStart()
{
    req_.method(boost::beast::http::verb::get);
    req_.target(path_.empty() ? "/" : path_);
    req_.version(11);
    req_.set("Host", host_ + ":" + port_);
    req_.set("User-Agent", build_info::getFullVersionString());
    req_.prepare_payload();
    boost::beast::http::async_write(
        impl().stream(),
        req_,
        boost::asio::bind_executor(
            strand_, [self = impl().shared_from_this()](ErrorCode const& ec, std::size_t) {
                self->onRequest(ec);
            }));
}

template <class Impl>
void
WorkBase<Impl>::onRequest(ErrorCode const& ec)
{
    if (ec)
        return fail(ec);

    boost::beast::http::async_read(
        impl().stream(),
        readBuf_,
        res_,
        boost::asio::bind_executor(
            strand_, [self = impl().shared_from_this()](ErrorCode const& ec, std::size_t) {
                self->onResponse(ec);
            }));
}

template <class Impl>
void
WorkBase<Impl>::onResponse(ErrorCode const& ec)
{
    if (ec)
        return fail(ec);

    close();
    XRPL_ASSERT(cb_, "xrpl::detail::WorkBase::onResponse : callback is set");
    cb_(ec, lastEndpoint_, std::move(res_));
    cb_ = nullptr;
}

template <class Impl>
void
WorkBase<Impl>::close()
{
    if (socket_.is_open())
    {
        ErrorCode ec;
        socket_.shutdown(boost::asio::socket_base::shutdown_send, ec);
        if (ec)
            return;
        socket_.close(ec);
    }
}

}  // namespace xrpl::detail
