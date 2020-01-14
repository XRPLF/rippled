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

#ifndef RIPPLE_SERVER_PLAINHTTPPEER_H_INCLUDED
#define RIPPLE_SERVER_PLAINHTTPPEER_H_INCLUDED

#include <ripple/beast/rfc2616.h>
#include <ripple/server/impl/BaseHTTPPeer.h>
#include <ripple/server/impl/PlainWSPeer.h>
#include <boost/beast/core/tcp_stream.hpp>
#include <memory>

namespace ripple {

template<class Handler>
class PlainHTTPPeer
    : public BaseHTTPPeer<Handler, PlainHTTPPeer<Handler>>
    , public std::enable_shared_from_this<PlainHTTPPeer<Handler>>
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
        endpoint_type remote_address,
        ConstBufferSequence const& buffers,
        stream_type&& stream);

    void run();

    std::shared_ptr<WSSession>
    websocketUpgrade() override;

private:
    void
    do_request() override;

    void
    do_close() override;
};

//------------------------------------------------------------------------------

template <class Handler>
template <class ConstBufferSequence>
PlainHTTPPeer<Handler>::PlainHTTPPeer(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    beast::Journal journal,
    endpoint_type remote_endpoint,
    ConstBufferSequence const& buffers,
    stream_type&& stream)
    : BaseHTTPPeer<Handler, PlainHTTPPeer>(
          port,
          handler,
          ioc.get_executor(),
          journal,
          remote_endpoint,
          buffers)
    , stream_(std::move(stream))
    , socket_(stream_.socket())
{
    // Set TCP_NODELAY on loopback interfaces,
    // otherwise Nagle's algorithm makes Env
    // tests run slower on Linux systems.
    //
    if(remote_endpoint.address().is_loopback())
        socket_.set_option(boost::asio::ip::tcp::no_delay{true});
}

template<class Handler>
void
PlainHTTPPeer<Handler>::
run()
{
    if (! this->handler_.onAccept(this->session(), this->remote_address_))
    {
        boost::asio::spawn(this->strand_,
            std::bind (&PlainHTTPPeer::do_close,
                this->shared_from_this()));
        return;
    }

    if (! socket_.is_open())
        return;

    boost::asio::spawn(this->strand_, std::bind(&PlainHTTPPeer::do_read,
        this->shared_from_this(), std::placeholders::_1));
}

template<class Handler>
std::shared_ptr<WSSession>
PlainHTTPPeer<Handler>::
websocketUpgrade()
{
    auto ws = this->ios().template emplace<PlainWSPeer<Handler>>(
        this->port_, this->handler_, this->remote_address_,
            std::move(this->message_), std::move(stream_),
                this->journal_);
    return ws;
}

template<class Handler>
void
PlainHTTPPeer<Handler>::
do_request()
{
    ++this->request_count_;
    auto const what = this->handler_.onHandoff(this->session(),
        std::move(this->message_), this->remote_address_);
    if (what.moved)
        return;
    boost::system::error_code ec;
    if (what.response)
    {
        // half-close on Connection: close
        if (! what.keep_alive)
            socket_.shutdown(socket_type::shutdown_receive, ec);
        if (ec)
            return this->fail(ec, "request");
        return this->write(what.response, what.keep_alive);
    }

    // Perform half-close when Connection: close and not SSL
    if (! beast::rfc2616::is_keep_alive(this->message_))
        socket_.shutdown(socket_type::shutdown_receive, ec);
    if (ec)
        return this->fail(ec, "request");
    // legacy
    this->handler_.onRequest(this->session());
}

template<class Handler>
void
PlainHTTPPeer<Handler>::
do_close()
{
    boost::system::error_code ec;
    socket_.shutdown(socket_type::shutdown_send, ec);
}

} // ripple

#endif
