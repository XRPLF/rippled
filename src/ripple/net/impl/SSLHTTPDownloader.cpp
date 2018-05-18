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

#include <ripple/net/SSLHTTPDownloader.h>
#include <ripple/net/RegisterSSLCerts.h>

namespace ripple {

SSLHTTPDownloader::SSLHTTPDownloader(
    boost::asio::io_service& io_service,
    beast::Journal j)
    : ctx_(boost::asio::ssl::context::sslv23_client)
    , strand_(io_service)
    , stream_(io_service, ctx_)
    , j_(j)
{
}

bool
SSLHTTPDownloader::init(Config const& config)
{
    boost::system::error_code ec;
    if (config.SSL_VERIFY_FILE.empty())
    {
        registerSSLCerts(ctx_, ec, j_);
        if (ec && config.SSL_VERIFY_DIR.empty())
        {
            JLOG(j_.error()) <<
                "Failed to set_default_verify_paths: " <<
                ec.message();
            return false;
        }
    }
    else
        ctx_.load_verify_file(config.SSL_VERIFY_FILE);

    if (!config.SSL_VERIFY_DIR.empty())
    {
        ctx_.add_verify_path(config.SSL_VERIFY_DIR, ec);
        if (ec)
        {
            JLOG(j_.error()) <<
                "Failed to add verify path: " <<
                ec.message();
            return false;
        }
    }
    return true;
}

bool
SSLHTTPDownloader::download(
    std::string const& host,
    std::string const& port,
    std::string const& target,
    int version,
    boost::filesystem::path const& dstPath,
    std::function<void(boost::filesystem::path)> complete)
{
    try
    {
        if (exists(dstPath))
        {
            JLOG(j_.error()) <<
                "Destination file exists";
            return false;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return false;
    }

    if (!strand_.running_in_this_thread())
        strand_.post(
            std::bind(
                &SSLHTTPDownloader::download,
                this->shared_from_this(),
                host,
                port,
                target,
                version,
                dstPath,
                complete));
    else
        boost::asio::spawn(
            strand_,
            std::bind(
                &SSLHTTPDownloader::do_session,
                this->shared_from_this(),
                host,
                port,
                target,
                version,
                dstPath,
                complete,
                std::placeholders::_1));
    return true;
}

void
SSLHTTPDownloader::do_session(
    std::string const host,
    std::string const port,
    std::string const target,
    int version,
    boost::filesystem::path dstPath,
    std::function<void(boost::filesystem::path)> complete,
    boost::asio::yield_context yield)
{
    using namespace boost::asio;
    using namespace boost::beast;

    boost::system::error_code ec;
    auto fail = [&](std::string errMsg)
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            JLOG(j_.error()) <<
                errMsg << ": " << ec.message();
        }
        try
        {
            remove(dstPath);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) <<
                "exception: " << e.what();
        }
        complete(std::move(dstPath));
    };

    if (!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str()))
    {
        ec.assign(static_cast<int>(
            ::ERR_get_error()), boost::asio::error::get_ssl_category());
        return fail("SSL_set_tlsext_host_name");
    }

    ip::tcp::resolver resolver {stream_.get_io_context()};
    auto const results = resolver.async_resolve(host, port, yield[ec]);
    if (ec)
        return fail("async_resolve");

    boost::asio::async_connect(
        stream_.next_layer(), results.begin(), results.end(), yield[ec]);
    if (ec)
        return fail("async_connect");

    stream_.async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec)
        return fail("async_handshake");

    // Set up an HTTP HEAD request message to find the file size
    http::request<http::empty_body> req {http::verb::head, target, version};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    http::async_write(stream_, req, yield[ec]);
    if(ec)
        return fail("async_write");

    {
        // Check if available storage for file size
        http::response_parser<http::empty_body> p;
        p.skip(true);
        http::async_read(stream_, read_buf_, p, yield[ec]);
        if(ec)
            return fail("async_read");
        if (auto len = p.content_length())
        {
            try
            {
                if (*len > space(dstPath.parent_path()).available)
                    return fail("Insufficient disk space for download");
            }
            catch (std::exception const& e)
            {
                JLOG(j_.error()) <<
                    "exception: " << e.what();
                return fail({});
            }
        }
    }

    // Set up an HTTP GET request message to download the file
    req.method(http::verb::get);
    http::async_write(stream_, req, yield[ec]);
    if(ec)
        return fail("async_write");

    // Download the file
    http::response_parser<http::file_body> p;
    p.body_limit(std::numeric_limits<std::uint64_t>::max());
    p.get().body().open(
        dstPath.string().c_str(),
        boost::beast::file_mode::write,
        ec);
    if (ec)
    {
        p.get().body().close();
        return fail("open");
    }

    http::async_read(stream_, read_buf_, p, yield[ec]);
    if (ec)
    {
        p.get().body().close();
        return fail("async_read");
    }
    p.get().body().close();

    // Gracefully close the stream
    stream_.async_shutdown(yield[ec]);
    if (ec == boost::asio::error::eof)
        ec.assign(0, ec.category());
    if (ec)
    {
        // Most web servers don't bother with performing
        // the SSL shutdown handshake, for speed.
        JLOG(j_.trace()) <<
            "async_shutdown: " << ec.message();
    }

    JLOG(j_.trace()) <<
        "download completed: " << dstPath.string();

    // Notify the completion handler
    complete(std::move(dstPath));
}

}// ripple
