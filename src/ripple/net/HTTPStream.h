//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_NET_HTTPSTREAM_H_INCLUDED
#define RIPPLE_NET_HTTPSTREAM_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/net/HTTPClientSSLContext.h>

#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace ripple {

class HTTPStream
{
public:
    using request = boost::beast::http::request<boost::beast::http::empty_body>;
    using parser = boost::beast::http::basic_parser<false>;

    virtual ~HTTPStream() = default;

    [[nodiscard]] virtual boost::asio::ip::tcp::socket&
    getStream() = 0;

    [[nodiscard]] virtual bool
    connect(
        std::string& errorOut,
        std::string const& host,
        std::string const& port,
        boost::asio::yield_context& yield) = 0;

    virtual void
    asyncWrite(
        request& req,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) = 0;

    virtual void
    asyncRead(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) = 0;

    virtual void
    asyncReadSome(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) = 0;
};

class SSLStream : public HTTPStream
{
public:
    SSLStream(
        Config const& config,
        boost::asio::io_service::strand& strand,
        beast::Journal j);

    virtual ~SSLStream() = default;

    boost::asio::ip::tcp::socket&
    getStream() override;

    bool
    connect(
        std::string& errorOut,
        std::string const& host,
        std::string const& port,
        boost::asio::yield_context& yield) override;

    void
    asyncWrite(
        request& req,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

    void
    asyncRead(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

    void
    asyncReadSome(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

private:
    HTTPClientSSLContext ssl_ctx_;
    boost::optional<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
        stream_;
    boost::asio::io_service::strand& strand_;
};

class RawStream : public HTTPStream
{
public:
    RawStream(boost::asio::io_service::strand& strand);

    virtual ~RawStream() = default;

    boost::asio::ip::tcp::socket&
    getStream() override;

    bool
    connect(
        std::string& errorOut,
        std::string const& host,
        std::string const& port,
        boost::asio::yield_context& yield) override;

    void
    asyncWrite(
        request& req,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

    void
    asyncRead(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

    void
    asyncReadSome(
        boost::beast::flat_buffer& buf,
        parser& p,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec) override;

private:
    boost::optional<boost::asio::ip::tcp::socket> stream_;
    boost::asio::io_service::strand& strand_;
};

}  // namespace ripple

#endif  // RIPPLE_NET_HTTPSTREAM_H
