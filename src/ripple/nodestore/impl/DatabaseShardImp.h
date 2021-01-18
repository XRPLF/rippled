//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_DATABASESHARDIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASESHARDIMP_H_INCLUDED

#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/impl/Shard.h>
#include <ripple/nodestore/impl/TaskQueue.h>

#include <boost/asio/basic_waitable_timer.hpp>

namespace ripple {
namespace NodeStore {

class DatabaseShardImp : public DatabaseShard
{
public:
    DatabaseShardImp() = delete;
    DatabaseShardImp(DatabaseShardImp const&) = delete;
    DatabaseShardImp(DatabaseShardImp&&) = delete;
    DatabaseShardImp&
    operator=(DatabaseShardImp const&) = delete;
    DatabaseShardImp&
    operator=(DatabaseShardImp&&) = delete;

    DatabaseShardImp(
        Application& app,
        Scheduler& scheduler,
        int readThreads,
        beast::Journal j);

    [[nodiscard]] bool
    init() override;

    [[nodiscard]] boost::optional<std::uint32_t>
    prepareLedger(std::uint32_t validLedgerSeq) override;

    bool
    prepareShards(std::vector<std::uint32_t> const& shardIndexes) override;

    void
    removePreShard(std::uint32_t shardIndex) override;

    std::string
    getPreShards() override;

    bool
    importShard(std::uint32_t shardIndex, boost::filesystem::path const& srcDir)
        override;

    std::shared_ptr<Ledger>
    fetchLedger(uint256 const& hash, std::uint32_t ledgerSeq) override;

    void
    setStored(std::shared_ptr<Ledger const> const& ledger) override;

    std::string
    getCompleteShards() override;

    std::uint32_t
    ledgersPerShard() const override
    {
        return ledgersPerShard_;
    }

    std::uint32_t
    earliestShardIndex() const override
    {
        return earliestShardIndex_;
    }

    std::uint32_t
    seqToShardIndex(std::uint32_t ledgerSeq) const override
    {
        assert(ledgerSeq >= earliestLedgerSeq());
        return NodeStore::seqToShardIndex(ledgerSeq, ledgersPerShard_);
    }

    std::uint32_t
    firstLedgerSeq(std::uint32_t shardIndex) const override
    {
        assert(shardIndex >= earliestShardIndex_);
        if (shardIndex <= earliestShardIndex_)
            return earliestLedgerSeq();
        return 1 + (shardIndex * ledgersPerShard_);
    }

    std::uint32_t
    lastLedgerSeq(std::uint32_t shardIndex) const override
    {
        assert(shardIndex >= earliestShardIndex_);
        return (shardIndex + 1) * ledgersPerShard_;
    }

    boost::filesystem::path const&
    getRootDir() const override
    {
        return dir_;
    }

    std::string
    getName() const override
    {
        return backendName_;
    }

    void
    stop() override;

    /** Import the application local node store

        @param source The application node store.
    */
    void
    import(Database& source) override;

    std::int32_t
    getWriteLoad() const override;

    bool
    isSameDB(std::uint32_t s1, std::uint32_t s2) override
    {
        return seqToShardIndex(s1) == seqToShardIndex(s2);
    }

    void
    store(
        NodeObjectType type,
        Blob&& data,
        uint256 const& hash,
        std::uint32_t ledgerSeq) override;

    void
    sync() override{};

    bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) override;

    void
    sweep() override;

    Backend&
    getBackend() override;

private:
    enum class PathDesignation : uint8_t {
        none,       // No path specified
        historical  // Needs a historical path
    };

    Application& app_;
    mutable std::mutex mutex_;
    bool init_{false};

    // The context shared with all shard backend databases
    std::unique_ptr<nudb::context> ctx_;

    // Queue of background tasks to be performed
    TaskQueue taskQueue_;

    // Shards held by this server
    std::unordered_map<std::uint32_t, std::shared_ptr<Shard>> shards_;

    // Shard indexes being imported
    std::set<std::uint32_t> preparedIndexes_;

    // Shard index being acquired from the peer network
    std::uint32_t acquireIndex_{0};

    // The shard store root directory
    boost::filesystem::path dir_;

    // If new shards can be stored
    bool canAdd_{true};

    // Complete shard indexes
    std::string status_;

    // The name associated with the backend used with the shard store
    std::string backendName_;

    // Maximum number of historical shards to store.
    std::uint32_t maxHistoricalShards_{0};

    // Contains historical shard paths
    std::vector<boost::filesystem::path> historicalPaths_;

    // Storage space utilized by the shard store (in bytes)
    std::uint64_t fileSz_{0};

    // Each shard stores 16384 ledgers. The earliest shard may store
    // less if the earliest ledger sequence truncates its beginning.
    // The value should only be altered for unit tests.
    std::uint32_t ledgersPerShard_ = ledgersPerShardDefault;

    // The earliest shard index
    std::uint32_t earliestShardIndex_;

    // Average storage space required by a shard (in bytes)
    std::uint64_t avgShardFileSz_;

    // The limit of final shards with open databases at any time
    std::uint32_t const openFinalLimit_;

    // File name used to mark shards being imported from node store
    static constexpr auto importMarker_ = "import";

    // latestShardIndex_ and secondLatestShardIndex hold the indexes
    // of the shards most recently confirmed by the network. These
    // values are not updated in real time and are modified only
    // when adding shards to the database, in order to determine where
    // pending shards will be stored on the filesystem. A value of
    // boost::none indicates that the corresponding shard is not held
    // by the database.
    boost::optional<std::uint32_t> latestShardIndex_;
    boost::optional<std::uint32_t> secondLatestShardIndex_;

    // Initialize settings from the configuration file
    // Lock must be held
    bool
    initConfig(std::lock_guard<std::mutex> const&);

    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        FetchReport& fetchReport) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        Throw<std::runtime_error>("Shard store import not supported");
    }

    // Randomly select a shard index not stored
    // Lock must be held
    boost::optional<std::uint32_t>
    findAcquireIndex(
        std::uint32_t validLedgerSeq,
        std::lock_guard<std::mutex> const&);

    // Queue a task to finalize a shard by verifying its databases
    // Lock must be held
    void
    finalizeShard(
        std::shared_ptr<Shard>& shard,
        bool writeSQLite,
        boost::optional<uint256> const& expectedHash);

    // Set storage and file descriptor usage stats
    void
    setFileStats();

    // Update status string
    // Lock must be held
    void
    updateStatus(std::lock_guard<std::mutex> const&);

    // Returns true if the filesystem has enough storage
    // available to hold the specified number of shards.
    // The value of pathDesignation determines whether
    // the shard(s) in question are historical and thus
    // meant to be stored at a path designated for historical
    // shards.
    bool
    sufficientStorage(
        std::uint32_t numShards,
        PathDesignation pathDesignation,
        std::lock_guard<std::mutex> const&) const;

    bool
    setStoredInShard(
        std::shared_ptr<Shard>& shard,
        std::shared_ptr<Ledger const> const& ledger);

    void
    removeFailedShard(std::shared_ptr<Shard>& shard);

    // Returns the index that represents the logical
    // partition between historical and recent shards
    std::uint32_t
    shardBoundaryIndex() const;

    std::uint32_t
    numHistoricalShards(std::lock_guard<std::mutex> const& lock) const;

    // Shifts the recent and second most recent (by index)
    // shards as new shards become available on the network.
    // Older shards are moved to a historical shard path.
    void
    relocateOutdatedShards(std::lock_guard<std::mutex> const& lock);

    // Checks whether the shard can be stored. If
    // the new shard can't be stored, returns
    // boost::none. Otherwise returns an enum
    // indicating whether the new shard should be
    // placed in a separate directory for historical
    // shards.
    boost::optional<PathDesignation>
    prepareForNewShard(
        std::uint32_t shardIndex,
        std::uint32_t numHistoricalShards,
        std::lock_guard<std::mutex> const& lock);

    boost::filesystem::path
    chooseHistoricalPath(std::lock_guard<std::mutex> const&) const;

    bool
    checkHistoricalPaths() const;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
