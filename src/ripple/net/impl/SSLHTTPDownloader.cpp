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
#include <boost/asio/ssl.hpp>

namespace ripple {

SSLHTTPDownloader::SSLHTTPDownloader(
    boost::asio::io_service& io_service,
    beast::Journal j,
    Config const& config,
    bool isPaused)
    : j_(j)
    , ssl_ctx_(config, j, boost::asio::ssl::context::tlsv12_client)
    , strand_(io_service)
    , isStopped_(false)
    , sessionActive_(false)
{
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
    if (!checkPath(dstPath))
        return false;

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
SSLHTTPDownloader::onStop()
{
    std::unique_lock lock(m_);

    isStopped_ = true;

    if(sessionActive_)
    {
        // Wait for the handler to exit.
        c_.wait(lock,
            [this]()
            {
                return !sessionActive_;
            });
    }
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
    bool skip = false;

    //////////////////////////////////////////////
    // Define lambdas for encapsulating download
    // operations:
    auto connect = [&](std::shared_ptr<parser> parser)
    {
        uint64_t const rangeStart = size(parser);

        ip::tcp::resolver resolver {strand_.context()};
        auto const results = resolver.async_resolve(host, port, yield[ec]);
        if (ec)
            return fail(dstPath, complete, ec, "async_resolve", parser);

        try
        {
            stream_.emplace(strand_.context(), ssl_ctx_.context());
        }
        catch (std::exception const& e)
        {
            return fail(dstPath, complete, ec,
                        std::string("exception: ") + e.what(), parser);
        }

        ec = ssl_ctx_.preConnectVerify(*stream_, host);
        if (ec)
            return fail(dstPath, complete, ec, "preConnectVerify", parser);

        boost::asio::async_connect(
            stream_->next_layer(), results.begin(), results.end(), yield[ec]);
        if (ec)
            return fail(dstPath, complete, ec, "async_connect", parser);

        ec = ssl_ctx_.postConnectVerify(*stream_, host);
        if (ec)
            return fail(dstPath, complete, ec, "postConnectVerify", parser);

        stream_->async_handshake(ssl::stream_base::client, yield[ec]);
        if (ec)
            return fail(dstPath, complete, ec, "async_handshake", parser);

        // Set up an HTTP HEAD request message to find the file size
        http::request<http::empty_body> req {http::verb::head, target, version};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        // Requesting a portion of the file
        if (rangeStart)
        {
            req.set(http::field::range,
                (boost::format("bytes=%llu-") % rangeStart).str());
        }

        http::async_write(*stream_, req, yield[ec]);
        if(ec)
            return fail(dstPath, complete, ec, "async_write", parser);

        {
            // Check if available storage for file size
            http::response_parser<http::empty_body> p;
            p.skip(true);
            http::async_read(*stream_, read_buf_, p, yield[ec]);
            if(ec)
                return fail(dstPath, complete, ec, "async_read", parser);

            // Range request was rejected
            if(p.get().result() == http::status::range_not_satisfiable)
            {
                req.erase(http::field::range);

                http::async_write(*stream_, req, yield[ec]);
                if(ec)
                    return fail(dstPath, complete, ec,
                        "async_write_range_verify", parser);

                http::response_parser<http::empty_body> p;
                p.skip(true);

                http::async_read(*stream_, read_buf_, p, yield[ec]);
                if(ec)
                    return fail(dstPath, complete, ec,
                        "async_read_range_verify", parser);

                // The entire file is downloaded already.
                if(p.content_length() == rangeStart)
                    skip = true;
                else
                    return fail(dstPath, complete, ec,
                        "range_not_satisfiable", parser);
            }
            else if (rangeStart &&
                p.get().result() != http::status::partial_content)
            {
                ec.assign(boost::system::errc::not_supported,
                    boost::system::generic_category());

                return fail(dstPath, complete, ec,
                    "Range request ignored", parser);
            }
            else if (auto len = p.content_length())
            {
                try
                {
                    if (*len > space(dstPath.parent_path()).available)
                    {
                        return fail(dstPath, complete, ec,
                            "Insufficient disk space for download", parser);
                    }
                }
                catch (std::exception const& e)
                {
                    return fail(dstPath, complete, ec,
                        std::string("exception: ") + e.what(), parser);
                }
            }
        }

        if(!skip)
        {
            // Set up an HTTP GET request message to download the file
            req.method(http::verb::get);

            if (rangeStart)
            {
                req.set(http::field::range,
                    (boost::format("bytes=%llu-") % rangeStart).str());
            }
        }

        http::async_write(*stream_, req, yield[ec]);
        if(ec)
            return fail(dstPath, complete, ec, "async_write", parser);

        return true;
    };

    auto close = [&](auto p)
    {
        closeBody(p);

        // Gracefully close the stream
        stream_->async_shutdown(yield[ec]);
        if (ec == boost::asio::error::eof)
            ec.assign(0, ec.category());
        if (ec)
        {
            // Most web servers don't bother with performing
            // the SSL shutdown handshake, for speed.
            JLOG(j_.trace()) <<
                             "async_shutdown: " << ec.message();
        }
        // The socket cannot be reused
        stream_ = boost::none;
    };

    auto getParser = [&]
    {
        auto p = this->getParser(dstPath, complete, ec);
        if (ec)
            fail(dstPath, complete, ec, "getParser", p);

        return p;
    };

    // When the downloader is being stopped
    // because the server is shutting down,
    // this method notifies a 'Stoppable'
    // object that the session has ended.
    auto exit = [this]()
    {
        std::lock_guard<std::mutex> lock(m_);
        sessionActive_ = false;
        c_.notify_one();
    };

    // end lambdas
    ////////////////////////////////////////////////////////////

    {
        std::lock_guard<std::mutex> lock(m_);
        sessionActive_ = true;
    }

    if(isStopped_.load())
        return exit();

    auto p = getParser();
    if (ec)
        return exit();

    if (!connect(p) || ec)
        return exit();

    if(skip)
        p->skip(true);

    // Download the file
    while (!p->is_done())
    {
        if(isStopped_.load())
        {
            close(p);
            return exit();
        }

        http::async_read_some(*stream_, read_buf_, *p, yield[ec]);
    }

    JLOG(j_.trace()) <<
        "download completed: " << dstPath.string();

    close(p);
    exit();

    // Notify the completion handler
    complete(std::move(dstPath));
}

bool
SSLHTTPDownloader::fail(
    boost::filesystem::path dstPath,
    std::function<void(boost::filesystem::path)> const& complete,
    boost::system::error_code const& ec,
    std::string const& errMsg,
    std::shared_ptr<parser> parser)
{
    if (!ec)
    {
        JLOG(j_.error()) <<
            errMsg;
    }
    else if (ec != boost::asio::error::operation_aborted)
    {
        JLOG(j_.error()) <<
            errMsg << ": " << ec.message();
    }

    if (parser)
        closeBody(parser);

    try
    {
        remove(dstPath);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what()
            << " in function: " << __func__;
    }
    complete(std::move(dstPath));

    return false;
}

}// ripple
