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

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/PeerSet.h>

namespace ripple {

LedgerDeltaAcquire::LedgerDeltaAcquire(
    Application& app,
    InboundLedgers& inboundLedgers,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayParameters::SUB_TASK_TIMEOUT,
          {jtREPLAY_TASK,
           "LedgerReplayDelta",
           LedgerReplayParameters::MAX_QUEUED_TASKS},
          app.journal("LedgerReplayDelta"))
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
                    JLOG(journal_.trace())
                        << "Add a peer " << peer->id() << " for " << hash_;
                    protocol::TMReplayDeltaRequest request;
                    request.set_ledgerhash(hash_.data(), hash_.size());
                    peerSet_->sendRequest(request, peer);
                }
                else
                {
                    if (++noFeaturePeerCount >=
                        LedgerReplayParameters::MAX_NO_FEATURE_PEER_COUNT)
                    {
                        JLOG(journal_.debug()) << "Fall back for " << hash_;
                        timerInterval_ =
                            LedgerReplayParameters::SUB_TASK_FALLBACK_TIMEOUT;
                        fallBack_ = true;
                    }
                }
            });
    }

    if (fallBack_)
        inboundLedgers_.acquire(
            hash_, ledgerSeq_, InboundLedger::Reason::GENERIC);
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& sl)
{
    JLOG(journal_.trace()) << "mTimeouts=" << timeouts_ << " for " << hash_;
    if (timeouts_ > LedgerReplayParameters::SUB_TASK_MAX_TIMEOUTS)
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
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
{
    ScopedLockType sl(mtx_);
    JLOG(journal_.trace()) << "got data for " << hash_;
    if (isDone())
        return;

    if (info.seq == ledgerSeq_)
    {
        // create a temporary ledger for building a LedgerReplay object later
        replayTemp_ =
            std::make_shared<Ledger>(info, app_.config(), app_.getNodeFamily());
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
    JLOG(journal_.error())
        << "failed to create a (info only) ledger from verified data " << hash_;
    notify(sl);
}

void
LedgerDeltaAcquire::addDataCallback(
    InboundLedger::Reason reason,
    OnDeltaDataCB&& cb)
{
    ScopedLockType sl(mtx_);
    dataReadyCallbacks_.emplace_back(std::move(cb));
    if (reasons_.count(reason) == 0)
    {
        reasons_.emplace(reason);
        if (fullLedger_)
            onLedgerBuilt(sl, reason);
    }

    if (isDone())
    {
        JLOG(journal_.debug())
            << "task added to a finished LedgerDeltaAcquire " << hash_;
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

    assert(parent->seq() + 1 == replayTemp_->seq());
    assert(parent->info().hash == replayTemp_->info().parentHash);
    // build ledger
    LedgerReplay replayData(parent, replayTemp_, std::move(orderedTxns_));
    fullLedger_ = buildLedger(replayData, tapNONE, app_, journal_);
    if (fullLedger_ && fullLedger_->info().hash == hash_)
    {
        JLOG(journal_.info()) << "Built " << hash_;
        onLedgerBuilt(sl);
        return fullLedger_;
    }
    else
    {
        failed_ = true;
        complete_ = false;
        JLOG(journal_.error()) << "tryBuild failed " << hash_ << " with parent "
                               << parent->info().hash;
        Throw<std::runtime_error>("Cannot replay ledger");
    }
}

void
LedgerDeltaAcquire::onLedgerBuilt(
    ScopedLockType& sl,
    std::optional<InboundLedger::Reason> reason)
{
    JLOG(journal_.debug()) << "onLedgerBuilt " << hash_
                           << (reason ? " for a new reason" : "");

    std::vector<InboundLedger::Reason> reasons(
        reasons_.begin(), reasons_.end());
    bool firstTime = true;
    if (reason)  // small chance
    {
        reasons.clear();
        reasons.push_back(*reason);
        firstTime = false;
    }
    app_.getJobQueue().addJob(
        jtREPLAY_TASK,
        "onLedgerBuilt",
        [=, ledger = this->fullLedger_, &app = this->app_](Job&) {
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
    assert(isDone());
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

}  // namespace ripple
