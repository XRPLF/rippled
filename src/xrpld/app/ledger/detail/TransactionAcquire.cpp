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
    UInt256 const& hash,
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
        // nothing walks this map with the lock released, so it is still valid. UNREACHABLE for
        // that reason, with the flags still withdrawn, since UNREACHABLE need not stop here.
        // LCOV_EXCL_START
        // Withdraw complete_ alongside the failure, for trigger() and takeNodes(), which both
        // check complete_ before failed_.
        complete_ = false;
        failed_ = true;
        JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
        UNREACHABLE("xrpl::TransactionAcquire::done : map is invalid");
        // LCOV_EXCL_STOP
    }
    else
    {
        JLOG(journal_.debug()) << "Acquired TX set " << hash_;

        UInt256 const& hash(hash_);
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
TransactionAcquire::recordAsked(std::shared_ptr<Peer> const& peer)
{
    // Each request renews the pass chargeLateReply() reads, so a peer answering the request
    // just sent to it is free whatever it answered in an earlier round.
    if (peer)
    {
        requestedPeers_.insert(peer->id());
        lateReplyGranted_.erase(peer->id());
        return;
    }

    // A broadcast goes to every peer the set tracks, so each of them has been asked.
    auto const& ids = peerSet_->getPeerIds();
    requestedPeers_.insert(ids.begin(), ids.end());
    for (auto const id : ids)
        lateReplyGranted_.erase(id);
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
        recordAsked(peer);
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
        recordAsked(peer);
        peerSet_->sendRequest(tmGL, peer);
    }
}

SHAMapAddNode
TransactionAcquire::takeNodes(
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
    std::shared_ptr<Peer> const& peer)
{
    ScopedLockType sl(mtx_);

    // Read before the call below, which can settle the set itself.
    bool const wasSettled = isDone();

    auto const san = takeNodesLocked(std::move(data), peer, sl);

    // A batch that advanced the map must keep the next timer tick from counting a timeout against
    // it. A duplicate counts as an answer: an honest second responder to trigger()'s fan-out has
    // replied, so no timeout is owed. A reply to a set already settled on entry owes nothing,
    // since the allowance it spends belongs to the round that ended.
    if (!wasSettled && (san.isUseful() || san.getDuplicate() > 0))
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

        // Reported as a duplicate while the map is still valid, and as bad once an invalid map
        // is what failed the set, since that verdict holds for every later reply on this hash.
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
                    // addRootNode rejects a hash mismatch and leaves the map valid, so the timer
                    // owns the retry and picks another peer.
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
                    // The acquisition fails here, and stillNeed() keeps it failed. See
                    // SHAMap::addKnownNode for why that verdict holds for every peer. Charged
                    // kFeeMalformedData under the lock that reached the verdict, so the packet
                    // that earned the fee is the one that pays it. A deterrent only, since the
                    // same node reaches a map by paths with no sender to charge.
                    peer->charge(resource::kFeeMalformedData, "tx_set node makes map invalid");
                    failed_ = true;
                    done();

                    // The whole batch is discarded, since the nodes ahead of the bad one belong to
                    // the same impossible tree, and the acquisition is over.
                    return SHAMapAddNode::invalid();
                }

                // Any other bad node leaves the map sound, so the timer owns the retry and picks
                // another peer.
                peer->charge(resource::kFeeInvalidData, "tx_set node invalid");
                return san;
            }
        }

        trigger(peer);
        return san;
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "TX acquire threw while taking nodes for TX set " << hash_
                               << " from peer " << peer->id() << ": " << ex.what();
        // Whatever the batch hooked in before the throw stands, so the tally it reached is what
        // the caller is told. The timer owns the retry. The sender keeps its fee, since the
        // classes that reach here are raised by this code rather than by the data.
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

    // A running acquisition keeps the wait it has, rather than restarting it for every consensus
    // round that asks for the set again.
    if (!failed_)
        return true;

    // An invalid map keeps the acquisition failed, since that verdict holds for every peer (see
    // SHAMap::addKnownNode). Reported so the caller stops refreshing this set's retention
    // window.
    if (!map_->isValid())
        return false;

    failed_ = false;

    // lateReplyGranted_ is left alone. The free allowance is earned one request at a time, so a
    // peer keeps the pass it already spent until recordAsked() records another request to it. The
    // timer restarted below is what sends those requests.

    // Restarting the timer is what resumes the acquisition. expires_after() cancels whatever wait
    // was outstanding, so the timer holds at most one wait at a time. A job queueJob() already
    // handed to the JobQueue is not canceled by that and still runs one invokeOnTimer(), which
    // re-arms this same timer and so folds back into the one chain.
    setTimer(sl);
    return true;
}

}  // namespace xrpl
