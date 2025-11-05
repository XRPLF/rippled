#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>

#include <memory>

namespace ripple {

using namespace std::chrono_literals;

// Timeout interval in milliseconds
auto constexpr TX_ACQUIRE_TIMEOUT = 250ms;

enum {
    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};

TransactionAcquire::TransactionAcquire(
    Application& app,
    uint256 const& hash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          hash,
          TX_ACQUIRE_TIMEOUT,
          {jtTXN_DATA, "TransactionAcquire", {}},
          app.journal("TransactionAcquire"))
    , mHaveRoot(false)
    , mPeerSet(std::move(peerSet))
{
    mMap = std::make_shared<SHAMap>(
        SHAMapType::TRANSACTION, hash, app_.getNodeFamily());
    mMap->setUnbacked();
}

void
TransactionAcquire::done()
{
    // We hold a PeerSet lock and so cannot do real work here

    if (failed_)
    {
        JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
    }
    else
    {
        JLOG(journal_.debug()) << "Acquired TX set " << hash_;
        mMap->setImmutable();

        uint256 const& hash(hash_);
        std::shared_ptr<SHAMap> const& map(mMap);
        auto const pap = &app_;
        // Note that, when we're in the process of shutting down, addJob()
        // may reject the request.  If that happens then giveSet() will
        // not be called.  That's fine.  According to David the giveSet() call
        // just updates the consensus and related structures when we acquire
        // a transaction set. No need to update them if we're shutting down.
        app_.getJobQueue().addJob(
            jtTXN_DATA, "completeAcquire", [pap, hash, map]() {
                pap->getInboundTransactions().giveSet(hash, map, true);
            });
    }
}

void
TransactionAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (timeouts_ > MAX_TIMEOUTS)
    {
        failed_ = true;
        done();
        return;
    }

    if (timeouts_ >= NORM_TIMEOUTS)
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
    if (complete_)
    {
        JLOG(journal_.info()) << "trigger after complete";
        return;
    }
    if (failed_)
    {
        JLOG(journal_.info()) << "trigger after fail";
        return;
    }

    if (!mHaveRoot)
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
        failed_ = true;
        done();
    }
    else
    {
        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());
        auto nodes = mMap->getMissingNodes(256, &sf);

        if (nodes.empty())
        {
            if (mMap->isValid())
                complete_ = true;
            else
                failed_ = true;

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

    if (complete_)
    {
        JLOG(journal_.trace()) << "TX set complete";
        return SHAMapAddNode();
    }

    if (failed_)
    {
        JLOG(journal_.trace()) << "TX set failed";
        return SHAMapAddNode();
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
                if (mHaveRoot)
                    JLOG(journal_.debug())
                        << "Got root TXS node, already have it";
                else if (!mMap->addRootNode(
                                  SHAMapHash{hash_}, d.second, nullptr)
                              .isGood())
                {
                    JLOG(journal_.warn()) << "TX acquire got bad root node";
                }
                else
                    mHaveRoot = true;
            }
            else if (!mMap->addKnownNode(d.first, d.second, &sf).isGood())
            {
                JLOG(journal_.warn()) << "TX acquire got bad non-root node";
                return SHAMapAddNode::invalid();
            }
        }

        trigger(peer);
        progress_ = true;
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

    if (timeouts_ > NORM_TIMEOUTS)
        timeouts_ = NORM_TIMEOUTS;
    failed_ = false;
}

}  // namespace ripple
