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

#include <ripple/net/HTTPStream.h>

namespace ripple {

SSLStream::SSLStream(
    Config const& config,
    boost::asio::io_service::strand& strand,
    beast::Journal j)
    : ssl_ctx_(config, j, boost::asio::ssl::context::tlsv12_client)
    , strand_(strand)
{
}

boost::asio::ip::tcp::socket&
SSLStream::getStream()
{
    assert(stream_);
    return stream_->next_layer();
}

bool
SSLStream::connect(
    std::string& errorOut,
    std::string const& host,
    std::string const& port,
    boost::asio::yield_context& yield)
{
    using namespace boost::asio;
    using namespace boost::beast;

    boost::system::error_code ec;

    auto fail = [&errorOut, &ec](
                    std::string const& errorIn,
                    std::string const& message = "") {
        errorOut = errorIn + ": " + (message.empty() ? ec.message() : message);
        return false;
    };

    ip::tcp::resolver resolver{strand_.context()};
    auto const endpoints = resolver.async_resolve(host, port, yield[ec]);
    if (ec)
        return fail("async_resolve");

    try
    {
        stream_.emplace(strand_.context(), ssl_ctx_.context());
    }
    catch (std::exception const& e)
    {
        return fail("exception", e.what());
    }

    ec = ssl_ctx_.preConnectVerify(*stream_, host);
    if (ec)
        return fail("preConnectVerify");

    boost::asio::async_connect(
        stream_->next_layer(), endpoints.begin(), endpoints.end(), yield[ec]);
    if (ec)
        return fail("async_connect");

    ec = ssl_ctx_.postConnectVerify(*stream_, host);
    if (ec)
        return fail("postConnectVerify");

    stream_->async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec)
        return fail("async_handshake");

    return true;
}

void
SSLStream::asyncWrite(
    request& req,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_write(*stream_, req, yield[ec]);
}

void
SSLStream::asyncRead(
    boost::beast::flat_buffer& buf,
    parser& p,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_read(*stream_, buf, p, yield[ec]);
}

void
SSLStream::asyncReadSome(
    boost::beast::flat_buffer& buf,
    parser& p,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_read_some(*stream_, buf, p, yield[ec]);
}

RawStream::RawStream(boost::asio::io_service::strand& strand) : strand_(strand)
{
}

boost::asio::ip::tcp::socket&
RawStream::getStream()
{
    assert(stream_);
    return *stream_;
}

bool
RawStream::connect(
    std::string& errorOut,
    std::string const& host,
    std::string const& port,
    boost::asio::yield_context& yield)
{
    using namespace boost::asio;
    using namespace boost::beast;

    boost::system::error_code ec;

    auto fail = [&errorOut, &ec](
                    std::string const& errorIn,
                    std::string const& message = "") {
        errorOut = errorIn + ": " + (message.empty() ? ec.message() : message);
        return false;
    };

    ip::tcp::resolver resolver{strand_.context()};
    auto const endpoints = resolver.async_resolve(host, port, yield[ec]);
    if (ec)
        return fail("async_resolve");

    try
    {
        stream_.emplace(strand_.context());
    }
    catch (std::exception const& e)
    {
        return fail("exception", e.what());
    }

    boost::asio::async_connect(
        *stream_, endpoints.begin(), endpoints.end(), yield[ec]);
    if (ec)
        return fail("async_connect");

    return true;
}

void
RawStream::asyncWrite(
    request& req,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_write(*stream_, req, yield[ec]);
}

void
RawStream::asyncRead(
    boost::beast::flat_buffer& buf,
    parser& p,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_read(*stream_, buf, p, yield[ec]);
}

void
RawStream::asyncReadSome(
    boost::beast::flat_buffer& buf,
    parser& p,
    boost::asio::yield_context& yield,
    boost::system::error_code& ec)
{
    boost::beast::http::async_read_some(*stream_, buf, p, yield[ec]);
}

}  // namespace ripple
