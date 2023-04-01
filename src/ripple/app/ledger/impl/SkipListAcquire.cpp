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

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/PeerSet.h>

namespace ripple {

SkipListAcquire::SkipListAcquire(
    Application& app,
    InboundLedgers& inboundLedgers,
    uint256 const& ledgerHash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayParameters::SUB_TASK_TIMEOUT,
          {jtREPLAY_TASK,
           "SkipListAcquire",
           LedgerReplayParameters::MAX_QUEUED_TASKS},
          app.journal("LedgerReplaySkipList"))
    , inboundLedgers_(inboundLedgers)
    , peerSet_(std::move(peerSet))
{
    JLOG(journal_.trace()) << "Create " << hash_;
}

SkipListAcquire::~SkipListAcquire()
{
    JLOG(journal_.trace()) << "Destroy " << hash_;
}

void
SkipListAcquire::init(int numPeers)
{
    ScopedLockType sl(mtx_);
    if (!isDone())
    {
        trigger(numPeers, sl);
        setTimer(sl);
    }
}

void
SkipListAcquire::trigger(std::size_t limit, ScopedLockType& sl)
{
    if (auto const l = app_.getLedgerMaster().getLedgerByHash(hash_); l)
    {
        JLOG(journal_.trace()) << "existing ledger " << hash_;
        retrieveSkipList(l, sl);
        return;
    }

    if (!fallBack_)
    {
        peerSet_->addPeers(
            limit,
            [this](auto peer) {
                return peer->supportsFeature(ProtocolFeature::LedgerReplay) &&
                    peer->hasLedger(hash_, 0);
            },
            [this](auto peer) {
                if (peer->supportsFeature(ProtocolFeature::LedgerReplay))
                {
                    JLOG(journal_.trace())
                        << "Add a peer " << peer->id() << " for " << hash_;
                    protocol::TMProofPathRequest request;
                    request.set_ledgerhash(hash_.data(), hash_.size());
                    request.set_key(
                        keylet::skip().key.data(), keylet::skip().key.size());
                    request.set_type(
                        protocol::TMLedgerMapType::lmACCOUNT_STATE);
                    peerSet_->sendRequest(request, peer);
                }
                else
                {
                    JLOG(journal_.trace()) << "Add a no feature peer "
                                           << peer->id() << " for " << hash_;
                    if (++noFeaturePeerCount_ >=
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
        inboundLedgers_.acquire(hash_, 0, InboundLedger::Reason::GENERIC);
}

void
SkipListAcquire::onTimer(bool progress, ScopedLockType& sl)
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
SkipListAcquire::pmDowncast()
{
    return shared_from_this();
}

void
SkipListAcquire::processData(
    std::uint32_t ledgerSeq,
    boost::intrusive_ptr<SHAMapItem const> const& item)
{
    assert(ledgerSeq != 0 && item);
    ScopedLockType sl(mtx_);
    if (isDone())
        return;

    JLOG(journal_.trace()) << "got data for " << hash_;
    try
    {
        if (auto sle =
                std::make_shared<SLE>(SerialIter{item->slice()}, item->key());
            sle)
        {
            if (auto const& skipList = sle->getFieldV256(sfHashes).value();
                !skipList.empty())
                onSkipListAcquired(skipList, ledgerSeq, sl);
            return;
        }
    }
    catch (...)
    {
    }

    failed_ = true;
    JLOG(journal_.error()) << "failed to retrieve Skip list from verified data "
                           << hash_;
    notify(sl);
}

void
SkipListAcquire::addDataCallback(OnSkipListDataCB&& cb)
{
    ScopedLockType sl(mtx_);
    dataReadyCallbacks_.emplace_back(std::move(cb));
    if (isDone())
    {
        JLOG(journal_.debug())
            << "task added to a finished SkipListAcquire " << hash_;
        notify(sl);
    }
}

std::shared_ptr<SkipListAcquire::SkipListData const>
SkipListAcquire::getData() const
{
    ScopedLockType sl(mtx_);
    return data_;
}

void
SkipListAcquire::retrieveSkipList(
    std::shared_ptr<Ledger const> const& ledger,
    ScopedLockType& sl)
{
    if (auto const hashIndex = ledger->read(keylet::skip());
        hashIndex && hashIndex->isFieldPresent(sfHashes))
    {
        auto const& slist = hashIndex->getFieldV256(sfHashes).value();
        if (!slist.empty())
        {
            onSkipListAcquired(slist, ledger->seq(), sl);
            return;
        }
    }

    failed_ = true;
    JLOG(journal_.error()) << "failed to retrieve Skip list from a ledger "
                           << hash_;
    notify(sl);
}

void
SkipListAcquire::onSkipListAcquired(
    std::vector<uint256> const& skipList,
    std::uint32_t ledgerSeq,
    ScopedLockType& sl)
{
    complete_ = true;
    data_ = std::make_shared<SkipListData>(ledgerSeq, skipList);
    JLOG(journal_.debug()) << "Skip list acquired " << hash_;
    notify(sl);
}

void
SkipListAcquire::notify(ScopedLockType& sl)
{
    assert(isDone());
    std::vector<OnSkipListDataCB> toCall;
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
