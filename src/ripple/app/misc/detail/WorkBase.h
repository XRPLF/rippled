//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_DETAIL_WORKBASE_H_INCLUDED
#define RIPPLE_APP_MISC_DETAIL_WORKBASE_H_INCLUDED

#include <ripple/app/misc/detail/Work.h>
#include <ripple/protocol/BuildInfo.h>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/asio.hpp>

namespace ripple {

namespace detail {

template <class Impl>
class WorkBase
    : public Work
{
protected:
    using error_code = boost::system::error_code;

public:
    using callback_type =
        std::function<void(error_code const&, response_type&&)>;
protected:
    using socket_type = boost::asio::ip::tcp::socket;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using resolver_type = boost::asio::ip::tcp::resolver;
    using query_type = resolver_type::query;
    using request_type =
        boost::beast::http::request<boost::beast::http::empty_body>;

    std::string host_;
    std::string path_;
    std::string port_;
    callback_type cb_;
    boost::asio::io_service& ios_;
    boost::asio::io_service::strand strand_;
    resolver_type resolver_;
    socket_type socket_;
    request_type req_;
    response_type res_;
    boost::beast::multi_buffer read_buf_;

public:
    WorkBase(
        std::string const& host, std::string const& path,
        std::string const& port,
        boost::asio::io_service& ios, callback_type cb);
    ~WorkBase();

    Impl&
    impl()
    {
        return *static_cast<Impl*>(this);
    }

    void run() override;

    void cancel() override;

    void
    fail(error_code const& ec);

    void
    onResolve(error_code const& ec, resolver_type::iterator it);

    void
    onStart();

    void
    onRequest(error_code const& ec);

    void
    onResponse(error_code const& ec);
};

//------------------------------------------------------------------------------

template<class Impl>
WorkBase<Impl>::WorkBase(std::string const& host,
    std::string const& path, std::string const& port,
    boost::asio::io_service& ios, callback_type cb)
    : host_(host)
    , path_(path)
    , port_(port)
    , cb_(std::move(cb))
    , ios_(ios)
    , strand_(ios)
    , resolver_(ios)
    , socket_(ios)
{
}

template<class Impl>
WorkBase<Impl>::~WorkBase()
{
    if (cb_)
        cb_ (make_error_code(boost::system::errc::not_a_socket),
            std::move(res_));
}

template<class Impl>
void
WorkBase<Impl>::run()
{
    if (! strand_.running_in_this_thread())
        return ios_.post(strand_.wrap (std::bind(
            &WorkBase::run, impl().shared_from_this())));

    resolver_.async_resolve(
        query_type{host_, port_},
        strand_.wrap (std::bind(&WorkBase::onResolve, impl().shared_from_this(),
            std::placeholders::_1,
                std::placeholders::_2)));
}

template<class Impl>
void
WorkBase<Impl>::cancel()
{
    if (! strand_.running_in_this_thread())
    {
        return ios_.post(strand_.wrap (std::bind(
            &WorkBase::cancel, impl().shared_from_this())));
    }

    error_code ec;
    resolver_.cancel();
    socket_.cancel (ec);
}

template<class Impl>
void
WorkBase<Impl>::fail(error_code const& ec)
{
    if (cb_)
    {
        cb_(ec, std::move(res_));
        cb_ = nullptr;
    }
}

template<class Impl>
void
WorkBase<Impl>::onResolve(error_code const& ec, resolver_type::iterator it)
{
    if (ec)
        return fail(ec);

    socket_.async_connect(*it,
        strand_.wrap (std::bind(&Impl::onConnect, impl().shared_from_this(),
            std::placeholders::_1)));
}

template<class Impl>
void
WorkBase<Impl>::onStart()
{
    req_.method(boost::beast::http::verb::get);
    req_.target(path_.empty() ? "/" : path_);
    req_.version(11);
    req_.set (
        "Host", host_ + ":" + port_);
    req_.set ("User-Agent", BuildInfo::getFullVersionString());
    req_.prepare_payload();
    boost::beast::http::async_write(impl().stream(), req_,
        strand_.wrap (std::bind (&WorkBase::onRequest,
            impl().shared_from_this(), std::placeholders::_1)));
}

template<class Impl>
void
WorkBase<Impl>::onRequest(error_code const& ec)
{
    if (ec)
        return fail(ec);

    boost::beast::http::async_read (impl().stream(), read_buf_, res_,
        strand_.wrap (std::bind (&WorkBase::onResponse,
            impl().shared_from_this(), std::placeholders::_1)));
}

template<class Impl>
void
WorkBase<Impl>::onResponse(error_code const& ec)
{
    if (ec)
        return fail(ec);

    assert(cb_);
    cb_(ec, std::move(res_));
    cb_ = nullptr;
}

} // detail

} // ripple

#endif
