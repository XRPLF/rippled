#include <xrpld/app/ledger/LedgerReplayer.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerReplayTask.h>
#include <xrpld/app/ledger/detail/LedgerDeltaAcquire.h>
#include <xrpld/app/ledger/detail/SkipListAcquire.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/shamap/SHAMapItem.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace xrpl {

LedgerReplayer::LedgerReplayer(
    Application& app,
    InboundLedgers& inboundLedgers,
    std::unique_ptr<PeerSetBuilder> peerSetBuilder)
    : app_(app)
    , inboundLedgers_(inboundLedgers)
    , peerSetBuilder_(std::move(peerSetBuilder))
    , j_(app.getJournal("LedgerReplayer"))
{
}

LedgerReplayer::~LedgerReplayer()
{
    std::scoped_lock const lock(mtx_);
    tasks_.clear();
}

void
LedgerReplayer::replay(
    InboundLedger::Reason r,
    uint256 const& finishLedgerHash,
    std::uint32_t totalNumLedgers)
{
    XRPL_ASSERT(
        finishLedgerHash.isNonZero() && totalNumLedgers > 0 &&
            totalNumLedgers <= LedgerReplayParameters::kMaxTaskSize,
        "xrpl::LedgerReplayer::replay : valid inputs");

    // NOLINTNEXTLINE(misc-const-correctness)
    LedgerReplayTask::TaskParameter parameter(r, finishLedgerHash, totalNumLedgers);

    std::shared_ptr<LedgerReplayTask> task;
    std::shared_ptr<SkipListAcquire> skipList;
    bool newSkipList = false;
    {
        std::scoped_lock const lock(mtx_);
        if (app_.isStopping())
            return;
        if (tasks_.size() >= LedgerReplayParameters::kMaxTasks)
        {
            JLOG(j_.info()) << "Too many replay tasks, dropping new task " << parameter.finishHash;
            return;
        }

        for (auto const& t : tasks_)
        {
            if (parameter.canMergeInto(t->getTaskParameter()))
            {
                JLOG(j_.info()) << "Task " << parameter.finishHash << " with " << totalNumLedgers
                                << " ledgers merged into an existing task.";
                return;
            }
        }
        JLOG(j_.info()) << "Replay " << totalNumLedgers << " ledgers. Finish ledger hash "
                        << parameter.finishHash;

        auto i = skipLists_.find(parameter.finishHash);
        if (i != skipLists_.end())
            skipList = i->second.lock();

        if (!skipList)  // cannot find, or found but cannot lock
        {
            skipList = std::make_shared<SkipListAcquire>(
                app_, inboundLedgers_, parameter.finishHash, peerSetBuilder_->build());
            skipLists_[parameter.finishHash] = skipList;
            newSkipList = true;
        }

        task = std::make_shared<LedgerReplayTask>(
            app_, inboundLedgers_, *this, skipList, std::move(parameter));
        tasks_.push_back(task);
    }

    if (newSkipList)
        skipList->init(1);
    // task init after skipList init, could save a timeout
    task->init();
}

void
LedgerReplayer::createDeltas(std::shared_ptr<LedgerReplayTask> task)
{
    {
        // TODO for use cases like Consensus (i.e. totalLedgers = 1 or small):
        // check if the last closed or validated ledger l the local node has
        // is in the skip list and is an ancestor of parameter.startLedger
        // that has to be downloaded, if so expand the task to start with l.
    }

    auto const& parameter = task->getTaskParameter();
    JLOG(j_.trace()) << "Creating " << parameter.totalLedgers - 1 << " deltas";
    if (parameter.totalLedgers > 1)
    {
        auto skipListItem = std::ranges::find(parameter.skipList, parameter.startHash);
        auto const wasLast = skipListItem == parameter.skipList.end();
        if (not wasLast)
            ++skipListItem;
        auto const isLast = skipListItem == parameter.skipList.end();

        if (wasLast || isLast)
        {
            JLOG(j_.error()) << "Task parameter error when creating deltas "
                             << parameter.finishHash;
            return;
        }

        for (std::uint32_t seq = parameter.startSeq + 1;
             seq <= parameter.finishSeq && skipListItem != parameter.skipList.end();
             ++seq, ++skipListItem)
        {
            std::shared_ptr<LedgerDeltaAcquire> delta;
            bool newDelta = false;
            {
                std::scoped_lock const lock(mtx_);
                if (app_.isStopping())
                    return;
                auto i = deltas_.find(*skipListItem);
                if (i != deltas_.end())
                    delta = i->second.lock();

                if (!delta)  // cannot find, or found but cannot lock
                {
                    delta = std::make_shared<LedgerDeltaAcquire>(
                        app_, inboundLedgers_, *skipListItem, seq, peerSetBuilder_->build());
                    deltas_[*skipListItem] = delta;
                    newDelta = true;
                }
            }

            task->addDelta(delta);
            if (newDelta)
                delta->init(1);
        }
    }
}

void
LedgerReplayer::gotSkipList(
    LedgerHeader const& info,
    boost::intrusive_ptr<SHAMapItem const> const& item)
{
    std::shared_ptr<SkipListAcquire> skipList = {};
    {
        std::scoped_lock const lock(mtx_);
        auto i = skipLists_.find(info.hash);
        if (i == skipLists_.end())
            return;
        skipList = i->second.lock();
        if (!skipList)
        {
            skipLists_.erase(i);
            return;
        }
    }

    if (skipList)
        skipList->processData(info.seq, item);
}

void
LedgerReplayer::gotReplayDelta(
    LedgerHeader const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns)
{
    std::shared_ptr<LedgerDeltaAcquire> delta = {};
    {
        std::scoped_lock const lock(mtx_);
        auto i = deltas_.find(info.hash);
        if (i == deltas_.end())
            return;
        delta = i->second.lock();
        if (!delta)
        {
            deltas_.erase(i);
            return;
        }
    }

    if (delta)
        delta->processData(info, std::move(txns));
}

void
LedgerReplayer::sweep()
{
    auto const start = std::chrono::steady_clock::now();
    {
        std::scoped_lock const lock(mtx_);
        JLOG(j_.debug()) << "Sweeping, LedgerReplayer has " << tasks_.size() << " tasks, "
                         << skipLists_.size() << " skipLists, and " << deltas_.size() << " deltas.";

        tasks_.erase(
            std::ranges::remove_if(
                tasks_,

                [this](auto const& t) -> bool {
                    if (t->finished())
                    {
                        JLOG(j_.debug()) << "Sweep task " << t->getTaskParameter().finishHash;
                        return true;
                    }
                    return false;
                })
                .begin(),
            tasks_.end());

        auto removeCannotLocked = [](auto& subTasks) {
            for (auto it = subTasks.begin(); it != subTasks.end();)
            {
                if (auto item = it->second.lock(); !item)
                {
                    it = subTasks.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        };
        removeCannotLocked(skipLists_);
        removeCannotLocked(deltas_);
    }
    JLOG(j_.debug()) << " LedgerReplayer sweep lock duration "
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count()
                     << "ms";
}

void
LedgerReplayer::stop()
{
    JLOG(j_.info()) << "Stopping...";
    {
        std::scoped_lock const lock(mtx_);
        std::ranges::for_each(tasks_, [](auto& i) { i->cancel(); });
        tasks_.clear();
        auto lockAndCancel = [](auto& i) {
            if (auto sptr = i.second.lock(); sptr)
            {
                sptr->cancel();
            }
        };
        std::ranges::for_each(skipLists_, lockAndCancel);
        skipLists_.clear();
        std::ranges::for_each(deltas_, lockAndCancel);
        deltas_.clear();
    }

    JLOG(j_.info()) << "Stopped";
}

}  // namespace xrpl
