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
#include <ripple/basics/BasicConfig.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/ShardArchiveHandler.h>
#include <ripple/rpc/impl/Handler.h>

#include <memory>

namespace ripple {
namespace RPC {

using namespace boost::filesystem;
using namespace std::chrono_literals;

std::mutex ShardArchiveHandler::instance_mutex_;
ShardArchiveHandler::pointer ShardArchiveHandler::instance_ = nullptr;

boost::filesystem::path
ShardArchiveHandler::getDownloadDirectory(Config const& config)
{
    return get(config.section(ConfigSection::shardDatabase()),
               "download_path",
               get(config.section(ConfigSection::shardDatabase()),
                   "path",
                   "")) /
        "download";
}

auto
ShardArchiveHandler::getInstance() -> pointer
{
    std::lock_guard lock(instance_mutex_);

    return instance_;
}

auto
ShardArchiveHandler::getInstance(Application& app, Stoppable& parent) -> pointer
{
    std::lock_guard lock(instance_mutex_);
    assert(!instance_);

    instance_.reset(new ShardArchiveHandler(app, parent));

    return instance_;
}

auto
ShardArchiveHandler::recoverInstance(Application& app, Stoppable& parent)
    -> pointer
{
    std::lock_guard lock(instance_mutex_);
    assert(!instance_);

    instance_.reset(new ShardArchiveHandler(app, parent, true));

    return instance_;
}

bool
ShardArchiveHandler::hasInstance()
{
    std::lock_guard lock(instance_mutex_);

    return instance_.get() != nullptr;
}

ShardArchiveHandler::ShardArchiveHandler(
    Application& app,
    Stoppable& parent,
    bool recovery)
    : Stoppable("ShardArchiveHandler", parent)
    , app_(app)
    , j_(app.journal("ShardArchiveHandler"))
    , downloadDir_(getDownloadDirectory(app.config()))
    , timer_(app_.getIOService())
    , process_(false)
{
    assert(app_.getShardStore());

    if (recovery)
        downloader_.reset(
            new DatabaseDownloader(app_.getIOService(), j_, app_.config()));
}

bool
ShardArchiveHandler::init()
{
    try
    {
        create_directories(downloadDir_);

        sqliteDB_ = std::make_unique<DatabaseCon>(
            downloadDir_,
            stateDBName,
            DownloaderDBPragma,
            ShardArchiveHandlerDBInit);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what()
                         << " in function: " << __func__;

        return false;
    }

    return true;
}

bool
ShardArchiveHandler::initFromDB()
{
    try
    {
        using namespace boost::filesystem;

        assert(
            exists(downloadDir_ / stateDBName) &&
            is_regular_file(downloadDir_ / stateDBName));

        sqliteDB_ = std::make_unique<DatabaseCon>(
            downloadDir_,
            stateDBName,
            DownloaderDBPragma,
            ShardArchiveHandlerDBInit);

        auto& session{sqliteDB_->getSession()};

        soci::rowset<soci::row> rs =
            (session.prepare << "SELECT * FROM State;");

        std::lock_guard<std::mutex> lock(m_);

        for (auto it = rs.begin(); it != rs.end(); ++it)
        {
            parsedURL url;

            if (!parseUrl(url, it->get<std::string>(1)))
            {
                JLOG(j_.error())
                    << "Failed to parse url: " << it->get<std::string>(1);

                continue;
            }

            add(it->get<int>(0), std::move(url), lock);
        }

        // Failed to load anything
        // from the state database.
        if (archives_.empty())
        {
            release();
            return false;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what()
                         << " in function: " << __func__;

        return false;
    }

    return true;
}

void
ShardArchiveHandler::onStop()
{
    std::lock_guard<std::mutex> lock(m_);

    if (downloader_)
    {
        downloader_->onStop();
        downloader_.reset();
    }

    stopped();
}

bool
ShardArchiveHandler::add(
    std::uint32_t shardIndex,
    std::pair<parsedURL, std::string>&& url)
{
    std::lock_guard<std::mutex> lock(m_);

    if (!add(shardIndex, std::forward<parsedURL>(url.first), lock))
        return false;

    auto& session{sqliteDB_->getSession()};

    session << "INSERT INTO State VALUES (:index, :url);",
        soci::use(shardIndex), soci::use(url.second);

    return true;
}

bool
ShardArchiveHandler::add(
    std::uint32_t shardIndex,
    parsedURL&& url,
    std::lock_guard<std::mutex> const&)
{
    if (process_)
    {
        JLOG(j_.error()) << "Download and import already in progress";
        return false;
    }

    auto const it{archives_.find(shardIndex)};
    if (it != archives_.end())
        return url == it->second;

    if (!app_.getShardStore()->prepareShard(shardIndex))
        return false;

    archives_.emplace(shardIndex, std::move(url));

    return true;
}

bool
ShardArchiveHandler::start()
{
    std::lock_guard lock(m_);
    if (!app_.getShardStore())
    {
        JLOG(j_.error()) << "No shard store available";
        return false;
    }
    if (process_)
    {
        JLOG(j_.warn()) << "Archives already being processed";
        return false;
    }
    if (archives_.empty())
    {
        JLOG(j_.warn()) << "No archives to process";
        return false;
    }

    try
    {
        // Create temp root download directory
        create_directories(downloadDir_);

        if (!downloader_)
        {
            // will throw if can't initialize ssl context
            downloader_ = std::make_shared<DatabaseDownloader>(
                app_.getIOService(), j_, app_.config());
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
        return false;
    }

    return next(lock);
}

void
ShardArchiveHandler::release()
{
    std::lock_guard<std::mutex> lock(m_);
    doRelease(lock);
}

bool
ShardArchiveHandler::next(std::lock_guard<std::mutex>& l)
{
    if (archives_.empty())
    {
        doRelease(l);
        return false;
    }

    // Create a temp archive directory at the root
    auto const shardIndex{archives_.begin()->first};
    auto const dstDir{downloadDir_ / std::to_string(shardIndex)};
    try
    {
        create_directory(dstDir);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
        remove(l);
        return next(l);
    }

    // Download the archive. Process in another thread
    // to prevent holding up the lock if the downloader
    // sleeps.
    auto const& url{archives_.begin()->second};
    app_.getJobQueue().addJob(
        jtCLIENT,
        "ShardArchiveHandler",
        [this, ptr = shared_from_this(), url, dstDir](Job&) {
            if (!downloader_->download(
                    url.domain,
                    std::to_string(url.port.get_value_or(443)),
                    url.path,
                    11,
                    dstDir / "archive.tar.lz4",
                    std::bind(
                        &ShardArchiveHandler::complete,
                        ptr,
                        std::placeholders::_1)))
            {
                std::lock_guard<std::mutex> l(m_);
                remove(l);
                next(l);
            }
        });

    process_ = true;
    return true;
}

void
ShardArchiveHandler::complete(path dstPath)
{
    {
        std::lock_guard lock(m_);
        try
        {
            if (!is_regular_file(dstPath))
            {
                auto ar{archives_.begin()};
                JLOG(j_.error())
                    << "Downloading shard id " << ar->first << " form URL "
                    << ar->second.domain << ar->second.path;
                remove(lock);
                next(lock);
                return;
            }
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "exception: " << e.what();
            remove(lock);
            next(lock);
            return;
        }
    }

    // Process in another thread to not hold up the IO service
    // Make lambdas mutable so can move from the captured variables
    app_.getJobQueue().addJob(
        jtCLIENT,
        "ShardArchiveHandler",
        [=, dstPath = std::move(dstPath), ptr = shared_from_this()](
            Job&) mutable {
            // If not synced then defer and retry
            auto const mode{ptr->app_.getOPs().getOperatingMode()};
            if (mode != OperatingMode::FULL)
            {
                std::lock_guard lock(m_);
                timer_.expires_from_now(static_cast<std::chrono::seconds>(
                    (static_cast<std::size_t>(OperatingMode::FULL) -
                     static_cast<std::size_t>(mode)) *
                    10));
                timer_.async_wait(
                    [=, dstPath = std::move(dstPath), ptr = std::move(ptr)](
                        boost::system::error_code const& ec) mutable {
                        if (ec != boost::asio::error::operation_aborted)
                            ptr->complete(std::move(dstPath));
                    });
            }
            else
            {
                ptr->process(dstPath);
                std::lock_guard lock(m_);
                remove(lock);
                next(lock);
            }
        });
}

void
ShardArchiveHandler::process(path const& dstPath)
{
    std::uint32_t shardIndex;
    {
        std::lock_guard lock(m_);
        shardIndex = archives_.begin()->first;
    }

    auto const shardDir{dstPath.parent_path() / std::to_string(shardIndex)};
    try
    {
        // Extract the downloaded archive
        extractTarLz4(dstPath, dstPath.parent_path());

        // The extracted root directory name must match the shard index
        if (!is_directory(shardDir))
        {
            JLOG(j_.error()) << "Shard " << shardIndex
                             << " mismatches archive shard directory";
            return;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
        return;
    }

    // Import the shard into the shard store
    if (!app_.getShardStore()->importShard(shardIndex, shardDir))
    {
        JLOG(j_.error()) << "Importing shard " << shardIndex;
        return;
    }

    JLOG(j_.debug()) << "Shard " << shardIndex << " downloaded and imported";
}

void
ShardArchiveHandler::remove(std::lock_guard<std::mutex>&)
{
    auto const shardIndex{archives_.begin()->first};
    app_.getShardStore()->removePreShard(shardIndex);
    archives_.erase(shardIndex);

    auto& session{sqliteDB_->getSession()};

    session << "DELETE FROM State WHERE ShardIndex = :index;",
        soci::use(shardIndex);

    auto const dstDir{downloadDir_ / std::to_string(shardIndex)};
    try
    {
        remove_all(dstDir);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
    }
}

void
ShardArchiveHandler::doRelease(std::lock_guard<std::mutex> const&)
{
    process_ = false;

    timer_.cancel();
    for (auto const& ar : archives_)
        app_.getShardStore()->removePreShard(ar.first);
    archives_.clear();

    {
        auto& session{sqliteDB_->getSession()};

        session << "DROP TABLE State;";
    }

    sqliteDB_.reset();

    // Remove temp root download directory
    try
    {
        remove_all(downloadDir_);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what()
                         << " in function: " << __func__;
    }

    downloader_.reset();
}

}  // namespace RPC
}  // namespace ripple
