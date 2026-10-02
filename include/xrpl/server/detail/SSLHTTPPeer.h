#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/WSSession.h>
#include <xrpl/server/detail/BaseHTTPPeer.h>
#include <xrpl/server/detail/SSLWSPeer.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <memory>
#include <utility>

namespace xrpl {

template <class Handler>
class SSLHTTPPeer : public BaseHTTPPeer<Handler, SSLHTTPPeer<Handler>>,
                    public std::enable_shared_from_this<SSLHTTPPeer<Handler>>
{
private:
    friend class BaseHTTPPeer<Handler, SSLHTTPPeer>;
    using SocketType = boost::asio::ip::tcp::socket;
    using MiddleType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<MiddleType>;
    using EndpointType = boost::asio::ip::tcp::endpoint;
    using YieldContext = boost::asio::yield_context;
    using ErrorCode = boost::system::error_code;

    std::unique_ptr<StreamType> streamPtr_;
    StreamType& stream_;
    SocketType& socket_;

public:
    template <class ConstBufferSequence>
    SSLHTTPPeer(
        Port const& port,
        Handler& handler,
        boost::asio::io_context& ioc,
        beast::Journal journal,
        EndpointType remoteAddress,
        ConstBufferSequence const& buffers,
        MiddleType&& stream);

    void
    run();

    std::shared_ptr<WSSession>
    websocketUpgrade() override;

private:
    void
    doHandshake(YieldContext doYield);

    void
    doRequest() override;

    void
    doClose() override;

    void
    onShutdown(ErrorCode ec);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class ConstBufferSequence>
SSLHTTPPeer<Handler>::SSLHTTPPeer(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    beast::Journal journal,
    EndpointType remoteAddress,
    ConstBufferSequence const& buffers,
    MiddleType&& stream)
    : BaseHTTPPeer<Handler, SSLHTTPPeer>(
          port,
          handler,
          ioc.get_executor(),
          journal,
          remoteAddress,
          buffers)
    , streamPtr_(std::make_unique<StreamType>(MiddleType(std::move(stream)), *port.context))
    , stream_(*streamPtr_)
    , socket_(stream_.next_layer().socket())
{
}

// Called when the acceptor accepts our socket.
template <class Handler>
void
SSLHTTPPeer<Handler>::run()
{
    if (!this->handler_.onAccept(this->session(), this->remoteAddress_))
    {
        util::spawn(
            this->strand_, [self = this->shared_from_this()](YieldContext) { self->doClose(); });
        return;
    }
    if (!socket_.is_open())
        return;
    util::spawn(this->strand_, [self = this->shared_from_this()](YieldContext doYield) {
        self->doHandshake(doYield);
    });
}

template <class Handler>
std::shared_ptr<WSSession>
SSLHTTPPeer<Handler>::websocketUpgrade()
{
    auto ws = this->ios().template emplace<SSLWSPeer<Handler>>(
        this->port_,
        this->handler_,
        this->remoteAddress_,
        std::move(this->message_),
        std::move(this->streamPtr_),
        this->journal_);
    return ws;
}

template <class Handler>
void
SSLHTTPPeer<Handler>::doHandshake(YieldContext doYield)
{
    boost::system::error_code ec;
    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    this->startTimer();
    this->readBuf_.consume(
        stream_.async_handshake(StreamType::server, this->readBuf_.data(), doYield[ec]));
    this->cancelTimer();
    if (ec == boost::beast::error::timeout)
        return this->onTimer();
    if (ec)
        return this->fail(ec, "handshake");
    bool const http = this->port().protocol.count("peer") > 0 ||
        this->port().protocol.count("wss") > 0 || this->port().protocol.count("wss2") > 0 ||
        this->port().protocol.count("https") > 0;
    if (http)
    {
        util::spawn(this->strand_, [self = this->shared_from_this()](YieldContext doYield) {
            self->doRead(doYield);
        });
        return;
    }
    // `this` will be destroyed
}

template <class Handler>
void
SSLHTTPPeer<Handler>::doRequest()
{
    ++this->requestCount_;
    auto const what = this->handler_.onHandoff(
        this->session(), std::move(streamPtr_), std::move(this->message_), this->remoteAddress_);
    if (what.moved)
        return;
    if (what.response)
        return this->write(what.response, what.keepAlive);
    // legacy
    this->handler_.onRequest(this->session());
}

template <class Handler>
void
SSLHTTPPeer<Handler>::doClose()
{
    this->startTimer();
    stream_.async_shutdown(bind_executor(
        this->strand_,
        [self = this->shared_from_this()](ErrorCode const& ec) { self->onShutdown(ec); }));
}

template <class Handler>
void
SSLHTTPPeer<Handler>::onShutdown(ErrorCode ec)
{
    this->cancelTimer();

    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(this->journal_.debug()) << "on_shutdown: " << ec.message();
    }

    // Close socket now in case this->destructor is delayed
    stream_.next_layer().close();
}

}  // namespace xrpl
