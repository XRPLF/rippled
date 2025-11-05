#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/app/main/Tuning.h>
#include <xrpld/shamap/NodeFamily.h>

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
NodeFamily::missingNodeAcquireBySeq(std::uint32_t seq, uint256 const& nodeHash)
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
