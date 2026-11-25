#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace xrpl {

// VFALCO TODO rename to PeerTxRequest
// A transaction set we are trying to acquire
class TransactionAcquire : public TimeoutCounter,
                           public std::enable_shared_from_this<TransactionAcquire>,
                           public CountedObject<TransactionAcquire>
{
public:
    using pointer = std::shared_ptr<TransactionAcquire>;

    /**
     * How long to wait between retries, and so how long one timeout takes.
     */
    static constexpr std::chrono::milliseconds kRetryInterval{250};

    /**
     * @param app The application to run in.
     * @param hash The set to acquire.
     * @param peerSet Which peers to ask, and how to reach them.
     * @param retryInterval How long to wait between retries. TimeoutCounter
     *        requires more than 10ms and less than 30s.
     */
    TransactionAcquire(
        Application& app,
        UInt256 const& hash,
        std::unique_ptr<PeerSet> peerSet,
        std::chrono::milliseconds retryInterval = kRetryInterval);
    ~TransactionAcquire() override = default;

    /**
     * Add nodes a peer sent us to the set we are acquiring.
     *
     * Charges the peer for data it declines, since only this function holds the
     * lock that decides the tier. A node that leaves the map invalid also fails
     * the acquisition; see SHAMap::addKnownNode for why that verdict is final.
     * A late reply is bounded by a per-peer allowance; see
     * lateReplyGranted_.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @return The tally of useful, duplicate, and bad nodes in the batch.
     *         Useful and bad can both be nonzero, since only the node the
     *         batch stops on is bad.
     */
    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer);

    /**
     * Whether a reply from this peer is worth deserializing.
     *
     * Takes mtx_ and spends the peer's late-reply allowance. The answer can go
     * stale, since mtx_ is released before takeNodes(), which recognizes a late
     * reply of its own accord.
     *
     * @param peer The peer that sent the reply, charged here when the reply is
     *        outside the allowance.
     * @return Whether the reply should be parsed and handed to takeNodes().
     */
    [[nodiscard]] bool
    wantsReplyFrom(std::shared_ptr<Peer> const& peer);

    void
    init(int startPeers);

    /**
     * Resume a timed-out acquisition, or leave it alone.
     *
     * Always clamps the timeout count. An acquisition that failed with
     * its map still valid has its timer chain stopped, so this also
     * clears the failed flag and restarts the timer. One that failed
     * because its map went invalid stays failed; see
     * SHAMap::addKnownNode for why that verdict holds for every peer.
     *
     * @return Whether the set is still worth keeping. False for one whose
     *         map went invalid, so the caller stops refreshing the window
     *         that decides when it is swept.
     */
    [[nodiscard]] bool
    stillNeed();

protected:
    // Kept protected so a test subclass can read the map's state.
    // Production callers reach a set through InboundTransactions.
    std::shared_ptr<SHAMap> map_;

private:
    bool haveRoot_{false};

    /**
     * Every peer a request has actually been sent to.
     *
     * Holds the peers addPeers() selected and trigger() then built a request
     * for, the unsolicited senders takeNodesLocked() trigger()s directly
     * outside addPeers(), and the whole of peerSet_->getPeerIds() once a
     * broadcast has gone out. Selection alone does not enroll a peer: trigger()
     * records it at the two branches that build a request, so one reached while
     * the map is invalid, while nothing is missing, or after the acquisition
     * settled is left out. Recording every peer a request went to, however it
     * was chosen, bounds the free allowance to peers with a reply in flight.
     * stillNeed() keeps this set, since a peer already asked stays asked
     * whichever round its reply arrives in.
     */
    std::set<Peer::ID> requestedPeers_;

    /**
     * Peers in requestedPeers_ that have already spent the free late reply
     * their last request earned.
     *
     * Membership rather than a count, so each peer's pass is its own: a
     * peer already in requestedPeers_ is granted one free late reply the
     * first time it reaches takeNodesLocked()'s isDone() branch, and is
     * charged on every later one. recordAsked() renews a peer's pass wherever
     * it records a request, for a targeted request and for a broadcast alike.
     * So each peer holds one unspent pass, renewed by each request sent to it,
     * and overlapping requests to one peer share one pass. A revival on its
     * own renews nothing.
     */
    std::set<Peer::ID> lateReplyGranted_;

    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Add nodes a peer sent us, on the lock takeNodes() holds.
     *
     * Split out so recording what the batch achieved happens on one exit,
     * covering the paths that stop the batch early as well.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @return The tally of useful, duplicate, and bad nodes in the batch.
     */
    SHAMapAddNode
    takeNodesLocked(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer,
        ScopedLockType& sl);

    /**
     * Spend this peer's one free late reply, or charge it for replaying. Only
     * one of wantsReplyFrom() and takeNodes() sees any given reply, so a reply
     * is charged once.
     *
     * @param peer The peer that sent the reply.
     * @param sl Proof mtx_ is held, which the allowance sets require.
     */
    void
    chargeLateReply(std::shared_ptr<Peer> const& peer, ScopedLockType& sl);

    void
    onTimer(bool progress, ScopedLockType& sl) override;

    /**
     * Settle the acquired set and hand it on, or report the failure. Call under
     * mtx_. Runs once per outcome, and a stillNeed() revival gives one object a
     * second outcome.
     */
    void
    done();

    void
    addPeers(std::size_t limit);

    /**
     * Record every peer a request has just gone to, and renew each one's free
     * late reply. Call under mtx_.
     *
     * The renewal gives each peer one unspent pass, renewed by each request
     * sent to it, so overlapping requests to one peer share one pass. See
     * lateReplyGranted_.
     *
     * @param peer The peer a targeted request went to, or nullptr for a
     *        broadcast, which reaches every peer the set tracks.
     */
    void
    recordAsked(std::shared_ptr<Peer> const& peer);

    void
    trigger(std::shared_ptr<Peer> const&);
    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;
};

}  // namespace xrpl
