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

namespace ripple {
namespace NodeStore {

class DatabaseShardImp : public DatabaseShard
{
public:
    DatabaseShardImp() = delete;
    DatabaseShardImp(DatabaseShardImp const&) = delete;
    DatabaseShardImp(DatabaseShardImp&&) = delete;
    DatabaseShardImp& operator=(DatabaseShardImp const&) = delete;
    DatabaseShardImp& operator=(DatabaseShardImp&&) = delete;

    DatabaseShardImp(
        Application& app,
        Stoppable& parent,
        std::string const& name,
        Scheduler& scheduler,
        int readThreads,
        beast::Journal j);

    ~DatabaseShardImp() override;

    bool
    init() override;

    boost::optional<std::uint32_t>
    prepareLedger(std::uint32_t validLedgerSeq) override;

    bool
    prepareShard(std::uint32_t shardIndex) override;

    void
    removePreShard(std::uint32_t shardIndex) override;

    std::string
    getPreShards() override;

    bool
    importShard(std::uint32_t shardIndex,
        boost::filesystem::path const& srcDir, bool validate) override;

    std::shared_ptr<Ledger>
    fetchLedger(uint256 const& hash, std::uint32_t seq) override;

    void
    setStored(std::shared_ptr<Ledger const> const& ledger) override;

    bool
    contains(std::uint32_t seq) override;

    std::string
    getCompleteShards() override;

    void
    validate() override;

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
    seqToShardIndex(std::uint32_t seq) const override
    {
        assert(seq >= earliestSeq());
        return NodeStore::seqToShardIndex(seq, ledgersPerShard_);
    }

    std::uint32_t
    firstLedgerSeq(std::uint32_t shardIndex) const override
    {
        assert(shardIndex >= earliestShardIndex_);
        if (shardIndex <= earliestShardIndex_)
            return earliestSeq();
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

    /** Import the application local node store

        @param source The application node store.
    */
    void
    import(Database& source) override;

    std::int32_t
    getWriteLoad() const override;

    void
    store(NodeObjectType type, Blob&& data,
        uint256 const& hash, std::uint32_t seq) override;

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, std::uint32_t seq) override;

    bool
    asyncFetch(uint256 const& hash, std::uint32_t seq,
        std::shared_ptr<NodeObject>& object) override;

    bool
    copyLedger(std::shared_ptr<Ledger const> const& ledger) override;

    int
    getDesiredAsyncReadCount(std::uint32_t seq) override;

    float
    getCacheHitRate() override;

    void
    tune(int size, std::chrono::seconds age) override {};

    void
    sweep() override;

private:
    Application& app_;
    mutable std::mutex m_;
    bool init_ {false};

    // The context shared with all shard backend databases
    std::unique_ptr<nudb::context> ctx_;

    // Complete shards
    std::map<std::uint32_t, std::unique_ptr<Shard>> complete_;

    // A shard being acquired from the peer network
    std::unique_ptr<Shard> incomplete_;

    // Shards prepared for import
    std::map<std::uint32_t, Shard*> preShards_;

    // The shard store root directory
    boost::filesystem::path dir_;

    // If new shards can be stored
    bool canAdd_ {true};

    // Complete shard indexes
    std::string status_;

    // If backend type uses permanent storage
    bool backed_;

    // The name associated with the backend used with the shard store
    std::string backendName_;

    // Maximum storage space the shard store can utilize (in bytes)
    std::uint64_t maxFileSz_;

    // Storage space utilized by the shard store (in bytes)
    std::uint64_t fileSz_ {0};

    // Each shard stores 16384 ledgers. The earliest shard may store
    // less if the earliest ledger sequence truncates its beginning.
    // The value should only be altered for unit tests.
    std::uint32_t ledgersPerShard_ = ledgersPerShardDefault;

    // The earliest shard index
    std::uint32_t const earliestShardIndex_;

    // Average storage space required by a shard (in bytes)
    std::uint64_t avgShardFileSz_;

    // File name used to mark shards being imported from node store
    static constexpr auto importMarker_ = "import";

    std::shared_ptr<NodeObject>
    fetchFrom(uint256 const& hash, std::uint32_t seq) override;

    void
    for_each(std::function <void(std::shared_ptr<NodeObject>)> f) override
    {
        Throw<std::runtime_error>("Shard store import not supported");
    }

    // Finds a random shard index that is not stored
    // Lock must be held
    boost::optional<std::uint32_t>
    findShardIndexToAdd(std::uint32_t validLedgerSeq,
        std::lock_guard<std::mutex>&);

    // Set storage and file descriptor usage stats
    // Lock must be held
    void
    setFileStats(std::lock_guard<std::mutex>&);

    // Update status string
    // Lock must be held
    void
    updateStatus(std::lock_guard<std::mutex>&);

    std::pair<std::shared_ptr<PCache>, std::shared_ptr<NCache>>
    selectCache(std::uint32_t seq);

    // Returns available storage space
    std::uint64_t
    available() const;
};

} // NodeStore
} // ripple

#endif
