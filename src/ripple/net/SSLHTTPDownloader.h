//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_NET_SSLHTTPDOWNLOADER_H_INCLUDED
#define RIPPLE_NET_SSLHTTPDOWNLOADER_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/net/HTTPClientSSLContext.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/filesystem.hpp>

#include <memory>

namespace ripple {

/** Provides an asynchronous HTTPS file downloader
 */
class SSLHTTPDownloader
{
public:
    using error_code = boost::system::error_code;

    SSLHTTPDownloader(
        boost::asio::io_service& io_service,
        beast::Journal j,
        Config const& config);

    bool
    download(
        std::string const& host,
        std::string const& port,
        std::string const& target,
        int version,
        boost::filesystem::path const& dstPath,
        std::function<void(boost::filesystem::path)> complete);

    void
    onStop();

    virtual ~SSLHTTPDownloader() = default;

protected:
    using parser = boost::beast::http::basic_parser<false>;

    beast::Journal const j_;

    void
    fail(
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> const& complete,
        boost::system::error_code const& ec,
        std::string const& errMsg,
        std::shared_ptr<parser> parser);

private:
    HTTPClientSSLContext ssl_ctx_;
    boost::asio::io_service::strand strand_;
    boost::optional<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
        stream_;
    boost::beast::flat_buffer read_buf_;
    std::atomic<bool> cancelDownloads_;

    // Used to protect sessionActive_
    std::mutex m_;
    bool sessionActive_;
    std::condition_variable c_;

    void
    do_session(
        std::string host,
        std::string port,
        std::string target,
        int version,
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> complete,
        boost::asio::yield_context yield);

    virtual std::shared_ptr<parser>
    getParser(
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> complete,
        boost::system::error_code& ec) = 0;

    virtual bool
    checkPath(boost::filesystem::path const& dstPath) = 0;

    virtual void
    closeBody(std::shared_ptr<parser> p) = 0;

    virtual uint64_t
    size(std::shared_ptr<parser> p) = 0;
};

}  // namespace ripple

#endif
