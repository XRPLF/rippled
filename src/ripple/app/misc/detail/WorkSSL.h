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
#include <ripple/net/RegisterSSLCerts.h>
#include <ripple/basics/contract.h>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>

namespace ripple {

namespace detail {

class SSLContext : public boost::asio::ssl::context
{
public:
    SSLContext()
    : boost::asio::ssl::context(boost::asio::ssl::context::sslv23)
    {
        boost::system::error_code ec;
        registerSSLCerts(*this, ec);
        if (ec)
        {
            Throw<std::runtime_error> (
                boost::str (boost::format (
                    "Failed to set_default_verify_paths: %s") %
                    ec.message ()));
        }
    }
};

// Work over SSL
class WorkSSL : public WorkBase<WorkSSL>
    , public std::enable_shared_from_this<WorkSSL>
{
    friend class WorkBase<WorkSSL>;

private:
    using stream_type = boost::asio::ssl::stream<socket_type&>;

    SSLContext context_;
    stream_type stream_;

public:
    WorkSSL(
        std::string const& host,
        std::string const& path, std::string const& port,
        boost::asio::io_service& ios, callback_type cb);
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

    static bool
    rfc2818_verify(
        std::string const& domain,
        bool preverified,
        boost::asio::ssl::verify_context& ctx)
    {
        return boost::asio::ssl::rfc2818_verification(domain)(preverified, ctx);
    }
};

//------------------------------------------------------------------------------

WorkSSL::WorkSSL(
    std::string const& host,
    std::string const& path, std::string const& port,
    boost::asio::io_service& ios, callback_type cb)
    : WorkBase (host, path, port, ios, cb)
    , context_()
    , stream_ (socket_, context_)
{
    // Set SNI hostname
    SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str());
    stream_.set_verify_mode (boost::asio::ssl::verify_peer);
    stream_.set_verify_callback(    std::bind (
            &WorkSSL::rfc2818_verify, host_,
            std::placeholders::_1, std::placeholders::_2));
}

void
WorkSSL::onConnect(error_code const& ec)
{
    if (ec)
        return fail(ec);

    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        strand_.wrap (boost::bind(&WorkSSL::onHandshake, shared_from_this(),
            boost::asio::placeholders::error)));
}

void
WorkSSL::onHandshake(error_code const& ec)
{
    if (ec)
        return fail(ec);

    onStart ();
}

} // detail

} // ripple

#endif
