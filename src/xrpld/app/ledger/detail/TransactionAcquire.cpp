#include <xrpld/app/ledger/detail/TransactionAcquire.h>

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

static constexpr auto kNormTimeouts = 4;
static constexpr auto kMaxTimeouts = 20;

TransactionAcquire::TransactionAcquire(
    Application& app,
    uint256 const& hash,
    std::unique_ptr<PeerSet> peerSet,
    std::chrono::milliseconds retryInterval)
    : TimeoutCounter(
          app,
          hash,
          retryInterval,
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
    // mtx_ is held, so this may only post real work rather than do it.
    if (failed_)
    {
        JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
    }
    else if (!map_->setImmutable())
    {
        // trigger() verified the map before setting complete_ and mtx_ has been held since, and
        // unlike InboundLedger nothing walks this map with the lock released, so nothing can have
        // invalidated it. UNREACHABLE for that reason, with the flags still withdrawn, since
        // UNREACHABLE is not guaranteed to stop here.
        // LCOV_EXCL_START
        // Withdraw complete_ alongside the failure, or trigger() and takeNodes() - which both check
        // complete_ before failed_ - keep treating this as delivered while consensus waits on a set
        // giveSet() never hands over.
        complete_ = false;
        failed_ = true;
        JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
        UNREACHABLE("xrpl::TransactionAcquire::done : map is invalid");
        // LCOV_EXCL_STOP
    }
    else
    {
        JLOG(journal_.debug()) << "Acquired TX set " << hash_;

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
TransactionAcquire::onTimer(bool progress, ScopedLockType&)
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
        if (peer)
            requestedPeers_.insert(peer->id());
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
        if (peer)
            requestedPeers_.insert(peer->id());
        peerSet_->sendRequest(tmGL, peer);
    }
}

SHAMapAddNode
TransactionAcquire::takeNodes(
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
    std::shared_ptr<Peer> const& peer)
{
    ScopedLockType sl(mtx_);

    auto const san = takeNodesLocked(std::move(data), peer, sl);

    // A batch that advanced the map must keep the next timer tick from counting a timeout against
    // it. A duplicate counts as an answer: an honest second responder to trigger()'s fan-out has
    // replied, so no timeout is owed.
    if (san.isUseful() || san.getDuplicate() > 0)
        progress_ = true;

    return san;
}

SHAMapAddNode
TransactionAcquire::takeNodesLocked(
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
    std::shared_ptr<Peer> const& peer,
    ScopedLockType& sl)
{
    // A reply that arrives after the set is settled - by completing it, or by a different packet
    // failing it. trigger() sends to every peer it was given, so any of their replies, including
    // another packet from the same peer whose data failed the set, can already be in flight and
    // could not have known the outcome. Those are solicited, and free: one per peer we asked,
    // which is what bounds the honest case.
    //
    // Past that bound, further data for this hash is a replay - a resend of data already
    // accepted or now known worthless, not a first-time reply - and serving it is not free
    // work, so it is charged.
    if (isDone())
    {
        JLOG(journal_.trace()) << (complete_ ? "TX set complete" : "TX set failed");

        chargeLateReply(peer, sl);

        // Reported as a duplicate rather than as bad, unless the map itself is why the set failed:
        // no reply for such a hash can ever be useful to anyone.
        return map_->isValid() ? SHAMapAddNode::duplicate() : SHAMapAddNode::invalid();
    }

    // Accumulated across the batch, so a packet ending in one bad node still counts the nodes
    // hooked in ahead of it, as InboundLedger::receiveNode() already does.
    SHAMapAddNode san;

    try
    {
        if (data.empty())
        {
            // Defensive: PeerImp rejects an empty node list before dispatch.
            peer->charge(resource::kFeeInvalidData, "tx_set empty");
            return SHAMapAddNode::invalid();
        }

        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());

        for (auto& d : data)
        {
            if (d.first.isRoot())
            {
                if (haveRoot_)
                {
                    JLOG(journal_.debug()) << "Got root TXS node, already have it";
                    san.incDuplicate();
                    continue;
                }

                auto const result =
                    map_->addRootNode(SHAMapHash{hash_}, std::move(d.second), nullptr);
                san += result;

                if (!result.isGood())
                {
                    JLOG(journal_.warn()) << "TX acquire got bad root node for TX set " << hash_
                                          << " from peer " << peer->id();
                    // addRootNode only rejects a hash mismatch, which never invalidates the map,
                    // so there is nothing to fail here: the timer will retry with another peer.
                    peer->charge(resource::kFeeInvalidData, "tx_set root hash mismatch");
                    return san;
                }

                haveRoot_ = true;
                continue;
            }

            auto const result = map_->addKnownNode(d.first, std::move(d.second), &sf);
            san += result;

            if (!result.isGood())
            {
                JLOG(journal_.warn()) << "TX acquire got bad non-root node " << d.first
                                      << " for TX set " << hash_ << " from peer " << peer->id();
                if (!map_->isValid())
                {
                    // No peer can complete this hash (see SHAMap::addKnownNode), so fail the
                    // acquisition rather than retrying; stillNeed() will not revive it either.
                    // Charged more harshly than data that is merely wrong, and charged here, under
                    // the lock that reached the verdict, so a concurrent packet cannot decide this
                    // peer's fee. A deterrent rather than a control even so: such a node can reach
                    // a map by paths with no peer to charge (see SHAMap::addKnownNode), so nothing
                    // may rely on the sender having paid.
                    peer->charge(resource::kFeeMalformedData, "tx_set node makes map invalid");
                    failed_ = true;
                    done();

                    // Nothing in this batch is worth counting: the nodes ahead of the bad one
                    // belong to a tree that cannot exist, and the acquisition is over.
                    return SHAMapAddNode::invalid();
                }

                // Any other bad node leaves the map sound, so leave that retry to the timer
                // rather than re-requesting from the peer that just sent us bad data.
                peer->charge(resource::kFeeInvalidData, "tx_set node invalid");
                return san;
            }
        }

        trigger(peer);
        return san;
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "Peer " << peer->id()
                               << " sent us junky transaction node data: " << ex.what();
        peer->charge(resource::kFeeInvalidData, "tx_set junky node data");
        san.incInvalid();
        return san;
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
TransactionAcquire::chargeLateReply(std::shared_ptr<Peer> const& peer, ScopedLockType&)
{
    if (!requestedPeers_.contains(peer->id()) || !lateReplyGranted_.insert(peer->id()).second)
        peer->charge(resource::kFeeUselessData, "tx_set data after the set was settled");
}

bool
TransactionAcquire::stillNeed()
{
    ScopedLockType sl(mtx_);

    timeouts_ = std::min<int>(timeouts_, kNormTimeouts);

    // Nothing to revive: leave a running acquisition on the wait it has, rather than restarting it
    // for every consensus round that asks for the set again.
    if (!failed_)
        return true;

    // An invalid map is not a timeout: no peer can complete such a hash (see
    // SHAMap::addKnownNode), so this one stays failed. Reported, so the caller stops holding its
    // retention window open for a set nothing can ever finish.
    if (!map_->isValid())
        return false;

    failed_ = false;

    // The free allowance in takeNodesLocked() belongs to the round that just failed, so a record
    // of who spent their pass must not carry into the new round and mischarge a reply it is still
    // owed.
    lateReplyGranted_.clear();

    // Restarting the timer is what resumes the acquisition. expires_after() cancels any pending
    // wait, so this cannot leave two timer chains running.
    setTimer(sl);
    return true;
}

}  // namespace xrpl
