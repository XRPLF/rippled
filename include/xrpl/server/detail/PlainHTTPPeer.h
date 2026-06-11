#pragma once

#include <xrpl/beast/rfc2616.h>
#include <xrpl/server/detail/BaseHTTPPeer.h>
#include <xrpl/server/detail/PlainWSPeer.h>

#include <boost/beast/core/tcp_stream.hpp>

#include <memory>

namespace xrpl {

template <class Handler>
class PlainHTTPPeer : public BaseHTTPPeer<Handler, PlainHTTPPeer<Handler>>,
                      public std::enable_shared_from_this<PlainHTTPPeer<Handler>>
{
private:
    friend class BaseHTTPPeer<Handler, PlainHTTPPeer>;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::beast::tcp_stream;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

    stream_type stream_;
    socket_type& socket_;

public:
    template <class ConstBufferSequence>
    PlainHTTPPeer(
        Port const& port,
        Handler& handler,
        boost::asio::io_context& ioc,
        beast::Journal journal,
        endpoint_type remoteAddress,
        ConstBufferSequence const& buffers,
        stream_type&& stream);

    void
    run();

    std::shared_ptr<WSSession>
    websocketUpgrade() override;

private:
    void
    doRequest() override;

    void
    doClose() override;
};

//------------------------------------------------------------------------------

template <class Handler>
template <class ConstBufferSequence>
PlainHTTPPeer<Handler>::PlainHTTPPeer(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    beast::Journal journal,
    endpoint_type remoteEndpoint,
    ConstBufferSequence const& buffers,
    stream_type&& stream)
    : BaseHTTPPeer<Handler, PlainHTTPPeer>(
          port,
          handler,
          ioc.get_executor(),
          journal,
          remoteEndpoint,
          buffers)
    , stream_(std::move(stream))
    , socket_(stream_.socket())
{
    // Set TCP_NODELAY on loopback interfaces,
    // otherwise Nagle's algorithm makes Env
    // tests run slower on Linux systems.
    //
    if (remoteEndpoint.address().is_loopback())
        socket_.set_option(boost::asio::ip::tcp::no_delay{true});
}

template <class Handler>
void
PlainHTTPPeer<Handler>::run()
{
    if (!this->handler_.onAccept(this->session(), this->remoteAddress_))
    {
        util::spawn(this->strand_, std::bind(&PlainHTTPPeer::doClose, this->shared_from_this()));
        return;
    }

    if (!socket_.is_open())
        return;

    util::spawn(
        this->strand_,
        std::bind(&PlainHTTPPeer::doRead, this->shared_from_this(), std::placeholders::_1));
}

template <class Handler>
std::shared_ptr<WSSession>
PlainHTTPPeer<Handler>::websocketUpgrade()
{
    auto ws = this->ios().template emplace<PlainWSPeer<Handler>>(
        this->port_,
        this->handler_,
        this->remoteAddress_,
        std::move(this->message_),
        std::move(stream_),
        this->journal_);
    return ws;
}

template <class Handler>
void
PlainHTTPPeer<Handler>::doRequest()
{
    ++this->requestCount_;
    auto const what =
        this->handler_.onHandoff(this->session(), std::move(this->message_), this->remoteAddress_);
    if (what.moved)
        return;
    boost::system::error_code ec;
    if (what.response)
    {
        // half-close on Connection: close
        if (!what.keepAlive)
            socket_.shutdown(socket_type::shutdown_receive, ec);
        if (ec)
            return this->fail(ec, "request");
        return this->write(what.response, what.keepAlive);
    }

    // Perform half-close when Connection: close and not SSL
    if (!beast::rfc2616::isKeepAlive(this->message_))
        socket_.shutdown(socket_type::shutdown_receive, ec);
    if (ec)
        return this->fail(ec, "request");
    // legacy
    this->handler_.onRequest(this->session());
}

template <class Handler>
void
PlainHTTPPeer<Handler>::doClose()
{
    boost::system::error_code ec;
    socket_.shutdown(socket_type::shutdown_send, ec);
}

}  // namespace xrpl
