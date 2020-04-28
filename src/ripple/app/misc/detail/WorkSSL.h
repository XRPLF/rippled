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

#ifndef RIPPLE_APP_MISC_DETAIL_WORKSSL_H_INCLUDED
#define RIPPLE_APP_MISC_DETAIL_WORKSSL_H_INCLUDED

#include <ripple/app/misc/detail/WorkBase.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/net/HTTPClientSSLContext.h>
#include <boost/asio/ssl.hpp>
#include <boost/format.hpp>

#include <functional>

namespace ripple {

namespace detail {

// Work over SSL
class WorkSSL : public WorkBase<WorkSSL>,
                public std::enable_shared_from_this<WorkSSL>
{
    friend class WorkBase<WorkSSL>;

private:
    using stream_type = boost::asio::ssl::stream<socket_type&>;

    HTTPClientSSLContext context_;
    stream_type stream_;

public:
    WorkSSL(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_service& ios,
        beast::Journal j,
        Config const& config,
        callback_type cb);
    ~WorkSSL() = default;

private:
    stream_type&
    stream()
    {
        return stream_;
    }

    void
    onConnect(error_code const& ec);

    void
    onHandshake(error_code const& ec);
};

//------------------------------------------------------------------------------

WorkSSL::WorkSSL(
    std::string const& host,
    std::string const& path,
    std::string const& port,
    boost::asio::io_service& ios,
    beast::Journal j,
    Config const& config,
    callback_type cb)
    : WorkBase(host, path, port, ios, cb)
    , context_(config, j, boost::asio::ssl::context::tlsv12_client)
    , stream_(socket_, context_.context())
{
    auto ec = context_.preConnectVerify(stream_, host_);
    if (ec)
        Throw<std::runtime_error>(
            boost::str(boost::format("preConnectVerify: %s") % ec.message()));
}

void
WorkSSL::onConnect(error_code const& ec)
{
    auto err = ec ? ec : context_.postConnectVerify(stream_, host_);
    if (err)
        return fail(err);

    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        strand_.wrap(std::bind(
            &WorkSSL::onHandshake, shared_from_this(), std::placeholders::_1)));
}

void
WorkSSL::onHandshake(error_code const& ec)
{
    if (ec)
        return fail(ec);

    onStart();
}

}  // namespace detail

}  // namespace ripple

#endif
