#pragma once

#include <xrpl/server/WSSession.h>
#include <xrpl/server/detail/BaseHTTPPeer.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <memory>

namespace xrpl {

template <class Handler>
class SSLWSPeer : public BaseWSPeer<Handler, SSLWSPeer<Handler>>,
                  public std::enable_shared_from_this<SSLWSPeer<Handler>>
{
    friend class BasePeer<Handler, SSLWSPeer>;
    friend class BaseWSPeer<Handler, SSLWSPeer>;

    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;
    using waitable_timer = boost::asio::basic_waitable_timer<clock_type>;

    std::unique_ptr<stream_type> streamPtr_;
    boost::beast::websocket::stream<stream_type&> ws_;

public:
    template <class Body, class Headers>
    SSLWSPeer(
        Port const& port,
        Handler& handler,
        endpoint_type remoteEndpoint,
        boost::beast::http::request<Body, Headers>&& request,
        std::unique_ptr<stream_type>&& streamPtr,
        beast::Journal journal);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class Body, class Headers>
SSLWSPeer<Handler>::SSLWSPeer(
    Port const& port,
    Handler& handler,
    endpoint_type remoteEndpoint,
    boost::beast::http::request<Body, Headers>&& request,
    std::unique_ptr<stream_type>&& streamPtr,
    beast::Journal journal)
    : BaseWSPeer<Handler, SSLWSPeer>(
          port,
          handler,
          streamPtr->get_executor(),
          waitable_timer{streamPtr->get_executor()},
          remoteEndpoint,
          std::move(request),
          journal)
    , streamPtr_(std::move(streamPtr))
    , ws_(*streamPtr_)
{
}

}  // namespace xrpl
