#include <xrpld/app/ledger/detail/TransactionAcquire.h>

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/Job.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

using namespace std::chrono_literals;

// Timeout interval in milliseconds
constexpr auto kTxAcquireTimeout = 250ms;

static constexpr auto kNormTimeouts = 4;
static constexpr auto kMaxTimeouts = 20;

TransactionAcquire::TransactionAcquire(
    Application& app,
    uint256 const& hash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          hash,
          kTxAcquireTimeout,
          {.jobType = JtTxnData, .jobName = "TxAcq", .jobLimit = {}},
          app.getJournal("TransactionAcquire"))
    , peerSet_(std::move(peerSet))
{
    map_ = std::make_shared<SHAMap>(SHAMapType::TRANSACTION, hash, app_.getNodeFamily());
    map_->setUnbacked();
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
        map_->setImmutable();

        uint256 const& hash(hash_);
        std::shared_ptr<SHAMap> const& map(map_);
        auto const pap = &app_;
        // Note that, when we're in the process of shutting down, addJob()
        // may reject the request.  If that happens then giveSet() will
        // not be called.  That's fine.  According to David the giveSet() call
        // just updates the consensus and related structures when we acquire
        // a transaction set. No need to update them if we're shutting down.
        app_.getJobQueue().addJob(JtTxnData, "ComplAcquire", [pap, hash, map]() {
            pap->getInboundTransactions().giveSet(hash, map, true);
        });
    }
}

void
TransactionAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (timeouts_ > kMaxTimeouts)
    {
        failed_ = true;
        done();
        return;
    }

    if (timeouts_ >= kNormTimeouts)
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

    if (!haveRoot_)
    {
        JLOG(journal_.trace()) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer")
                               << " no root";
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash(hash_.begin(), hash_.size());
        tmGL.set_itype(protocol::liTS_CANDIDATE);
        tmGL.set_querydepth(3);  // We probably need the whole thing

        if (timeouts_ != 0)
            tmGL.set_querytype(protocol::qtINDIRECT);

        *(tmGL.add_nodeids()) = SHAMapNodeID().getRawString();
        peerSet_->sendRequest(tmGL, peer);
    }
    else if (!map_->isValid())
    {
        failed_ = true;
        done();
    }
    else
    {
        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());
        auto nodes = map_->getMissingNodes(256, &sf);

        if (nodes.empty())
        {
            if (map_->isValid())
            {
                complete_ = true;
            }
            else
            {
                failed_ = true;
            }

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
        peerSet_->sendRequest(tmGL, peer);
    }
}

SHAMapAddNode
TransactionAcquire::takeNodes(
    std::vector<std::pair<SHAMapNodeID, Slice>> const& data,
    std::shared_ptr<Peer> const& peer)
{
    ScopedLockType const sl(mtx_);

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
                if (haveRoot_)
                {
                    JLOG(journal_.debug()) << "Got root TXS node, already have it";
                }
                else if (!map_->addRootNode(SHAMapHash{hash_}, d.second, nullptr).isGood())
                {
                    JLOG(journal_.warn()) << "TX acquire got bad root node";
                }
                else
                {
                    haveRoot_ = true;
                }
            }
            else if (!map_->addKnownNode(d.first, d.second, &sf).isGood())
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
        JLOG(journal_.error()) << "Peer " << peer->id()
                               << " sent us junky transaction node data: " << ex.what();
        return SHAMapAddNode::invalid();
    }
}

void
TransactionAcquire::addPeers(std::size_t limit)
{
    peerSet_->addPeers(
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
    ScopedLockType const sl(mtx_);

    timeouts_ = std::min<int>(timeouts_, kNormTimeouts);
    failed_ = false;
}

}  // namespace xrpl
