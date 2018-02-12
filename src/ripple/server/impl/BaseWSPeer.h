//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright(c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_BASEWSPEER_H_INCLUDED
#define RIPPLE_SERVER_BASEWSPEER_H_INCLUDED

#include <ripple/server/impl/BasePeer.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/beast/utility/rngfill.h>
#include <ripple/crypto/csprng.h>
#include <beast/websocket.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/http/message.hpp>
#include <cassert>

namespace ripple {

/** Represents an active WebSocket connection. */
template<class Handler, class Impl>
class BaseWSPeer
    : public BasePeer<Handler, Impl>
    , public WSSession
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer <clock_type>;
    using BasePeer<Handler, Impl>::strand_;

private:
    friend class BasePeer<Handler, Impl>;

    http_request_type request_;
    beast::multi_buffer rb_;
    beast::multi_buffer wb_;
    std::list<std::shared_ptr<WSMsg>> wq_;
    bool do_close_ = false;
    beast::websocket::close_reason cr_;
    waitable_timer timer_;
    bool close_on_timer_ = false;
    bool ping_active_ = false;
    beast::websocket::ping_data payload_;
    error_code ec_;

public:
    template<class Body, class Headers>
    BaseWSPeer(
        Port const& port,
        Handler& handler,
        endpoint_type remote_address,
        beast::http::request<Body, Headers>&& request,
        boost::asio::io_service& io_service,
        beast::Journal journal);

    void
    run() override;

    //
    // WSSession
    //

    Port const&
    port() const override
    {
        return this->port_;
    }

    http_request_type const&
    request() const override
    {
        return this->request_;
    }

    boost::asio::ip::tcp::endpoint const&
    remote_endpoint() const override
    {
        return this->remote_address_;
    }

    void
    send(std::shared_ptr<WSMsg> w) override;

    void
    close() override;

    void
    complete() override;

protected:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    on_ws_handshake(error_code const& ec);

    void
    do_write();

    void
    on_write(error_code const& ec);

    void
    on_write_fin(error_code const& ec);

    void
    do_read();

    void
    on_read(error_code const& ec);

    void
    on_close(error_code const& ec);

    void
    start_timer();

    void
    cancel_timer();

    void
    on_ping(error_code const& ec);

    void
    on_ping_pong(beast::websocket::frame_type kind,
        beast::string_view payload);

    void
    on_timer(error_code ec);

    template<class String>
    void
    fail(error_code ec, String const& what);
};

//------------------------------------------------------------------------------

template<class Handler, class Impl>
template<class Body, class Headers>
BaseWSPeer<Handler, Impl>::
BaseWSPeer(
    Port const& port,
    Handler& handler,
    endpoint_type remote_address,
    beast::http::request<Body, Headers>&& request,
    boost::asio::io_service& io_service,
    beast::Journal journal)
    : BasePeer<Handler, Impl>(port, handler, remote_address,
        io_service, journal)
    , request_(std::move(request))
    , timer_(io_service)
{
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
run()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::run, impl().shared_from_this()));
    impl().ws_.set_option(port().pmd_options);
    impl().ws_.control_callback(
        std::bind(&BaseWSPeer::on_ping_pong, this,
            std::placeholders::_1, std::placeholders::_2));
    start_timer();
    close_on_timer_ = true;
    impl().ws_.async_accept_ex(request_,
        [](auto & res)
        {
            res.set(beast::http::field::server,
                BuildInfo::getFullVersionString());
        },
        strand_.wrap(std::bind(&BaseWSPeer::on_ws_handshake,
            impl().shared_from_this(), std::placeholders::_1)));
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
send(std::shared_ptr<WSMsg> w)
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::send, impl().shared_from_this(),
                std::move(w)));
    if(do_close_)
        return;
    if(wq_.size() > port().ws_queue_limit)
    {
        JLOG(this->j_.info()) <<
            "closing slow client";
        cr_.code = static_cast<beast::websocket::close_code>(4000);
        cr_.reason = "Client is too slow.";
        wq_.erase(std::next(wq_.begin()), wq_.end());
        close();
        return;
    }
    wq_.emplace_back(std::move(w));
    if(wq_.size() == 1)
        on_write({});
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
close()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::close, impl().shared_from_this()));
    do_close_ = true;
    if(wq_.empty())
        impl().ws_.async_close({}, strand_.wrap(std::bind(
            &BaseWSPeer::on_close, impl().shared_from_this(),
                std::placeholders::_1)));
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
complete()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::complete, impl().shared_from_this()));
    do_read();
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_ws_handshake(error_code const& ec)
{
    if(ec)
        return fail(ec, "on_ws_handshake");
    close_on_timer_ = false;
    do_read();
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
do_write()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::do_write, impl().shared_from_this()));
    on_write({});
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_write(error_code const& ec)
{
    if(ec)
        return fail(ec, "write");
    auto& w = *wq_.front();
    auto const result = w.prepare(65536,
        std::bind(&BaseWSPeer::do_write,
            impl().shared_from_this()));
    if(boost::indeterminate(result.first))
        return;
    start_timer();
    if(! result.first)
        impl().ws_.async_write_frame(
            result.first, result.second, strand_.wrap(std::bind(
                &BaseWSPeer::on_write, impl().shared_from_this(),
                    std::placeholders::_1)));
    else
        impl().ws_.async_write_frame(
            result.first, result.second, strand_.wrap(std::bind(
                &BaseWSPeer::on_write_fin, impl().shared_from_this(),
                    std::placeholders::_1)));
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_write_fin(error_code const& ec)
{
    if(ec)
        return fail(ec, "write_fin");
    wq_.pop_front();
    if(do_close_)
        impl().ws_.async_close(cr_, strand_.wrap(std::bind(
            &BaseWSPeer::on_close, impl().shared_from_this(),
                std::placeholders::_1)));
    else if(! wq_.empty())
        on_write({});
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
do_read()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::do_read, impl().shared_from_this()));
    impl().ws_.async_read(rb_, strand_.wrap(
        std::bind(&BaseWSPeer::on_read,
            impl().shared_from_this(), std::placeholders::_1)));
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_read(error_code const& ec)
{
    if(ec == beast::websocket::error::closed)
        return on_close({});
    if(ec)
        return fail(ec, "read");
    auto const& data = rb_.data();
    std::vector<boost::asio::const_buffer> b;
    b.reserve(std::distance(data.begin(), data.end()));
    std::copy(data.begin(), data.end(),
        std::back_inserter(b));
    this->handler_.onWSMessage(impl().shared_from_this(), b);
    rb_.consume(rb_.size());
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_close(error_code const& ec)
{
    cancel_timer();
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
start_timer()
{
    // Max seconds without completing a message
    static constexpr std::chrono::seconds timeout{30};
    static constexpr std::chrono::seconds timeoutLocal{3};
    error_code ec;
    timer_.expires_from_now(
        remote_endpoint().address().is_loopback() ? timeoutLocal : timeout,
        ec);
    if(ec)
        return fail(ec, "start_timer");
    timer_.async_wait(strand_.wrap(std::bind(
        &BaseWSPeer<Handler, Impl>::on_timer, impl().shared_from_this(),
            std::placeholders::_1)));
}

// Convenience for discarding the error code
template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
cancel_timer()
{
    error_code ec;
    timer_.cancel(ec);
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_ping(error_code const& ec)
{
    if(ec == boost::asio::error::operation_aborted)
        return;
    ping_active_ = false;
    if(! ec)
        return;
    fail(ec, "on_ping");
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_ping_pong(beast::websocket::frame_type kind,
    beast::string_view payload)
{
    if(kind == beast::websocket::frame_type::pong)
    {
        beast::string_view p(payload_.begin());
        if(payload == p)
        {
            close_on_timer_ = false;
            JLOG(this->j_.trace()) <<
                "got matching pong";
        }
        else
        {
            JLOG(this->j_.trace()) <<
                "got pong";
        }
    }
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_timer(error_code ec)
{
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(! ec)
    {
        if(! close_on_timer_ || ! ping_active_)
        {
            start_timer();
            close_on_timer_ = true;
            ping_active_ = true;
            // cryptographic is probably overkill..
            beast::rngfill(payload_.begin(),
                payload_.size(), crypto_prng());
            impl().ws_.async_ping(payload_,
                strand_.wrap(std::bind(
                    &BaseWSPeer::on_ping,
                        impl().shared_from_this(),
                            std::placeholders::_1)));
            JLOG(this->j_.trace()) <<
                "sent ping";
            return;
        }
        ec = boost::system::errc::make_error_code(
            boost::system::errc::timed_out);
    }
    fail(ec, "timer");
}

template<class Handler, class Impl>
template<class String>
void
BaseWSPeer<Handler, Impl>::
fail(error_code ec, String const& what)
{
    assert(strand_.running_in_this_thread());

    cancel_timer();
    if(! ec_ &&
        ec != boost::asio::error::operation_aborted)
    {
        ec_ = ec;
        JLOG(this->j_.trace()) <<
            what << ": " << ec.message();
        impl().ws_.lowest_layer().close(ec);
    }
}

} // ripple

#endif
