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

#include <ripple/net/HTTPDownloader.h>
#include <boost/asio/ssl.hpp>

namespace ripple {

HTTPDownloader::HTTPDownloader(
    boost::asio::io_service& io_service,
    Config const& config,
    beast::Journal j)
    : j_(j)
    , config_(config)
    , strand_(io_service)
    , stop_(false)
    , sessionActive_(false)
{
}

bool
HTTPDownloader::download(
    std::string const& host,
    std::string const& port,
    std::string const& target,
    int version,
    boost::filesystem::path const& dstPath,
    std::function<void(boost::filesystem::path)> complete,
    bool ssl)
{
    if (!checkPath(dstPath))
        return false;

    if (stop_)
        return true;

    {
        std::lock_guard lock(m_);
        sessionActive_ = true;
    }

    if (!strand_.running_in_this_thread())
        strand_.post(std::bind(
            &HTTPDownloader::download,
            shared_from_this(),
            host,
            port,
            target,
            version,
            dstPath,
            complete,
            ssl));
    else
        boost::asio::spawn(
            strand_,
            std::bind(
                &HTTPDownloader::do_session,
                shared_from_this(),
                host,
                port,
                target,
                version,
                dstPath,
                complete,
                ssl,
                std::placeholders::_1));
    return true;
}

void
HTTPDownloader::do_session(
    std::string const host,
    std::string const port,
    std::string const target,
    int version,
    boost::filesystem::path dstPath,
    std::function<void(boost::filesystem::path)> complete,
    bool ssl,
    boost::asio::yield_context yield)
{
    using namespace boost::asio;
    using namespace boost::beast;

    boost::system::error_code ec;
    bool skip = false;

    //////////////////////////////////////////////
    // Define lambdas for encapsulating download
    // operations:
    auto close = [&](auto p) {
        closeBody(p);

        // Gracefully close the stream
        stream_->getStream().shutdown(socket_base::shutdown_both, ec);
        if (ec == boost::asio::error::eof)
            ec.assign(0, ec.category());
        if (ec)
        {
            // Most web servers don't bother with performing
            // the SSL shutdown handshake, for speed.
            JLOG(j_.trace()) << "shutdown: " << ec.message();
        }

        // The stream cannot be reused
        stream_.reset();
    };

    // When the downloader is being stopped
    // because the server is shutting down,
    // this method notifies a caller of `onStop`
    // (`RPC::ShardArchiveHandler` to be specific)
    // that the session has ended.
    auto exit = [this, &dstPath, complete] {
        if (!stop_)
            complete(std::move(dstPath));

        std::lock_guard lock(m_);
        sessionActive_ = false;
        c_.notify_one();
    };

    auto failAndExit = [&exit, &dstPath, complete, &ec, this](
                           std::string const& errMsg, auto p) {
        fail(dstPath, ec, errMsg, p);
        exit();
    };
    // end lambdas
    ////////////////////////////////////////////////////////////

    if (stop_.load())
        return exit();

    auto p = this->getParser(dstPath, complete, ec);
    if (ec)
        return failAndExit("getParser", p);

    //////////////////////////////////////////////
    // Prepare for download and establish the
    // connection:
    if (ssl)
        stream_ = std::make_unique<SSLStream>(config_, strand_, j_);
    else
        stream_ = std::make_unique<RawStream>(strand_);

    std::string error;
    if (!stream_->connect(error, host, port, yield))
        return failAndExit(error, p);

    // Set up an HTTP HEAD request message to find the file size
    http::request<http::empty_body> req{http::verb::head, target, version};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    std::uint64_t const rangeStart = size(p);

    // Requesting a portion of the file
    if (rangeStart)
    {
        req.set(
            http::field::range,
            (boost::format("bytes=%llu-") % rangeStart).str());
    }

    stream_->asyncWrite(req, yield, ec);
    if (ec)
        return failAndExit("async_write", p);

    {
        // Read the response
        http::response_parser<http::empty_body> connectParser;
        connectParser.skip(true);
        stream_->asyncRead(read_buf_, connectParser, yield, ec);
        if (ec)
            return failAndExit("async_read", p);

        // Range request was rejected
        if (connectParser.get().result() == http::status::range_not_satisfiable)
        {
            req.erase(http::field::range);

            stream_->asyncWrite(req, yield, ec);
            if (ec)
                return failAndExit("async_write_range_verify", p);

            http::response_parser<http::empty_body> rangeParser;
            rangeParser.skip(true);

            stream_->asyncRead(read_buf_, rangeParser, yield, ec);
            if (ec)
                return failAndExit("async_read_range_verify", p);

            // The entire file is downloaded already.
            if (rangeParser.content_length() == rangeStart)
                skip = true;
            else
                return failAndExit("range_not_satisfiable", p);
        }
        else if (
            rangeStart &&
            connectParser.get().result() != http::status::partial_content)
        {
            ec.assign(
                boost::system::errc::not_supported,
                boost::system::generic_category());

            return failAndExit("Range request ignored", p);
        }
        else if (auto len = connectParser.content_length())
        {
            try
            {
                // Ensure sufficient space is available
                if (*len > space(dstPath.parent_path()).available)
                {
                    return failAndExit(
                        "Insufficient disk space for download", p);
                }
            }
            catch (std::exception const& e)
            {
                return failAndExit(std::string("exception: ") + e.what(), p);
            }
        }
    }

    if (!skip)
    {
        // Set up an HTTP GET request message to download the file
        req.method(http::verb::get);

        if (rangeStart)
        {
            req.set(
                http::field::range,
                (boost::format("bytes=%llu-") % rangeStart).str());
        }
    }

    stream_->asyncWrite(req, yield, ec);
    if (ec)
        return failAndExit("async_write", p);

    // end prepare and connect
    ////////////////////////////////////////////////////////////

    if (skip)
        p->skip(true);

    // Download the file
    while (!p->is_done())
    {
        if (stop_.load())
        {
            close(p);
            return exit();
        }

        stream_->asyncReadSome(read_buf_, *p, yield, ec);
    }

    JLOG(j_.trace()) << "download completed: " << dstPath.string();

    close(p);
    exit();
}

void
HTTPDownloader::onStop()
{
    stop_ = true;

    std::unique_lock lock(m_);
    if (sessionActive_)
    {
        // Wait for the handler to exit.
        c_.wait(lock, [this]() { return !sessionActive_; });
    }
}

void
HTTPDownloader::fail(
    boost::filesystem::path dstPath,
    boost::system::error_code const& ec,
    std::string const& errMsg,
    std::shared_ptr<parser> parser)
{
    if (!ec)
    {
        JLOG(j_.error()) << errMsg;
    }
    else if (ec != boost::asio::error::operation_aborted)
    {
        JLOG(j_.error()) << errMsg << ": " << ec.message();
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
}

}  // namespace ripple
