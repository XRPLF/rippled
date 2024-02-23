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

#ifndef RIPPLE_SHAMAP_SHARDFAMILY_H_INCLUDED
#define RIPPLE_SHAMAP_SHARDFAMILY_H_INCLUDED

#include <ripple/app/main/CollectorManager.h>
#include <ripple/shamap/Family.h>

namespace ripple {

class Application;

class ShardFamily : public Family
{
public:
    ShardFamily() = delete;
    ShardFamily(ShardFamily const&) = delete;
    ShardFamily(ShardFamily&&) = delete;

    ShardFamily&
    operator=(ShardFamily const&) = delete;

    ShardFamily&
    operator=(ShardFamily&&) = delete;

    ShardFamily(Application& app, CollectorManager& cm);

    NodeStore::Database&
    db() override
    {
        return db_;
    }

    NodeStore::Database const&
    db() const override
    {
        return db_;
    }

    beast::Journal const&
    journal() override
    {
        return j_;
    }

    bool
    isShardBacked() const override
    {
        return true;
    }

    std::shared_ptr<FullBelowCache>
    getFullBelowCache(std::uint32_t ledgerSeq) override;

    /** Return the number of entries in the cache */
    int
    getFullBelowCacheSize();

    std::shared_ptr<TreeNodeCache>
    getTreeNodeCache(std::uint32_t ledgerSeq) override;

    /** Return a pair where the first item is the number of items cached
        and the second item is the number of entries in the cached
    */
    std::pair<int, int>
    getTreeNodeCacheSize();

    void
    sweep() override;

    void
    reset() override;

    void
    missingNodeAcquireBySeq(std::uint32_t seq, uint256 const& nodeHash)
        override;

    void
    missingNodeAcquireByHash(uint256 const& hash, std::uint32_t seq) override
    {
        acquire(hash, seq);
    }

private:
    Application& app_;
    NodeStore::Database& db_;
    CollectorManager& cm_;
    beast::Journal const j_;

    std::unordered_map<std::uint32_t, std::shared_ptr<FullBelowCache>> fbCache_;
    std::mutex fbCacheMutex_;

    std::unordered_map<std::uint32_t, std::shared_ptr<TreeNodeCache>> tnCache_;
    std::mutex tnCacheMutex_;
    int const tnTargetSize_;
    std::chrono::seconds const tnTargetAge_;

    // Missing node handler
    LedgerIndex maxSeq_{0};
    std::mutex maxSeqMutex_;

    void
    acquire(uint256 const& hash, std::uint32_t seq);
};

}  // namespace ripple

#endif
