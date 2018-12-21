//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Archive.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/rpc/ShardArchiveHandler.h>

#include <memory>

namespace ripple {
namespace RPC {

using namespace boost::filesystem;
using namespace std::chrono_literals;

ShardArchiveHandler::ShardArchiveHandler(Application& app, bool validate)
    : app_(app)
    , validate_(validate)
    , downloadDir_(get(app_.config().section(
        ConfigSection::shardDatabase()), "path", "") + "/download")
    , timer_(app_.getIOService())
    , j_(app.journal("ShardArchiveHandler"))
{
    assert(app_.getShardStore());
}

ShardArchiveHandler::~ShardArchiveHandler()
{
    timer_.cancel();
    for (auto const& ar : archives_)
        app_.getShardStore()->removePreShard(ar.first);

    // Remove temp root download directory
    try
    {
        remove_all(downloadDir_);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
    }
}

bool
ShardArchiveHandler::init()
{
    if (!app_.getShardStore())
    {
        JLOG(j_.error()) <<
            "No shard store available";
        return false;
    }
    if (downloader_)
    {
        JLOG(j_.error()) <<
            "Already initialized";
        return false;
    }

    try
    {
        // Remove if remnant from a crash
        remove_all(downloadDir_);

        // Create temp root download directory
        create_directory(downloadDir_);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return false;
    }

    downloader_ = std::make_shared<SSLHTTPDownloader>(
        app_.getIOService(), j_);
    return downloader_->init(app_.config());
}

bool
ShardArchiveHandler::add(std::uint32_t shardIndex, parsedURL&& url)
{
    assert(downloader_);
    auto const it {archives_.find(shardIndex)};
    if (it != archives_.end())
        return url == it->second;
    if (!app_.getShardStore()->prepareShard(shardIndex))
        return false;
    archives_.emplace(shardIndex, std::move(url));
    return true;
}

void
ShardArchiveHandler::next()
{
    assert(downloader_);

    // Check if all archives completed
    if (archives_.empty())
        return;

    // Create a temp archive directory at the root
    auto const dstDir {
        downloadDir_ / std::to_string(archives_.begin()->first)};
    try
    {
        create_directory(dstDir);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        remove(archives_.begin()->first);
        return next();
    }

    // Download the archive
    auto const& url {archives_.begin()->second};
    downloader_->download(
        url.domain,
        std::to_string(url.port.get_value_or(443)),
        url.path,
        11,
        dstDir / "archive.tar.lz4",
        std::bind(&ShardArchiveHandler::complete,
            shared_from_this(), std::placeholders::_1));
}

std::string
ShardArchiveHandler::toString() const
{
    assert(downloader_);
    RangeSet<std::uint32_t> rs;
    for (auto const& ar : archives_)
        rs.insert(ar.first);
    return to_string(rs);
};

void
ShardArchiveHandler::complete(path dstPath)
{
    try
    {
        if (!is_regular_file(dstPath))
        {
            auto ar {archives_.begin()};
            JLOG(j_.error()) <<
                "Downloading shard id " << ar->first <<
                " URL " << ar->second.domain << ar->second.path;
            remove(ar->first);
            return next();
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        remove(archives_.begin()->first);
        return next();
    }

    // Process in another thread to not hold up the IO service
    app_.getJobQueue().addJob(
        jtCLIENT, "ShardArchiveHandler",
        [=, dstPath = std::move(dstPath), ptr = shared_from_this()](Job&)
        {
            // If validating and not synced then defer and retry
            auto const mode {ptr->app_.getOPs().getOperatingMode()};
            if (ptr->validate_ && mode != NetworkOPs::omFULL)
            {
                timer_.expires_from_now(std::chrono::seconds{
                    (NetworkOPs::omFULL - mode) * 10});
                timer_.async_wait(
                    [=, dstPath = std::move(dstPath), ptr = std::move(ptr)]
                    (boost::system::error_code const& ec)
                    {
                        if (ec != boost::asio::error::operation_aborted)
                            ptr->complete(std::move(dstPath));
                    });
                return;
            }
            ptr->process(dstPath);
            ptr->next();
        });
}

void
ShardArchiveHandler::process(path const& dstPath)
{
    auto const shardIndex {archives_.begin()->first};
    auto const shardDir {dstPath.parent_path() / std::to_string(shardIndex)};
    try
    {
        // Decompress and extract the downloaded file
        extractTarLz4(dstPath, dstPath.parent_path());

        // The extracted root directory name must match the shard index
        if (!is_directory(shardDir))
        {
            JLOG(j_.error()) <<
                "Shard " << shardIndex <<
                " mismatches archive shard directory";
            return remove(shardIndex);
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return remove(shardIndex);
    }

    // Import the shard into the shard store
    if (!app_.getShardStore()->importShard(shardIndex, shardDir, validate_))
    {
        JLOG(j_.error()) <<
            "Importing shard " << shardIndex;
    }
    else
    {
        JLOG(j_.debug()) <<
            "Shard " << shardIndex << " downloaded and imported";
    }
    remove(shardIndex);
}

void
ShardArchiveHandler::remove(std::uint32_t shardIndex)
{
    app_.getShardStore()->removePreShard(shardIndex);
    archives_.erase(shardIndex);

    auto const dstDir {downloadDir_ / std::to_string(shardIndex)};
    try
    {
        remove_all(dstDir);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
    }
}

} // RPC
} // ripple
