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
#include <ripple/app/rdb/RelationalDBInterface_shards.h>
#include <ripple/basics/Archive.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/rpc/ShardArchiveHandler.h>
#include <ripple/rpc/impl/Handler.h>

#include <memory>

namespace ripple {
namespace RPC {

using namespace boost::filesystem;
using namespace std::chrono_literals;

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

std::unique_ptr<ShardArchiveHandler>
ShardArchiveHandler::makeShardArchiveHandler(
    Application& app,
    Stoppable& parent)
{
    return std::make_unique<ShardArchiveHandler>(app, parent);
}

std::unique_ptr<ShardArchiveHandler>
ShardArchiveHandler::tryMakeRecoveryHandler(Application& app, Stoppable& parent)
{
    auto const downloadDir(getDownloadDirectory(app.config()));

    // Create the handler iff the database
    // is present.
    if (exists(downloadDir / stateDBName) &&
        is_regular_file(downloadDir / stateDBName))
    {
        return std::make_unique<RecoveryHandler>(app, parent);
    }

    return nullptr;
}

ShardArchiveHandler::ShardArchiveHandler(Application& app, Stoppable& parent)
    : Stoppable("ShardArchiveHandler", parent)
    , process_(false)
    , app_(app)
    , j_(app.journal("ShardArchiveHandler"))
    , downloadDir_(getDownloadDirectory(app.config()))
    , timer_(app_.getIOService())
    , verificationScheduler_(
          std::chrono::seconds(get<std::uint32_t>(
              app.config().section(ConfigSection::shardDatabase()),
              "shard_verification_retry_interval")),

          get<std::uint32_t>(
              app.config().section(ConfigSection::shardDatabase()),
              "shard_verification_max_attempts"))
{
    assert(app_.getShardStore());
}

bool
ShardArchiveHandler::init()
{
    std::lock_guard lock(m_);

    if (process_ || downloader_ != nullptr || sqlDB_ != nullptr)
    {
        JLOG(j_.warn()) << "Archives already being processed";
        return false;
    }

    // Initialize from pre-existing database
    if (exists(downloadDir_ / stateDBName) &&
        is_regular_file(downloadDir_ / stateDBName))
    {
        downloader_ =
            make_DatabaseDownloader(app_.getIOService(), app_.config(), j_);

        return initFromDB(lock);
    }

    // Fresh initialization
    else
    {
        try
        {
            create_directories(downloadDir_);

            sqlDB_ = makeArchiveDB(downloadDir_, stateDBName);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error())
                << "exception: " << e.what() << " in function: " << __func__;

            return false;
        }
    }

    return true;
}

bool
ShardArchiveHandler::initFromDB(std::lock_guard<std::mutex> const& lock)
{
    try
    {
        using namespace boost::filesystem;

        assert(
            exists(downloadDir_ / stateDBName) &&
            is_regular_file(downloadDir_ / stateDBName));

        sqlDB_ = makeArchiveDB(downloadDir_, stateDBName);

        readArchiveDB(*sqlDB_, [&](std::string const& url_, int state) {
            parsedURL url;

            if (!parseUrl(url, url_))
            {
                JLOG(j_.error()) << "Failed to parse url: " << url_;

                return;
            }

            add(state, std::move(url), lock);
        });

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
    {
        std::lock_guard<std::mutex> lock(m_);

        if (downloader_)
        {
            downloader_->onStop();
            downloader_.reset();
        }

        timer_.cancel();
    }

    jobCounter_.join(
        "ShardArchiveHandler", std::chrono::milliseconds(2000), j_);

    timerCounter_.join(
        "ShardArchiveHandler", std::chrono::milliseconds(2000), j_);

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

    insertArchiveDB(*sqlDB_, shardIndex, url.second);

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

    std::vector<std::uint32_t> shardIndexes(archives_.size());
    std::transform(
        archives_.begin(),
        archives_.end(),
        shardIndexes.begin(),
        [](auto const& entry) { return entry.first; });

    if (!app_.getShardStore()->prepareShards(shardIndexes))
        return false;

    try
    {
        // Create temp root download directory
        create_directories(downloadDir_);

        if (!downloader_)
        {
            // will throw if can't initialize ssl context
            downloader_ =
                make_DatabaseDownloader(app_.getIOService(), app_.config(), j_);
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
        return false;
    }

    process_ = true;
    return next(lock);
}

void
ShardArchiveHandler::release()
{
    std::lock_guard<std::mutex> lock(m_);
    doRelease(lock);
}

bool
ShardArchiveHandler::next(std::lock_guard<std::mutex> const& l)
{
    if (isStopping())
        return false;

    if (archives_.empty())
    {
        doRelease(l);
        return false;
    }

    auto const shardIndex{archives_.begin()->first};

    // We use the sequence of the last validated ledger
    // to determine whether or not we have stored a ledger
    // that comes after the last ledger in this shard. A
    // later ledger must be present in order to reliably
    // retrieve the hash of the shard's last ledger.
    std::optional<uint256> expectedHash;
    bool shouldHaveHash = false;
    if (auto const seq = app_.getShardStore()->lastLedgerSeq(shardIndex);
        (shouldHaveHash = app_.getLedgerMaster().getValidLedgerIndex() > seq))
    {
        expectedHash = app_.getLedgerMaster().walkHashBySeq(
            seq, InboundLedger::Reason::GENERIC);
    }

    if (!expectedHash)
    {
        auto wrapper =
            timerCounter_.wrap([this](boost::system::error_code const& ec) {
                if (ec != boost::asio::error::operation_aborted)
                {
                    std::lock_guard lock(m_);
                    this->next(lock);
                }
            });

        if (!wrapper)
            return onClosureFailed(
                "failed to wrap closure for last ledger confirmation timer", l);

        if (!verificationScheduler_.retry(app_, shouldHaveHash, *wrapper))
        {
            JLOG(j_.error()) << "failed to find last ledger hash for shard "
                             << shardIndex << ", maximum attempts reached";

            return removeAndProceed(l);
        }

        return true;
    }

    // Create a temp archive directory at the root
    auto const dstDir{downloadDir_ / std::to_string(shardIndex)};
    try
    {
        create_directory(dstDir);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception: " << e.what();
        return removeAndProceed(l);
    }

    // Download the archive. Process in another thread
    // to prevent holding up the lock if the downloader
    // sleeps.
    auto const& url{archives_.begin()->second};
    auto wrapper = jobCounter_.wrap([this, url, dstDir](Job&) {
        auto const ssl = (url.scheme == "https");
        auto const defaultPort = ssl ? 443 : 80;

        if (!downloader_->download(
                url.domain,
                std::to_string(url.port.value_or(defaultPort)),
                url.path,
                11,
                dstDir / "archive.tar.lz4",
                [this](path dstPath) { complete(dstPath); },
                ssl))
        {
            std::lock_guard<std::mutex> l(m_);
            removeAndProceed(l);
        }
    });

    if (!wrapper)
        return onClosureFailed(
            "failed to wrap closure for starting download", l);

    app_.getJobQueue().addJob(jtCLIENT, "ShardArchiveHandler", *wrapper);

    return true;
}

void
ShardArchiveHandler::complete(path dstPath)
{
    if (isStopping())
        return;

    {
        std::lock_guard lock(m_);
        try
        {
            if (!is_regular_file(dstPath))
            {
                auto ar{archives_.begin()};
                JLOG(j_.error())
                    << "Downloading shard id " << ar->first << " from URL "
                    << ar->second.domain << ar->second.path;
                removeAndProceed(lock);
                return;
            }
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "exception: " << e.what();
            removeAndProceed(lock);
            return;
        }
    }

    // Make lambdas mutable captured vars can be moved from
    auto wrapper =
        jobCounter_.wrap([=, dstPath = std::move(dstPath)](Job&) mutable {
            if (isStopping())
                return;

            // If not synced then defer and retry
            auto const mode{app_.getOPs().getOperatingMode()};
            if (mode != OperatingMode::FULL)
            {
                std::lock_guard lock(m_);
                timer_.expires_from_now(static_cast<std::chrono::seconds>(
                    (static_cast<std::size_t>(OperatingMode::FULL) -
                     static_cast<std::size_t>(mode)) *
                    10));

                auto wrapper = timerCounter_.wrap(
                    [=, dstPath = std::move(dstPath)](
                        boost::system::error_code const& ec) mutable {
                        if (ec != boost::asio::error::operation_aborted)
                            complete(std::move(dstPath));
                    });

                if (!wrapper)
                    onClosureFailed(
                        "failed to wrap closure for operating mode timer",
                        lock);
                else
                    timer_.async_wait(*wrapper);
            }
            else
            {
                process(dstPath);
                std::lock_guard lock(m_);
                removeAndProceed(lock);
            }
        });

    if (!wrapper)
    {
        if (isStopping())
            return;

        JLOG(j_.error()) << "failed to wrap closure for process()";

        std::lock_guard lock(m_);
        removeAndProceed(lock);
    }

    // Process in another thread to not hold up the IO service
    app_.getJobQueue().addJob(jtCLIENT, "ShardArchiveHandler", *wrapper);
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
ShardArchiveHandler::remove(std::lock_guard<std::mutex> const&)
{
    verificationScheduler_.reset();

    auto const shardIndex{archives_.begin()->first};
    app_.getShardStore()->removePreShard(shardIndex);
    archives_.erase(shardIndex);

    deleteFromArchiveDB(*sqlDB_, shardIndex);

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
    timer_.cancel();
    for (auto const& ar : archives_)
        app_.getShardStore()->removePreShard(ar.first);
    archives_.clear();

    dropArchiveDB(*sqlDB_);

    sqlDB_.reset();

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
    process_ = false;
}

bool
ShardArchiveHandler::onClosureFailed(
    std::string const& errorMsg,
    std::lock_guard<std::mutex> const& lock)
{
    if (isStopping())
        return false;

    JLOG(j_.error()) << errorMsg;

    return removeAndProceed(lock);
}

bool
ShardArchiveHandler::removeAndProceed(std::lock_guard<std::mutex> const& lock)
{
    remove(lock);
    return next(lock);
}

RecoveryHandler::RecoveryHandler(Application& app, Stoppable& parent)
    : ShardArchiveHandler(app, parent)
{
}

}  // namespace RPC
}  // namespace ripple
