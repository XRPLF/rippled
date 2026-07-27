#include <xrpld/app/ledger/LedgerReplayTask.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/detail/LedgerDeltaAcquire.h>
#include <xrpld/app/ledger/detail/SkipListAcquire.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/telemetry/MetricMacros.h>
#include <xrpld/telemetry/MetricNames.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace xrpl {

LedgerReplayTask::TaskParameter::TaskParameter(
    InboundLedger::Reason r,
    uint256 const& finishLedgerHash,
    std::uint32_t totalNumLedgers)
    : reason(r), finishHash(finishLedgerHash), totalLedgers(totalNumLedgers)
{
    XRPL_ASSERT(
        finishLedgerHash.isNonZero() && totalNumLedgers > 0,
        "xrpl::LedgerReplayTask::TaskParameter::TaskParameter : valid "
        "inputs");
}

bool
LedgerReplayTask::TaskParameter::update(
    uint256 const& hash,
    std::uint32_t seq,
    std::vector<uint256> const& sList)
{
    if (finishHash != hash || sList.size() + 1 < totalLedgers || full)
        return false;

    finishSeq = seq;
    skipList = sList;
    skipList.emplace_back(finishHash);
    startHash = skipList[skipList.size() - totalLedgers];
    XRPL_ASSERT(
        startHash.isNonZero(),
        "xrpl::LedgerReplayTask::TaskParameter::update : nonzero start hash");
    startSeq = finishSeq - totalLedgers + 1;
    full = true;
    return true;
}

bool
LedgerReplayTask::TaskParameter::canMergeInto(TaskParameter const& existingTask) const
{
    if (reason == existingTask.reason)
    {
        if (finishHash == existingTask.finishHash && totalLedgers <= existingTask.totalLedgers)
        {
            return true;
        }

        if (existingTask.full)
        {
            auto const& exList = existingTask.skipList;
            if (auto i = std::ranges::find(exList, finishHash); i != exList.end())
            {
                return existingTask.totalLedgers >= totalLedgers + (exList.end() - i) - 1;
            }
        }
    }

    return false;
}

LedgerReplayTask::LedgerReplayTask(
    Application& app,
    InboundLedgers& inboundLedgers,
    LedgerReplayer& replayer,
    std::shared_ptr<SkipListAcquire>& skipListAcquirer,
    TaskParameter const& parameter)
    : TimeoutCounter(
          app,
          parameter.finishHash,
          LedgerReplayParameters::kTaskTimeout,
          {.jobType = JtReplayTask,
           .jobName = "LedReplTask",
           .jobLimit = LedgerReplayParameters::kMaxQueuedTasks},
          app.getJournal("LedgerReplayTask"))
    , inboundLedgers_(inboundLedgers)
    , replayer_(replayer)
    , parameter_(parameter)
    , maxTimeouts_(
          std::max(
              LedgerReplayParameters::kTaskMaxTimeoutsMinimum,
              parameter.totalLedgers * LedgerReplayParameters::kTaskMaxTimeoutsMultiplier))
    , skipListAcquirer_(skipListAcquirer)
{
    JLOG(journal_.trace()) << "Create " << hash_;
}

LedgerReplayTask::~LedgerReplayTask()
{
    JLOG(journal_.trace()) << "Destroy " << hash_;
}

void
LedgerReplayTask::init()
{
    JLOG(journal_.debug()) << "Task start " << hash_;

    std::weak_ptr<LedgerReplayTask> const wptr = shared_from_this();
    skipListAcquirer_->addDataCallback([wptr](bool good, uint256 const& hash) {
        if (auto sptr = wptr.lock(); sptr)
        {
            if (!good)
            {
                sptr->cancel();
            }
            else
            {
                auto const skipListData = sptr->skipListAcquirer_->getData();
                sptr->updateSkipList(hash, skipListData->ledgerSeq, skipListData->skipList);
            }
        }
    });

    ScopedLockType sl(mtx_);
    if (!isDone())
    {
        trigger(sl);
        setTimer(sl);
    }
}

void
LedgerReplayTask::trigger(ScopedLockType& sl)
{
    JLOG(journal_.trace()) << "trigger " << hash_;
    if (!parameter_.full)
        return;

    if (!parent_)
    {
        parent_ = app_.getLedgerMaster().getLedgerByHash(parameter_.startHash);
        if (!parent_)
        {
            parent_ = inboundLedgers_.acquire(
                parameter_.startHash, parameter_.startSeq, InboundLedger::Reason::GENERIC);
        }
        if (parent_)
        {
            JLOG(journal_.trace())
                << "Got start ledger " << parameter_.startHash << " for task " << hash_;
        }
    }

    tryAdvance(sl);
}

void
LedgerReplayTask::deltaReady(uint256 const& deltaHash)
{
    JLOG(journal_.trace()) << "Delta " << deltaHash << " ready for task " << hash_;
    ScopedLockType sl(mtx_);
    if (!isDone())
        tryAdvance(sl);
}

void
LedgerReplayTask::tryAdvance(ScopedLockType& sl)
{
    JLOG(journal_.trace()) << "tryAdvance task " << hash_
                           << (parameter_.full ? ", full parameter" : ", waiting to fill parameter")
                           << ", deltaIndex=" << deltaToBuild_ << ", totalDeltas=" << deltas_.size()
                           << ", parent " << (parent_ ? parent_->header().hash : uint256());

    bool const shouldTry =
        parent_ && parameter_.full && parameter_.totalLedgers - 1 == deltas_.size();
    if (!shouldTry)
        return;

    try
    {
        for (; deltaToBuild_ < deltas_.size(); ++deltaToBuild_)
        {
            auto& delta = deltas_[deltaToBuild_];
            XRPL_ASSERT(
                parent_->seq() + 1 == delta->ledgerSeq_,
                "xrpl::LedgerReplayTask::tryAdvance : consecutive sequence");
            if (auto l = delta->tryBuild(parent_); l)
            {
                JLOG(journal_.debug())
                    << "Task " << hash_ << " got ledger " << l->header().hash
                    << " deltaIndex=" << deltaToBuild_ << " totalDeltas=" << deltas_.size();
                parent_ = l;
            }
            else
            {
                return;
            }
        }

        complete_ = true;
        JLOG(journal_.info()) << "Completed " << hash_;

        // Terminal success. Emitted once per task: the loop above is guarded by
        // isDone() at every entry point, so a completed task cannot re-enter
        // and double-count.
        recordOutcome(telemetry::lval::replay_outcome::success);
    }
    catch (std::runtime_error const&)
    {
        failed_ = true;

        // A delta failed to build on top of its parent, so the replayed range
        // cannot be reconstructed. Previously not logged at all here, only
        // reflected in failed_.
        recordOutcome(telemetry::lval::replay_outcome::buildFailed);
    }
}

void
LedgerReplayTask::recordOutcome(char const* outcome) const
{
    // One labelled counter Add per terminal event, never per delta or per
    // ledger. Replay quietly falling back to plain acquisition is the failure
    // this makes visible: without it, a replay that never succeeds is
    // indistinguishable from one that was never attempted.
    XRPL_METRIC_COUNTER_INC_LABELED(
        app_,
        telemetry::metric::ledgerReplayOutcomeTotal,
        "Ledger replay tasks by terminal outcome",
        {{telemetry::label::outcome, std::string(outcome)}});
}

void
LedgerReplayTask::updateSkipList(
    uint256 const& hash,
    std::uint32_t seq,
    std::vector<uint256> const& sList)
{
    {
        ScopedLockType const sl(mtx_);
        if (isDone())
            return;
        if (!parameter_.update(hash, seq, sList))
        {
            JLOG(journal_.error()) << "Parameter update failed " << hash_;
            failed_ = true;

            // The acquired skip list did not match what this task asked for,
            // so the task is abandoned before any delta is fetched. A distinct
            // outcome from a timeout: this one indicates a peer served an
            // inconsistent skip list, not a slow or absent peer.
            recordOutcome(telemetry::lval::replay_outcome::parameterFailed);
            return;
        }
    }

    replayer_.createDeltas(shared_from_this());
    ScopedLockType sl(mtx_);
    if (!isDone())
        trigger(sl);
}

void
LedgerReplayTask::onTimer(bool progress, ScopedLockType& sl)
{
    JLOG(journal_.trace()) << "timeouts_=" << timeouts_ << " for " << hash_;
    if (timeouts_ > maxTimeouts_)
    {
        failed_ = true;
        JLOG(journal_.debug()) << "LedgerReplayTask Failed, too many timeouts " << hash_;

        // The task ran out of retries waiting for its deltas. This is the
        // outcome that pairs with the fallback counters: the sub-acquires gave
        // up, and so did the task above them.
        recordOutcome(telemetry::lval::timeout);
    }
    else
    {
        trigger(sl);
    }
}

std::weak_ptr<TimeoutCounter>
LedgerReplayTask::pmDowncast()
{
    return shared_from_this();
}

void
LedgerReplayTask::addDelta(std::shared_ptr<LedgerDeltaAcquire> const& delta)
{
    std::weak_ptr<LedgerReplayTask> const wptr = shared_from_this();
    delta->addDataCallback(parameter_.reason, [wptr](bool good, uint256 const& hash) {
        if (auto sptr = wptr.lock(); sptr)
        {
            if (!good)
            {
                sptr->cancel();
            }
            else
            {
                sptr->deltaReady(hash);
            }
        }
    });

    ScopedLockType const sl(mtx_);
    if (!isDone())
    {
        JLOG(journal_.trace()) << "addDelta task " << hash_ << " deltaIndex=" << deltaToBuild_
                               << " totalDeltas=" << deltas_.size();
        XRPL_ASSERT(
            deltas_.empty() || deltas_.back()->ledgerSeq_ + 1 == delta->ledgerSeq_,
            "xrpl::LedgerReplayTask::addDelta : no deltas or consecutive "
            "sequence");
        deltas_.push_back(delta);
    }
}

bool
LedgerReplayTask::finished() const
{
    ScopedLockType const sl(mtx_);
    return isDone();
}

}  // namespace xrpl
