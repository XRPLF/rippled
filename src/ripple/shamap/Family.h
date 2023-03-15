//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_SHAMAP_FAMILY_H_INCLUDED
#define RIPPLE_SHAMAP_FAMILY_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/nodestore/Database.h>
#include <ripple/shamap/FullBelowCache.h>
#include <ripple/shamap/TreeNodeCache.h>
#include <cstdint>

namespace ripple {

class Family
{
public:
    Family(Family const&) = delete;
    Family(Family&&) = delete;

    Family&
    operator=(Family const&) = delete;

    Family&
    operator=(Family&&) = delete;

    explicit Family() = default;
    virtual ~Family() = default;

    virtual NodeStore::Database&
    db() = 0;

    virtual NodeStore::Database const&
    db() const = 0;

    virtual beast::Journal const&
    journal() = 0;

    /** Return a pointer to the Family Full Below Cache

        @param ledgerSeq ledger sequence determines a corresponding shard cache
        @note ledgerSeq is used by ShardFamily and ignored by NodeFamily
    */
    virtual std::shared_ptr<FullBelowCache>
    getFullBelowCache(std::uint32_t ledgerSeq) = 0;

    /** Return a pointer to the Family Tree Node Cache

        @param ledgerSeq ledger sequence determines a corresponding shard cache
        @note ledgerSeq is used by ShardFamily and ignored by NodeFamily
    */
    virtual std::shared_ptr<TreeNodeCache>
    getTreeNodeCache(std::uint32_t ledgerSeq) = 0;

    virtual void
    sweep() = 0;

    virtual bool
    isShardBacked() const = 0;

    /** Acquire ledger that has a missing node by ledger sequence
     *
     * Throw if in reporting mode.
     *
     * @param refNum Sequence of ledger to acquire.
     * @param nodeHash Hash of missing node to report in throw.
     */
    virtual void
    missingNodeAcquireBySeq(std::uint32_t refNum, uint256 const& nodeHash) = 0;

    /** Acquire ledger that has a missing node by ledger hash
     *
     * @param refHash Hash of ledger to acquire.
     * @param refNum Ledger sequence with missing node.
     */
    virtual void
    missingNodeAcquireByHash(uint256 const& refHash, std::uint32_t refNum) = 0;

    virtual void
    reset() = 0;
};

}  // namespace ripple

#endif
