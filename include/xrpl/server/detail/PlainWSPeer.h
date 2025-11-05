#ifndef XRPL_SERVER_PLAINWSPEER_H_INCLUDED
#define XRPL_SERVER_PLAINWSPEER_H_INCLUDED

#include <xrpl/server/detail/BaseWSPeer.h>

#include <boost/beast/core/tcp_stream.hpp>

#include <memory>

namespace ripple {

template <class Handler>
class PlainWSPeer : public BaseWSPeer<Handler, PlainWSPeer<Handler>>,
                    public std::enable_shared_from_this<PlainWSPeer<Handler>>
{
    friend class BasePeer<Handler, PlainWSPeer>;
    friend class BaseWSPeer<Handler, PlainWSPeer>;

    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer<clock_type>;
    using socket_type = boost::beast::tcp_stream;

    boost::beast::websocket::stream<socket_type> ws_;

public:
    template <class Body, class Headers>
    PlainWSPeer(
        Port const& port,
        Handler& handler,
        endpoint_type remote_address,
        boost::beast::http::request<Body, Headers>&& request,
        socket_type&& socket,
        beast::Journal journal);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class Body, class Headers>
PlainWSPeer<Handler>::PlainWSPeer(
    Port const& port,
    Handler& handler,
    endpoint_type remote_address,
    boost::beast::http::request<Body, Headers>&& request,
    socket_type&& socket,
    beast::Journal journal)
    : BaseWSPeer<Handler, PlainWSPeer>(
          port,
          handler,
          socket.get_executor(),
          waitable_timer{socket.get_executor()},
          remote_address,
          std::move(request),
          journal)
    , ws_(std::move(socket))
{
}

}  // namespace ripple

#endif
