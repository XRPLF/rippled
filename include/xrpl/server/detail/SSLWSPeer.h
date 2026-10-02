#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/detail/BaseHTTPPeer.h>
#include <xrpl/server/detail/BaseWSPeer.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <chrono>
#include <memory>
#include <utility>

namespace xrpl {

template <class Handler>
class SSLWSPeer : public BaseWSPeer<Handler, SSLWSPeer<Handler>>,
                  public std::enable_shared_from_this<SSLWSPeer<Handler>>
{
    friend class BasePeer<Handler, SSLWSPeer>;
    friend class BaseWSPeer<Handler, SSLWSPeer>;

    using ClockType = std::chrono::system_clock;
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;
    using SocketType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<SocketType>;
    using WaitableTimer = boost::asio::basic_waitable_timer<ClockType>;

    std::unique_ptr<StreamType> streamPtr_;
    boost::beast::websocket::stream<StreamType&> ws_;

public:
    template <class Body, class Headers>
    SSLWSPeer(
        Port const& port,
        Handler& handler,
        EndpointType remoteEndpoint,
        boost::beast::http::request<Body, Headers>&& request,
        std::unique_ptr<StreamType>&& streamPtr,
        beast::Journal journal);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class Body, class Headers>
SSLWSPeer<Handler>::SSLWSPeer(
    Port const& port,
    Handler& handler,
    EndpointType remoteEndpoint,
    boost::beast::http::request<Body, Headers>&& request,
    std::unique_ptr<StreamType>&& streamPtr,
    beast::Journal journal)
    : BaseWSPeer<Handler, SSLWSPeer>(
          port,
          handler,
          streamPtr->get_executor(),
          WaitableTimer{streamPtr->get_executor()},
          remoteEndpoint,
          std::move(request),
          journal)
    , streamPtr_(std::move(streamPtr))
    , ws_(*streamPtr_)
{
}

}  // namespace xrpl
