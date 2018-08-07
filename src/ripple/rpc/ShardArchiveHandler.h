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

#ifndef RIPPLE_RPC_SHARDARCHIVEHANDLER_H_INCLUDED
#define RIPPLE_RPC_SHARDARCHIVEHANDLER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/net/SSLHTTPDownloader.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/filesystem.hpp>

namespace ripple {
namespace RPC {

/** Handles the download and import one or more shard archives. */
class ShardArchiveHandler
    : public std::enable_shared_from_this <ShardArchiveHandler>
{
public:
    ShardArchiveHandler() = delete;
    ShardArchiveHandler(ShardArchiveHandler const&) = delete;
    ShardArchiveHandler& operator= (ShardArchiveHandler&&) = delete;
    ShardArchiveHandler& operator= (ShardArchiveHandler const&) = delete;

    /** @param validate if shard data should be verified with network. */
    ShardArchiveHandler(Application& app, bool validate);

    ~ShardArchiveHandler();

    /** Initializes the handler.
        @return `true` if successfully initialized.
    */
    bool
    init();

    /** Queue an archive to be downloaded and imported.
        @param shardIndex the index of the shard to be imported.
        @param url the location of the archive.
        @return `true` if successfully added.
    */
    bool
    add(std::uint32_t shardIndex, parsedURL&& url);

    /** Starts downloading and importing of queued archives. */
    void
    next();

    /** Returns indexes of queued archives.
        @return indexes of queued archives.
    */
    std::string
    toString() const;

private:
    // The callback used by the downloader to notify completion of a download.
    void
    complete(boost::filesystem::path dstPath);

    // A job to extract an archive and import a shard.
    void
    process(boost::filesystem::path const& dstPath);

    void
    remove(std::uint32_t shardIndex);

    Application& app_;
    std::shared_ptr<SSLHTTPDownloader> downloader_;
    std::map<std::uint32_t, parsedURL> archives_;
    bool const validate_;
    boost::filesystem::path const downloadDir_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    beast::Journal j_;
};

} // RPC
} // ripple

#endif
