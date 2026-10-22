#include <xrpld/shamap/NodeFamily.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/app/main/Tuning.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace xrpl {

NodeFamily::NodeFamily(Application& app, CollectorManager& cm)
    : app_(app)
    , db_(app.getNodeStore())
    , j_(app.getJournal("NodeFamily"))
    , fbCache_(
          std::make_shared<FullBelowCache>(
              "Node family full below cache",
              stopwatch(),
              app.getJournal("NodeFamilyFulLBelowCache"),
              cm.collector(),
              kFullBelowTargetSize,
              kFullBelowExpiration))
    , tnCache_(
          std::make_shared<TreeNodeCache>(
              "Node family tree node cache",
              app.config().getValueFor(SizedItem::TreeCacheSize),
              std::chrono::seconds(app.config().getValueFor(SizedItem::TreeCacheAge)),
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
        std::scoped_lock const lock(maxSeqMutex_);
        maxSeq_ = 0;
    }

    (*fbCache_).reset();
    (*tnCache_).reset();
}

/**
 * Start an acquisition of the ledger named by a missing-node report.
 *
 * One pass runs at a time. While a pass is running, maxSeq_ holds the
 * highest sequence reported so far, so a report for a newer ledger only
 * raises it and the running pass picks that ledger up next. A report for an
 * older ledger during a pass is dropped. Once the pass has acquired the
 * highest sequence it saw, the latch is released so the next report starts a
 * pass of its own. The release holds on every exit, including an exception
 * out of the acquisition.
 *
 * @param seq The sequence of the ledger the node belongs to.
 * @param nodeHash The hash of the missing node, named in the log.
 */
void
NodeFamily::missingNodeAcquireBySeq(std::uint32_t seq, UInt256 const& nodeHash)
{
    JLOG(j_.error()) << "Missing node " << to_string(nodeHash) << " in ledger " << seq;
    std::unique_lock<std::mutex> lock(maxSeqMutex_);
    if (maxSeq_ == 0)
    {
        maxSeq_ = seq;

        try
        {
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
        catch (...)
        {
            // The acquisition ran with the lock released. Retake it and release the latch before
            // rethrowing.
            if (!lock.owns_lock())
                lock.lock();
            maxSeq_ = 0;
            throw;
        }

        // The pass is over. Release the latch so the next report starts a new one.
        maxSeq_ = 0;
    }
    else if (maxSeq_ < seq)
    {
        // We found a more recent ledger with a missing node
        maxSeq_ = seq;
    }
}

void
NodeFamily::acquire(UInt256 const& hash, std::uint32_t seq)
{
    if (hash.isNonZero())
    {
        JLOG(j_.error()) << "Missing node in " << to_string(hash);

        app_.getInboundLedgers().acquire(hash, seq, InboundLedger::Reason::GENERIC);
    }
}

}  // namespace xrpl
