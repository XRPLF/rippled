//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/main/Tuning.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/shamap/ShardFamily.h>
#include <tuple>

namespace ripple {

static NodeStore::Database&
getShardStore(Application& app)
{
    auto const dbPtr = app.getShardStore();
    assert(dbPtr);
    return *dbPtr;
}

ShardFamily::ShardFamily(Application& app, CollectorManager& cm)
    : app_(app)
    , db_(getShardStore(app))
    , cm_(cm)
    , j_(app.journal("ShardFamily"))
    , tnTargetSize_(app.config().getValueFor(SizedItem::treeCacheSize, 0))
    , tnTargetAge_(app.config().getValueFor(SizedItem::treeCacheAge, 0))
{
}

void
ShardFamily::resetCacheFor(std::uint32_t ledgerSeq)
{
    auto const shardIndex = app_.getShardStore()->seqToShardIndex(ledgerSeq);

    {  // Destroy the cache without a lock
        std::shared_ptr<FullBelowCache> fbc;

        {
            std::lock_guard lock(fbCacheMutex_);
            if (auto const it = fbCache_.find(shardIndex); it != fbCache_.end())
            {
                fbc = std::move(it->second);
                fbCache_.erase(it);
            }
        }
    }

    {  // Destroy the cache without a lock
        std::shared_ptr<TreeNodeCache> tnc;

        {
            std::lock_guard lock(tnCacheMutex_);

            if (auto const it = tnCache_.find(shardIndex); it != tnCache_.end())
            {
                tnc = std::move(it->second);
                tnCache_.erase(it);
            }
        }
    }
}

std::shared_ptr<FullBelowCache>
ShardFamily::getFullBelowCache(std::uint32_t ledgerSeq)
{
    auto const shardIndex{app_.getShardStore()->seqToShardIndex(ledgerSeq)};
    std::lock_guard lock(fbCacheMutex_);
    if (auto const it{fbCache_.find(shardIndex)}; it != fbCache_.end())
        return it->second;

    // Create a cache for the corresponding shard
    auto fbCache{std::make_shared<FullBelowCache>(
        "Shard #" + std::to_string(shardIndex),
        stopwatch(),
        j_,
        fullBelowTargetSize,
        fullBelowExpiration,
        cm_.collector())};
    return fbCache_.emplace(shardIndex, std::move(fbCache)).first->second;
}

std::shared_ptr<TreeNodeCache>
ShardFamily::getTreeNodeCache(std::uint32_t ledgerSeq)
{
    auto const shardIndex{app_.getShardStore()->seqToShardIndex(ledgerSeq)};
    std::lock_guard lock(tnCacheMutex_);
    if (auto const it{tnCache_.find(shardIndex)}; it != tnCache_.end())
        return it->second;

    // Create a cache for the corresponding shard
    auto tnCache{std::make_shared<TreeNodeCache>(
        "Shard family tree node cache shard " + std::to_string(shardIndex),
        tnTargetSize_,
        tnTargetAge_,
        stopwatch(),
        j_)};
    return tnCache_.emplace(shardIndex, std::move(tnCache)).first->second;
}

void
ShardFamily::sweep()
{
    {
        std::lock_guard lock(fbCacheMutex_);
        for (auto it = fbCache_.cbegin(); it != fbCache_.cend();)
        {
            it->second->sweep();

            // Remove cache if empty
            if (it->second->size() == 0)
                it = fbCache_.erase(it);
            else
                ++it;
        }
    }

    std::lock_guard lock(tnCacheMutex_);
    for (auto it = tnCache_.cbegin(); it != tnCache_.cend();)
    {
        it->second->sweep();

        // Remove cache if empty
        if (it->second->getTrackSize() == 0)
            it = tnCache_.erase(it);
        else
            ++it;
    }
}

void
ShardFamily::missingNodeAcquireBySeq(std::uint32_t seq, uint256 const& nodeHash)
{
    std::ignore = nodeHash;
    JLOG(j_.error()) << "Missing node in ledger sequence " << seq;

    std::unique_lock<std::mutex> lock(maxSeqMutex_);
    if (maxSeq_ == 0)
    {
        maxSeq_ = seq;

        do
        {
            // Try to acquire the most recent missing ledger
            seq = maxSeq_;

            lock.unlock();

            // This can invoke the missing node handler
            acquire(app_.getLedgerMaster().getHashBySeq(seq), seq);

            lock.lock();
        } while (maxSeq_ != seq);
    }
    else if (maxSeq_ < seq)
    {
        // We found a more recent ledger with a missing node
        maxSeq_ = seq;
    }
}

void
ShardFamily::acquire(uint256 const& hash, std::uint32_t seq)
{
    if (hash.isNonZero())
    {
        JLOG(j_.error()) << "Missing node in " << to_string(hash);

        app_.getInboundLedgers().acquire(
            hash, seq, InboundLedger::Reason::SHARD);
    }
}

}  // namespace ripple
