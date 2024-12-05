//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/detail/LedgerDeltaAcquire.h>
#include <xrpld/app/ledger/detail/SkipListAcquire.h>
#include <xrpld/core/JobQueue.h>

namespace ripple {

LedgerReplayer::LedgerReplayer(
    Application& app,
    InboundLedgers& inboundLedgers,
    std::unique_ptr<PeerSetBuilder> peerSetBuilder)
    : app_(app)
    , inboundLedgers_(inboundLedgers)
    , peerSetBuilder_(std::move(peerSetBuilder))
    , j_(app.journal("LedgerReplayer"))
{
}

LedgerReplayer::~LedgerReplayer()
{
    std::lock_guard<std::mutex> lock(mtx_);
    tasks_.clear();
}

void
LedgerReplayer::replay(
    InboundLedger::Reason r,
    uint256 const& finishLedgerHash,
    std::uint32_t totalNumLedgers)
{
    ASSERT(
        finishLedgerHash.isNonZero() && totalNumLedgers > 0 &&
            totalNumLedgers <= LedgerReplayParameters::MAX_TASK_SIZE,
        "ripple::LedgerReplayer::replay : valid inputs");

    LedgerReplayTask::TaskParameter parameter(
        r, finishLedgerHash, totalNumLedgers);

    std::shared_ptr<LedgerReplayTask> task;
    std::shared_ptr<SkipListAcquire> skipList;
    bool newSkipList = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (app_.isStopping())
            return;
        if (tasks_.size() >= LedgerReplayParameters::MAX_TASKS)
        {
            JLOG(j_.info()) << "Too many replay tasks, dropping new task "
                            << parameter.finishHash_;
            return;
        }

        for (auto const& t : tasks_)
        {
            if (parameter.canMergeInto(t->getTaskParameter()))
            {
                JLOG(j_.info()) << "Task " << parameter.finishHash_ << " with "
                                << totalNumLedgers
                                << " ledgers merged into an existing task.";
                return;
            }
        }
        JLOG(j_.info()) << "Replay " << totalNumLedgers
                        << " ledgers. Finish ledger hash "
                        << parameter.finishHash_;

        auto i = skipLists_.find(parameter.finishHash_);
        if (i != skipLists_.end())
            skipList = i->second.lock();

        if (!skipList)  // cannot find, or found but cannot lock
        {
            skipList = std::make_shared<SkipListAcquire>(
                app_,
                inboundLedgers_,
                parameter.finishHash_,
                peerSetBuilder_->build());
            skipLists_[parameter.finishHash_] = skipList;
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
    JLOG(j_.trace()) << "Creating " << parameter.totalLedgers_ - 1 << " deltas";
    if (parameter.totalLedgers_ > 1)
    {
        auto skipListItem = std::find(
            parameter.skipList_.begin(),
            parameter.skipList_.end(),
            parameter.startHash_);
        if (skipListItem == parameter.skipList_.end() ||
            ++skipListItem == parameter.skipList_.end())
        {
            JLOG(j_.error()) << "Task parameter error when creating deltas "
                             << parameter.finishHash_;
            return;
        }

        for (std::uint32_t seq = parameter.startSeq_ + 1;
             seq <= parameter.finishSeq_ &&
             skipListItem != parameter.skipList_.end();
             ++seq, ++skipListItem)
        {
            std::shared_ptr<LedgerDeltaAcquire> delta;
            bool newDelta = false;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (app_.isStopping())
                    return;
                auto i = deltas_.find(*skipListItem);
                if (i != deltas_.end())
                    delta = i->second.lock();

                if (!delta)  // cannot find, or found but cannot lock
                {
                    delta = std::make_shared<LedgerDeltaAcquire>(
                        app_,
                        inboundLedgers_,
                        *skipListItem,
                        seq,
                        peerSetBuilder_->build());
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
    LedgerInfo const& info,
    boost::intrusive_ptr<SHAMapItem const> const& item)
{
    std::shared_ptr<SkipListAcquire> skipList = {};
    {
        std::lock_guard<std::mutex> lock(mtx_);
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
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns)
{
    std::shared_ptr<LedgerDeltaAcquire> delta = {};
    {
        std::lock_guard<std::mutex> lock(mtx_);
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
        std::lock_guard<std::mutex> lock(mtx_);
        JLOG(j_.debug()) << "Sweeping, LedgerReplayer has " << tasks_.size()
                         << " tasks, " << skipLists_.size()
                         << " skipLists, and " << deltas_.size() << " deltas.";

        tasks_.erase(
            std::remove_if(
                tasks_.begin(),
                tasks_.end(),
                [this](auto const& t) -> bool {
                    if (t->finished())
                    {
                        JLOG(j_.debug()) << "Sweep task "
                                         << t->getTaskParameter().finishHash_;
                        return true;
                    }
                    return false;
                }),
            tasks_.end());

        auto removeCannotLocked = [](auto& subTasks) {
            for (auto it = subTasks.begin(); it != subTasks.end();)
            {
                if (auto item = it->second.lock(); !item)
                {
                    it = subTasks.erase(it);
                }
                else
                    ++it;
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
        std::lock_guard<std::mutex> lock(mtx_);
        std::for_each(
            tasks_.begin(), tasks_.end(), [](auto& i) { i->cancel(); });
        tasks_.clear();
        auto lockAndCancel = [](auto& i) {
            if (auto sptr = i.second.lock(); sptr)
            {
                sptr->cancel();
            }
        };
        std::for_each(skipLists_.begin(), skipLists_.end(), lockAndCancel);
        skipLists_.clear();
        std::for_each(deltas_.begin(), deltas_.end(), lockAndCancel);
        deltas_.clear();
    }

    JLOG(j_.info()) << "Stopped";
}

}  // namespace ripple
