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
#include <beast/websocket.hpp>
#include <beast/core/streambuf.hpp>
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
    using BasePeer<Handler, Impl>::fail;
    using BasePeer<Handler, Impl>::strand_;

    enum
    {
        // Max seconds without signs of line
        timeoutSeconds = 30
    };

private:
    friend class BasePeer<Handler, Impl>;

    http_request_type request_;
    beast::websocket::opcode op_;
    beast::streambuf rb_;
    beast::streambuf wb_;
    std::list<std::shared_ptr<WSMsg>> wq_;
    bool do_close_ = false;
    beast::websocket::close_reason cr_;
    waitable_timer timer_;
    int timer_action_ = 0; // 0 = normal, 1 = ping sent, 2 = send ping

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
    struct identity
    {
        template<class Body, class Headers>
        void
        operator()(beast::http::message<true, Body, Headers>& req) const
        {
            req.fields.replace("User-Agent",
                BuildInfo::getFullVersionString());
        }

        template<class Body, class Headers>
        void
        operator()(beast::http::message<false, Body, Headers>& resp) const
        {
            resp.fields.replace("Server",
                BuildInfo::getFullVersionString());
        }
    };

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

    virtual
    void
    do_close() = 0;

    void
    start_timer();

    void
    cancel_timer();

    void
    on_timer(error_code ec);
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
    impl().ws_.set_option(beast::websocket::decorate(identity{}));
    impl().ws_.set_option(port().pmd_options);
    using namespace beast::asio;
    timer_action_ = 1; // close connection if the timer expires
    start_timer();
    impl().ws_.async_accept(request_, strand_.wrap(std::bind(
        &BaseWSPeer::on_ws_handshake, impl().shared_from_this(),
            placeholders::error)));
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
send(std::shared_ptr<WSMsg> w)
{
    // Maximum send queue size
    static std::size_t constexpr limit = 100;
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::send, impl().shared_from_this(),
                std::move(w)));
    if(wq_.size() >= limit)
    {
        cr_.code = static_cast<beast::websocket::close_code::value>(4000);
        cr_.reason = "Client is too slow.";
        do_close_ = true;
        wq_.erase(std::next(wq_.begin()), wq_.end());
        return;
    }
    wq_.emplace_back(std::move(w));
    if(wq_.size() == 1 && timer_action_ != 1)
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
    if(wq_.size() > 0)
        do_close_ = true;
    else
        impl().ws_.async_close({}, strand_.wrap(std::bind(
            &BaseWSPeer::on_close, impl().shared_from_this(),
                beast::asio::placeholders::error)));
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
    timer_action_ = 0;
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
    cancel_timer();
    if(ec)
        return fail(ec, "write");
    auto& w = *wq_.front();
    using namespace beast::asio;
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
                    placeholders::error)));
    else
        impl().ws_.async_write_frame(
            result.first, result.second, strand_.wrap(std::bind(
                &BaseWSPeer::on_write_fin, impl().shared_from_this(),
                    placeholders::error)));
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
                beast::asio::placeholders::error)));
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
    using namespace beast::asio;
    impl().ws_.async_read(op_, rb_, strand_.wrap(
        std::bind(&BaseWSPeer::on_read,
            impl().shared_from_this(), placeholders::error)));
    cancel_timer();
}

template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_read(error_code const& ec)
{
    if(ec == beast::websocket::error::closed)
        return do_close();
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
    // great
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
            beast::asio::placeholders::error)));
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

// Called when session times out
template<class Handler, class Impl>
void
BaseWSPeer<Handler, Impl>::
on_timer(error_code ec)
{
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(! ec)
        ec = boost::system::errc::make_error_code(
            boost::system::errc::timed_out);
    fail(ec, "timer");
}
} // ripple

#endif
