#include <xrpld/app/ledger/detail/LedgerDeltaAcquire.h>

#include <xrpld/app/ledger/BuildLedger.h>
#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerReplay.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

LedgerDeltaAcquire::LedgerDeltaAcquire(
    Application& app,
    InboundLedgers& inboundLedgers,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayParameters::kSubTaskTimeout,
          {.jobType = JtReplayTask,
           .jobName = "LedReplDelta",
           .jobLimit = LedgerReplayParameters::kMaxQueuedTasks},
          app.getJournal("LedgerReplayDelta"))
    , inboundLedgers_(inboundLedgers)
    , ledgerSeq_(ledgerSeq)
    , peerSet_(std::move(peerSet))
{
    JLOG(journal_.trace()) << "Create " << hash_ << " Seq " << ledgerSeq;
}

LedgerDeltaAcquire::~LedgerDeltaAcquire()
{
    JLOG(journal_.trace()) << "Destroy " << hash_;
}

void
LedgerDeltaAcquire::init(int numPeers)
{
    ScopedLockType sl(mtx_);
    if (!isDone())
    {
        trigger(numPeers, sl);
        setTimer(sl);
    }
}

void
LedgerDeltaAcquire::trigger(std::size_t limit, ScopedLockType& sl)
{
    fullLedger_ = app_.getLedgerMaster().getLedgerByHash(hash_);
    if (fullLedger_)
    {
        complete_ = true;
        JLOG(journal_.trace()) << "existing ledger " << hash_;
        notify(sl);
        return;
    }

    if (!fallBack_)
    {
        peerSet_->addPeers(
            limit,
            [this](auto peer) {
                return peer->supportsFeature(ProtocolFeature::LedgerReplay) &&
                    peer->hasLedger(hash_, ledgerSeq_);
            },
            [this](auto peer) {
                if (peer->supportsFeature(ProtocolFeature::LedgerReplay))
                {
                    JLOG(journal_.trace()) << "Add a peer " << peer->id() << " for " << hash_;
                    protocol::TMReplayDeltaRequest request;
                    request.set_ledgerhash(hash_.data(), hash_.size());
                    peerSet_->sendRequest(request, peer);
                }
                else
                {
                    if (++noFeaturePeerCount_ >= LedgerReplayParameters::kMaxNoFeaturePeerCount)
                    {
                        JLOG(journal_.debug()) << "Fall back for " << hash_;
                        timerInterval_ = LedgerReplayParameters::kSubTaskFallbackTimeout;
                        fallBack_ = true;
                    }
                }
            });
    }

    if (fallBack_)
        inboundLedgers_.acquire(hash_, ledgerSeq_, InboundLedger::Reason::GENERIC);
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& sl)
{
    JLOG(journal_.trace()) << "timeouts_=" << timeouts_ << " for " << hash_;
    if (timeouts_ > LedgerReplayParameters::kSubTaskMaxTimeouts)
    {
        failed_ = true;
        JLOG(journal_.debug()) << "too many timeouts " << hash_;
        notify(sl);
    }
    else
    {
        trigger(1, sl);
    }
}

std::weak_ptr<TimeoutCounter>
LedgerDeltaAcquire::pmDowncast()
{
    return shared_from_this();
}

void
LedgerDeltaAcquire::processData(
    LedgerHeader const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
{
    ScopedLockType sl(mtx_);
    JLOG(journal_.trace()) << "got data for " << hash_;
    if (isDone())
        return;

    if (info.seq == ledgerSeq_)
    {
        // create a temporary ledger for building a LedgerReplay object later
        Rules const rules{app_.config().features};
        replayTemp_ = std::make_shared<Ledger>(info, rules, app_.getNodeFamily());
        if (replayTemp_)
        {
            complete_ = true;
            orderedTxns_ = std::move(orderedTxns);
            JLOG(journal_.debug()) << "ready to replay " << hash_;
            notify(sl);
            return;
        }
    }

    failed_ = true;
    JLOG(journal_.error()) << "failed to create a (info only) ledger from verified data " << hash_;
    notify(sl);
}

void
LedgerDeltaAcquire::addDataCallback(InboundLedger::Reason reason, OnDeltaDataCB&& cb)
{
    ScopedLockType sl(mtx_);
    dataReadyCallbacks_.emplace_back(std::move(cb));
    if (!reasons_.contains(reason))
    {
        reasons_.emplace(reason);
        if (fullLedger_)
            onLedgerBuilt(sl, reason);
    }

    if (isDone())
    {
        JLOG(journal_.debug()) << "task added to a finished LedgerDeltaAcquire " << hash_;
        notify(sl);
    }
}

std::shared_ptr<Ledger const>
LedgerDeltaAcquire::tryBuild(std::shared_ptr<Ledger const> const& parent)
{
    ScopedLockType sl(mtx_);

    if (fullLedger_)
        return fullLedger_;

    if (failed_ || !complete_ || !replayTemp_)
        return {};

    XRPL_ASSERT(
        parent->seq() + 1 == replayTemp_->seq(),
        "xrpl::LedgerDeltaAcquire::tryBuild : parent sequence match");
    XRPL_ASSERT(
        parent->header().hash == replayTemp_->header().parentHash,
        "xrpl::LedgerDeltaAcquire::tryBuild : parent hash match");
    // build ledger
    LedgerReplay const replayData(parent, replayTemp_, std::move(orderedTxns_));
    fullLedger_ = buildLedger(replayData, TapNone, app_, journal_);
    if (fullLedger_ && fullLedger_->header().hash == hash_)
    {
        JLOG(journal_.info()) << "Built " << hash_;
        onLedgerBuilt(sl);
        return fullLedger_;
    }

    failed_ = true;
    complete_ = false;
    JLOG(journal_.error()) << "tryBuild failed " << hash_ << " with parent "
                           << parent->header().hash;
    Throw<std::runtime_error>("Cannot replay ledger");
}

void
LedgerDeltaAcquire::onLedgerBuilt(ScopedLockType& sl, std::optional<InboundLedger::Reason> reason)
{
    JLOG(journal_.debug()) << "onLedgerBuilt " << hash_ << (reason ? " for a new reason" : "");

    std::vector<InboundLedger::Reason> reasons(reasons_.begin(), reasons_.end());
    bool firstTime = true;
    if (reason)  // small chance
    {
        reasons.clear();
        reasons.push_back(*reason);
        firstTime = false;
    }
    app_.getJobQueue().addJob(
        JtReplayTask, "OnLedBuilt", [=, ledger = this->fullLedger_, &app = this->app_]() {
            for (auto reason : reasons)
            {
                switch (reason)
                {
                    case InboundLedger::Reason::GENERIC:
                        app.getLedgerMaster().storeLedger(ledger);
                        break;
                    default:
                        // TODO for other use cases
                        break;
                }
            }

            if (firstTime)
                app.getLedgerMaster().tryAdvance();
        });
}

void
LedgerDeltaAcquire::notify(ScopedLockType& sl)
{
    XRPL_ASSERT(isDone(), "xrpl::LedgerDeltaAcquire::notify : is done");
    std::vector<OnDeltaDataCB> toCall;
    std::swap(toCall, dataReadyCallbacks_);
    auto const good = !failed_;
    sl.unlock();

    for (auto& cb : toCall)
    {
        cb(good, hash_);
    }

    sl.lock();
}

}  // namespace xrpl
