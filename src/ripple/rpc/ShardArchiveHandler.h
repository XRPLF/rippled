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
#include <ripple/core/DatabaseCon.h>
#include <ripple/net/DatabaseDownloader.h>
#include <ripple/rpc/ShardVerificationScheduler.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/filesystem.hpp>

namespace ripple {
namespace test {
class ShardArchiveHandler_test;
}
namespace RPC {

/** Handles the download and import of one or more shard archives. */
class ShardArchiveHandler
{
public:
    using TimerOpCounter =
        ClosureCounter<void, boost::system::error_code const&>;
    friend class test::ShardArchiveHandler_test;

    static boost::filesystem::path
    getDownloadDirectory(Config const& config);

    static std::unique_ptr<ShardArchiveHandler>
    makeShardArchiveHandler(Application& app);

    // Create a ShardArchiveHandler only if
    // the state database is present, indicating
    // that recovery is needed.
    static std::unique_ptr<ShardArchiveHandler>
    tryMakeRecoveryHandler(Application& app);

    ShardArchiveHandler(Application& app);

    virtual ~ShardArchiveHandler() = default;

    [[nodiscard]] bool
    init();

    bool
    add(std::uint32_t shardIndex, std::pair<parsedURL, std::string>&& url);

    /** Starts downloading and importing archives. */
    bool
    start();

    void
    stop();

    void
    release();

private:
    ShardArchiveHandler() = delete;
    ShardArchiveHandler(ShardArchiveHandler const&) = delete;
    ShardArchiveHandler&
    operator=(ShardArchiveHandler&&) = delete;
    ShardArchiveHandler&
    operator=(ShardArchiveHandler const&) = delete;

    [[nodiscard]] bool
    initFromDB(std::lock_guard<std::mutex> const&);

    /** Add an archive to be downloaded and imported.
        @param shardIndex the index of the shard to be imported.
        @param url the location of the archive.
        @return `true` if successfully added.
        @note Returns false if called while downloading.
    */
    bool
    add(std::uint32_t shardIndex,
        parsedURL&& url,
        std::lock_guard<std::mutex> const&);

    // Begins the download and import of the next archive.
    bool
    next(std::lock_guard<std::mutex> const& l);

    // Callback used by the downloader to notify completion of a download.
    void
    complete(boost::filesystem::path dstPath);

    // Extract a downloaded archive and import it into the shard store.
    void
    process(boost::filesystem::path const& dstPath);

    // Remove the archive being processed.
    void
    remove(std::lock_guard<std::mutex> const&);

    void
    doRelease(std::lock_guard<std::mutex> const&);

    bool
    onClosureFailed(
        std::string const& errorMsg,
        std::lock_guard<std::mutex> const& lock);

    bool
    removeAndProceed(std::lock_guard<std::mutex> const& lock);

    /////////////////////////////////////////////////
    // m_ is used to protect access to downloader_,
    // archives_, process_ and to protect setting and
    // destroying sqlDB_.
    /////////////////////////////////////////////////
    std::mutex mutable m_;
    std::atomic_bool stopping_{false};
    std::shared_ptr<DatabaseDownloader> downloader_;
    std::map<std::uint32_t, parsedURL> archives_;
    bool process_;
    std::unique_ptr<DatabaseCon> sqlDB_;
    /////////////////////////////////////////////////

    Application& app_;
    beast::Journal const j_;
    boost::filesystem::path const downloadDir_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    JobCounter jobCounter_;
    TimerOpCounter timerCounter_;
    ShardVerificationScheduler verificationScheduler_;
};

////////////////////////////////////////////////////////////////////
// The RecoveryHandler is an empty class that is constructed by
// the application when the ShardArchiveHandler's state database
// is present at application start, indicating that the handler
// needs to perform recovery. However, if recovery isn't needed
// at application start, and the user subsequently submits a request
// to download shards, we construct a ShardArchiveHandler rather
// than a RecoveryHandler to process the request. With this approach,
// type verification can be employed to determine whether the
// ShardArchiveHandler was constructed in recovery mode by the
// application, or as a response to a user submitting a request to
// download shards.
////////////////////////////////////////////////////////////////////
class RecoveryHandler : public ShardArchiveHandler
{
public:
    RecoveryHandler(Application& app);
};

}  // namespace RPC
}  // namespace ripple

#endif
