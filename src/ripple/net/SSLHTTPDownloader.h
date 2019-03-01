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
    : public std::enable_shared_from_this <SSLHTTPDownloader>
{
public:
    using error_code = boost::system::error_code;

    SSLHTTPDownloader(
        boost::asio::io_service& io_service,
        beast::Journal j);

    bool
    init(Config const& config);

    bool
    download(
        std::string const& host,
        std::string const& port,
        std::string const& target,
        int version,
        boost::filesystem::path const& dstPath,
        std::function<void(boost::filesystem::path)> complete);

private:
    boost::asio::ssl::context ctx_;
    boost::asio::io_service::strand strand_;
    boost::optional<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> stream_;
    boost::beast::flat_buffer read_buf_;
    bool ssl_verify_;
    beast::Journal j_;

    void
    do_session(
        std::string host,
        std::string port,
        std::string target,
        int version,
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> complete,
        boost::asio::yield_context yield);

    void
    fail(
        boost::filesystem::path dstPath,
        std::function<void(boost::filesystem::path)> const& complete,
        boost::system::error_code const& ec,
        std::string const& errMsg);
};

} // ripple

#endif
