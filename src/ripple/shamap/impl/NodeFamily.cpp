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
#include <ripple/shamap/NodeFamily.h>

namespace ripple {

NodeFamily::NodeFamily(Application& app, CollectorManager& cm)
    : app_(app)
    , db_(app.getNodeStore())
    , j_(app.journal("NodeFamily"))
    , fbCache_(std::make_shared<FullBelowCache>(
          "Node family full below cache",
          stopwatch(),
          app.journal("NodeFamilyFulLBelowCache"),
          cm.collector(),
          fullBelowTargetSize,
          fullBelowExpiration))
    , tnCache_(std::make_shared<TreeNodeCache>(
          "Node family tree node cache",
          app.config().getValueFor(SizedItem::treeCacheSize),
          std::chrono::seconds(
              app.config().getValueFor(SizedItem::treeCacheAge)),
          stopwatch(),
          j_))
{
}

void
NodeFamily::sweep()
{
    fbCache_->sweep();
    tnCache_->sweep();
}

void
NodeFamily::reset()
{
    {
        std::lock_guard lock(maxSeqMutex_);
        maxSeq_ = 0;
    }

    fbCache_->reset();
    tnCache_->reset();
}

void
NodeFamily::missingNode(std::uint32_t seq)
{
    JLOG(j_.error()) << "Missing node in " << seq;

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
NodeFamily::acquire(uint256 const& hash, std::uint32_t seq)
{
    if (hash.isNonZero())
    {
        JLOG(j_.error()) << "Missing node in " << to_string(hash);

        app_.getInboundLedgers().acquire(
            hash, seq, InboundLedger::Reason::GENERIC);
    }
}

}  // namespace ripple
