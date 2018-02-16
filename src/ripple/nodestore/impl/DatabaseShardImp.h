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
    DatabaseShardImp& operator=(DatabaseShardImp const&) = delete;

    DatabaseShardImp(Application& app, std::string const& name,
        Stoppable& parent, Scheduler& scheduler, int readThreads,
            Section const& config, beast::Journal j);

    ~DatabaseShardImp() override;

    bool
    init() override;

    boost::optional<std::uint32_t>
    prepare(std::uint32_t validLedgerSeq) override;

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
        return (seq - 1) / ledgersPerShard_;
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

    std::string
    getName() const override
    {
        return "shardstore";
    }

    void
    import(Database& source) override
    {
        Throw<std::runtime_error>("Shard store import not supported");
    }

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
    tune(int size, int age) override;

    void
    sweep() override;

private:
    Application& app_;
    mutable std::mutex m_;
    bool init_ {false};
    std::map<std::uint32_t, std::unique_ptr<Shard>> complete_;
    std::unique_ptr<Shard> incomplete_;
    Section const config_;
    boost::filesystem::path dir_;

    // If new shards can be stored
    bool canAdd_ {true};

    // Complete shard indexes
    std::string status_;

    // If backend type uses permanent storage
    bool backed_;

    // Maximum disk space the DB can use (in bytes)
    std::uint64_t const maxDiskSpace_;

    // Disk space used to store the shards (in bytes)
    std::uint64_t usedDiskSpace_ {0};

    // Each shard stores 16384 ledgers. The earliest shard may store
    // less if the earliest ledger sequence truncates its beginning.
    // The value should only be altered for unit tests.
    std::uint32_t const ledgersPerShard_;

    // The earliest shard index
    std::uint32_t const earliestShardIndex_;

    // Average disk space a shard requires (in bytes)
    std::uint64_t avgShardSz_;

    // Shard cache tuning
    int cacheSz_ {shardCacheSz};
    PCache::clock_type::rep cacheAge_ {shardCacheSeconds};

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

    // Updates stats
    // Lock must be held
    void
    updateStats(std::lock_guard<std::mutex>&);

    std::pair<std::shared_ptr<PCache>, std::shared_ptr<NCache>>
    selectCache(std::uint32_t seq);

    // Returns the tune cache size divided by the number of shards
    // Lock must be held
    int
    calcTargetCacheSz(std::lock_guard<std::mutex>&) const
    {
        return std::max(shardCacheSz, cacheSz_ / std::max(
            1, static_cast<int>(complete_.size() + (incomplete_ ? 1 : 0))));
    }
};

} // NodeStore
} // ripple

#endif
