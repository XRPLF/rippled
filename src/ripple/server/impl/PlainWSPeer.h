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

#ifndef RIPPLE_SERVER_PLAINWSPEER_H_INCLUDED
#define RIPPLE_SERVER_PLAINWSPEER_H_INCLUDED

#include <ripple/server/impl/BaseWSPeer.h>
#include <memory>

namespace ripple {

template<class Handler>
class PlainWSPeer
    : public BaseWSPeer<Handler, PlainWSPeer<Handler>>
    , public std::enable_shared_from_this<PlainWSPeer<Handler>>
{
private:
    friend class BasePeer<Handler, PlainWSPeer>;
    friend class BaseWSPeer<Handler, PlainWSPeer>;

    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer <clock_type>;
    using socket_type = boost::asio::ip::tcp::socket;

    beast::websocket::stream<socket_type> ws_;

public:
    template<class Body, class Headers>
    PlainWSPeer(
        Port const& port,
        Handler& handler,
        endpoint_type remote_address,
        beast::http::request<Body, Headers>&& request,
        socket_type&& socket,
        beast::Journal journal);

private:
    void
    do_close() override;
};

//------------------------------------------------------------------------------

template<class Handler>
template<class Body, class Headers>
PlainWSPeer<Handler>::
PlainWSPeer(
    Port const& port,
    Handler& handler,
    endpoint_type remote_address,
    beast::http::request<Body, Headers>&& request,
    socket_type&& socket,
    beast::Journal journal)
    : BaseWSPeer<Handler, PlainWSPeer>(port, handler, remote_address,
        std::move(request), socket.get_io_service(), journal)
    , ws_(std::move(socket))
{
}

template<class Handler>
void
PlainWSPeer<Handler>::
do_close()
{
    error_code ec;
    auto& sock = ws_.next_layer();
    sock.shutdown(socket_type::shutdown_both, ec);
    if(ec)
        return this->fail(ec, "do_close");
}

} // ripple

#endif
