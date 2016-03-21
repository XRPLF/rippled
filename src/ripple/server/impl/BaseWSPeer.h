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

#ifndef RIPPLE_SERVER_BASEWSPEER_H_INCLUDED
#define RIPPLE_SERVER_BASEWSPEER_H_INCLUDED

#include <ripple/server/impl/BasePeer.h>
#include <ripple/protocol/BuildInfo.h>
#include <beast/wsproto.h>
#include <beast/asio/streambuf.h>
#include <beast/http/message.h>
#include <cassert>

namespace ripple {

/** Represents an active WebSocket connection. */
template <class Impl>
class BaseWSPeer
    : public BasePeer<Impl>
    , public WSSession
{
protected:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer <clock_type>;
    using BasePeer<Impl>::fail;
    using BasePeer<Impl>::strand_;

private:
    friend class BasePeer<Impl>;

    beast::http::message req_;
    beast::asio::streambuf rb_;
    beast::asio::streambuf wb_;
    beast::wsproto::frame_header fh_;
    std::list<std::shared_ptr<WSMsg>> wq_;

public:
    template<class Message>
    BaseWSPeer(
        Port const& port,
        Handler& handler,
        endpoint_type remote_address,
        Message&& m,
        boost::asio::io_service& io_service,
        beast::Journal journal);

    void
    run();

    //
    // WSSession
    //

    Port const&
    port() const override
    {
        return this->port_;
    }

    boost::asio::ip::tcp::endpoint const&
    remote_endpoint() const override
    {
        return this->remote_address_;
    }

    void
    send(std::shared_ptr<WSMsg> w) override;

protected:
    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void
    on_write_sb(error_code const& ec);

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

    virtual
    void
    do_close() = 0;
};

//------------------------------------------------------------------------------

template<class Impl>
template<class Message>
BaseWSPeer<Impl>::BaseWSPeer(
    Port const& port,
    Handler& handler,
    endpoint_type remote_address,
    Message&& m,
    boost::asio::io_service& io_service,
    beast::Journal journal)
    : BasePeer<Impl>(port, handler, remote_address,
        io_service, journal)
    , req_(std::forward<Message>(m))
{
    impl().ws_.set_option(
        beast::wsproto::frag_size(256));
}

template<class Impl>
void
BaseWSPeer<Impl>::run()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::run, impl().shared_from_this()));
    impl().ws_.decorate(
        [](auto& m)
        {
            m.headers.append("Server",
                BuildInfo::getFullVersionString());
        });
    using namespace beast::asio;
    impl().ws_.async_accept_request(req_, strand_.wrap(std::bind(
        &BaseWSPeer::on_write_sb, impl().shared_from_this(),
            placeholders::error)));
}

template<class Impl>
void
BaseWSPeer<Impl>::send(std::shared_ptr<WSMsg> w)
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::send, impl().shared_from_this(),
                std::move(w)));
    wq_.emplace_back(std::move(w));
    if(wq_.size() == 1)
        on_write({});
}

template<class Impl>
void
BaseWSPeer<Impl>::on_write_sb(error_code const& ec)
{
    if(ec)
        return fail(ec, "write_resp");
    do_read();
}

template<class Impl>
void
BaseWSPeer<Impl>::do_write()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::do_write, impl().shared_from_this()));
    on_write({});
}

template<class Impl>
void
BaseWSPeer<Impl>::on_write(error_code const& ec)
{
    if(ec)
        return fail(ec, "write");
    auto& w = *wq_.front();
    using namespace beast::asio;
    auto const result = w.prepare(65536,
        std::bind(&BaseWSPeer::do_write,
            impl().shared_from_this()));
    if(boost::indeterminate(result.first))
        return;
    if(! result.first)
        impl().ws_.async_write(result.first, result.second,
            strand_.wrap(std::bind(&BaseWSPeer::on_write,
                impl().shared_from_this(), placeholders::error)));
    else
        impl().ws_.async_write(result.first, result.second,
            strand_.wrap(std::bind(&BaseWSPeer::on_write_fin,
                impl().shared_from_this(), placeholders::error)));
}

template<class Impl>
void
BaseWSPeer<Impl>::on_write_fin(error_code const& ec)
{
    if(ec)
        return fail(ec, "write_fin");
    wq_.pop_front();
    if(! wq_.empty())
        on_write({});
}

template<class Impl>
void
BaseWSPeer<Impl>::do_read()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &BaseWSPeer::do_read, impl().shared_from_this()));
    using namespace beast::asio;
    beast::wsproto::async_read_msg(impl().ws_, rb_,
        strand_.wrap(std::bind(&BaseWSPeer::on_read,
            impl().shared_from_this(), placeholders::error)));
}

template<class Impl>
void
BaseWSPeer<Impl>::on_read(error_code const& ec)
{
    if(ec == beast::wsproto::error::closed)
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
    do_read();
}

} // ripple

#endif
