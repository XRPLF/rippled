//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/ledger/ConsensusTransSetSF.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/ProtocolMessage.h>

#include <memory>

namespace ripple {

using namespace std::chrono_literals;

// Timeout interval in milliseconds
auto constexpr TX_ACQUIRE_TIMEOUT = 250ms;

// If set, we have acquired the root of the TX SHAMap
static constexpr std::uint8_t const TX_HAVE_ROOT = 0x01;

static constexpr std::uint16_t const TX_NORM_TIMEOUTS = 4;
static constexpr std::uint16_t const TX_MAX_TIMEOUTS = 20;

TransactionAcquire::TransactionAcquire(
    Application& app,
    uint256 const& hash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          hash,
          TX_ACQUIRE_TIMEOUT,
          {jtTXN_DATA, 0, "TransactionAcquire"},
          0,
          app.journal("TransactionAcquire"))
    , mPeerSet(std::move(peerSet))
{
    mMap = std::make_shared<SHAMap>(
        SHAMapType::TRANSACTION, hash, app_.getNodeFamily());
    mMap->setUnbacked();
}

void
TransactionAcquire::done()
{
    if (hasFailed())
    {
        JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
        return;
    }

    JLOG(journal_.debug()) << "Acquired TX set " << hash_;
    mMap->setImmutable();

    // If we are in the process of shutting down, the job queue may refuse
    // to queue the job; that's fine.
    app_.getJobQueue().addJob(
        jtTXN_DATA,
        "completeAcquire",
        [&app = app_, hash = hash_, map = mMap]() {
            app.getInboundTransactions().giveSet(hash, map, true);
        });
}

void
TransactionAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (timeouts_ > TX_MAX_TIMEOUTS)
    {
        markFailed();
        done();
        return;
    }

    if (timeouts_ >= TX_NORM_TIMEOUTS)
        trigger(nullptr);

    addPeers(1);
}

std::weak_ptr<TimeoutCounter>
TransactionAcquire::pmDowncast()
{
    return shared_from_this();
}

void
TransactionAcquire::trigger(std::shared_ptr<Peer> const& peer)
{
    if (hasCompleted())
    {
        JLOG(journal_.info()) << "trigger after complete";
        return;
    }
    if (hasFailed())
    {
        JLOG(journal_.info()) << "trigger after fail";
        return;
    }

    if (!(userdata_ & TX_HAVE_ROOT))
    {
        JLOG(journal_.trace()) << "TransactionAcquire::trigger "
                               << (peer ? "havePeer" : "noPeer") << " no root";
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash(hash_.begin(), hash_.size());
        tmGL.set_itype(protocol::liTS_CANDIDATE);
        tmGL.set_querydepth(3);  // We probably need the whole thing

        if (timeouts_ != 0)
            tmGL.set_querytype(protocol::qtINDIRECT);

        *(tmGL.add_nodeids()) = SHAMapNodeID().getRawString();
        mPeerSet->sendRequest(tmGL, peer);
    }
    else if (!mMap->isValid())
    {
        markFailed();
        done();
    }
    else
    {
        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());
        auto nodes = mMap->getMissingNodes(256, &sf);

        if (nodes.empty())
        {
            if (mMap->isValid())
                markComplete();
            else
                markFailed();

            done();
            return;
        }

        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash(hash_.begin(), hash_.size());
        tmGL.set_itype(protocol::liTS_CANDIDATE);

        if (timeouts_ != 0)
            tmGL.set_querytype(protocol::qtINDIRECT);

        for (auto const& node : nodes)
        {
            *tmGL.add_nodeids() = node.first.getRawString();
        }
        mPeerSet->sendRequest(tmGL, peer);
    }
}

SHAMapAddNode
TransactionAcquire::takeNodes(
    std::vector<std::pair<SHAMapNodeID, Slice>> const& data,
    std::shared_ptr<Peer> const& peer)
{
    ScopedLockType sl(mtx_);

    if (hasCompleted())
    {
        JLOG(journal_.trace()) << "TX set complete";
        return {};
    }

    if (hasFailed())
    {
        JLOG(journal_.trace()) << "TX set failed";
        return {};
    }

    try
    {
        if (data.empty())
            return SHAMapAddNode::invalid();

        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());

        for (auto const& d : data)
        {
            if (d.first.isRoot())
            {
                if (userdata_ & TX_HAVE_ROOT)
                    JLOG(journal_.debug())
                        << "Got root TXS node, already have it";
                else if (!mMap->addRootNode(
                                  SHAMapHash{hash_}, d.second, nullptr)
                              .isGood())
                {
                    JLOG(journal_.warn()) << "TX acquire got bad root node";
                }
                else
                    userdata_ |= TX_HAVE_ROOT;
            }
            else if (!mMap->addKnownNode(d.first, d.second, &sf).isGood())
            {
                JLOG(journal_.warn()) << "TX acquire got bad non-root node";
                return SHAMapAddNode::invalid();
            }
        }

        trigger(peer);
        makeProgress();
        return SHAMapAddNode::useful();
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error())
            << "Peer " << peer->id()
            << " sent us junky transaction node data: " << ex.what();
        return SHAMapAddNode::invalid();
    }
}

void
TransactionAcquire::addPeers(std::size_t limit)
{
    mPeerSet->addPeers(
        limit,
        [this](auto peer) { return peer->hasTxSet(hash_); },
        [this](auto peer) { trigger(peer); });
}

void
TransactionAcquire::init(int numPeers)
{
    ScopedLockType sl(mtx_);

    addPeers(numPeers);

    setTimer(sl);
}

void
TransactionAcquire::stillNeed()
{
    ScopedLockType sl(mtx_);

    if (timeouts_ > TX_NORM_TIMEOUTS)
        timeouts_ = TX_NORM_TIMEOUTS;
}

}  // namespace ripple
