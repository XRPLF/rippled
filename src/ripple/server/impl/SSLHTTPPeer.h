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

#ifndef RIPPLE_SERVER_SSLHTTPPEER_H_INCLUDED
#define RIPPLE_SERVER_SSLHTTPPEER_H_INCLUDED

#include <ripple/server/impl/BaseHTTPPeer.h>
#include <ripple/server/impl/SSLWSPeer.h>
#include <ripple/beast/asio/ssl_bundle.h>
#include <memory>

namespace ripple {

template<class Handler>
class SSLHTTPPeer
    : public BaseHTTPPeer<Handler, SSLHTTPPeer<Handler>>
    , public std::enable_shared_from_this<SSLHTTPPeer<Handler>>
{
private:
    friend class BaseHTTPPeer<Handler, SSLHTTPPeer>;
    using waitable_timer = typename BaseHTTPPeer<Handler, SSLHTTPPeer>::waitable_timer;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream <socket_type&>;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using yield_context = boost::asio::yield_context;
    using error_code = boost::system::error_code;

    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    stream_type& stream_;

public:
    template <class ConstBufferSequence>
    SSLHTTPPeer(
        Port const& port,
        Handler& handler,
        boost::asio::io_context& ioc,
        beast::Journal journal,
        endpoint_type remote_address,
        ConstBufferSequence const& buffers,
        socket_type&& socket);

    void
    run();

    std::shared_ptr<WSSession>
    websocketUpgrade() override;

private:
    void
    do_handshake(yield_context do_yield);

    void
    do_request() override;

    void
    do_close() override;

    void
    on_shutdown(error_code ec);
};

//------------------------------------------------------------------------------

template <class Handler>
template <class ConstBufferSequence>
SSLHTTPPeer<Handler>::SSLHTTPPeer(
    Port const& port,
    Handler& handler,
    boost::asio::io_context& ioc,
    beast::Journal journal,
    endpoint_type remote_address,
    ConstBufferSequence const& buffers,
    socket_type&& socket)
    : BaseHTTPPeer<Handler, SSLHTTPPeer>(
          port,
          handler,
          ioc.get_executor(),
          waitable_timer{ioc},
          journal,
          remote_address,
          buffers)
    , ssl_bundle_(std::make_unique<beast::asio::ssl_bundle>(
        port.context, std::move(socket)))
    , stream_(ssl_bundle_->stream)
{
}

// Called when the acceptor accepts our socket.
template<class Handler>
void
SSLHTTPPeer<Handler>::
run()
{
    if(! this->handler_.onAccept(this->session(), this->remote_address_))
    {
        boost::asio::spawn(this->strand_,
            std::bind(&SSLHTTPPeer::do_close,
                this->shared_from_this()));
        return;
    }
    if (! stream_.lowest_layer().is_open())
        return;
    boost::asio::spawn(this->strand_, std::bind(
        &SSLHTTPPeer::do_handshake, this->shared_from_this(),
            std::placeholders::_1));
}

template<class Handler>
std::shared_ptr<WSSession>
SSLHTTPPeer<Handler>::
websocketUpgrade()
{
    auto ws = this->ios().template emplace<SSLWSPeer<Handler>>(
        this->port_, this->handler_, this->remote_address_,
            std::move(this->message_), std::move(this->ssl_bundle_),
                this->journal_);
    return ws;
}

template<class Handler>
void
SSLHTTPPeer<Handler>::
do_handshake(yield_context do_yield)
{
    boost::system::error_code ec;
    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    this->start_timer();
    this->read_buf_.consume(stream_.async_handshake(
        stream_type::server, this->read_buf_.data(), do_yield[ec]));
    this->cancel_timer();
    if (ec)
        return this->fail(ec, "handshake");
    bool const http =
        this->port().protocol.count("peer") > 0 ||
        this->port().protocol.count("wss") > 0 ||
        this->port().protocol.count("wss2") > 0 ||
        this->port().protocol.count("https") > 0;
    if(http)
    {
        boost::asio::spawn(this->strand_,
            std::bind(&SSLHTTPPeer::do_read,
                this->shared_from_this(), std::placeholders::_1));
        return;
    }
    // `this` will be destroyed
}

template<class Handler>
void
SSLHTTPPeer<Handler>::
do_request()
{
    ++this->request_count_;
    auto const what = this->handler_.onHandoff(this->session(),
        std::move(ssl_bundle_), std::move(this->message_),
            this->remote_address_);
    if(what.moved)
        return;
    if(what.response)
        return this->write(what.response, what.keep_alive);
    // legacy
    this->handler_.onRequest(this->session());
}

template<class Handler>
void
SSLHTTPPeer<Handler>::
do_close()
{
    this->start_timer();
    stream_.async_shutdown(bind_executor(
        this->strand_,
        std::bind(
            &SSLHTTPPeer::on_shutdown,
            this->shared_from_this(),
            std::placeholders::_1)));
}

template<class Handler>
void
SSLHTTPPeer<Handler>::
on_shutdown(error_code ec)
{
    this->cancel_timer();

    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(this->journal_.debug()) <<
            "on_shutdown: " << ec.message();
    }

    // Close socket now in case this->destructor is delayed
    stream_.lowest_layer().close(ec);
}

} // ripple

#endif
