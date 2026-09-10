#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/detail/BaseWSPeer.h>

#include <boost/beast/core/tcp_stream.hpp>

#include <chrono>
#include <memory>
#include <utility>

namespace xrpl {

template <class Handler>
class PlainWSPeer : public BaseWSPeer<Handler, PlainWSPeer<Handler>>,
                    public std::enable_shared_from_this<PlainWSPeer<Handler>>
{
    friend class BasePeer<Handler, PlainWSPeer>;
    friend class BaseWSPeer<Handler, PlainWSPeer>;

    using ClockType = std::chrono::system_clock;
    using ErrorCode = boost::system::error_code;
    using EndpointType = boost::asio::ip::tcp::endpoint;
    using WaitableTimer = boost::asio::basic_waitable_timer<ClockType>;
    using SocketType = boost::beast::tcp_stream;

    boost::beast::websocket::stream<SocketType> ws_;

public:
    template <class Body, class Headers>
    PlainWSPeer(
        Port const& port,
        Handler& handler,
        EndpointType remoteAddress,
        boost::beast::http::request<Body, Headers>&& request,
        SocketType&& socket,
        beast::Journal journal);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class Body, class Headers>
PlainWSPeer<Handler>::PlainWSPeer(
    Port const& port,
    Handler& handler,
    EndpointType remoteAddress,
    boost::beast::http::request<Body, Headers>&& request,
    SocketType&& socket,
    beast::Journal journal)
    : BaseWSPeer<Handler, PlainWSPeer>(
          port,
          handler,
          socket.get_executor(),
          WaitableTimer{socket.get_executor()},
          remoteAddress,
          std::move(request),
          journal)
    , ws_(std::move(socket))
{
}

}  // namespace xrpl
