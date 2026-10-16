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
     * Takes mtx_, the lock that reaches the verdict, so the charge for
     * declined data is issued here. A node that leaves the map invalid also
     * fails the acquisition. A late reply is bounded by the per-peer allowance
     * in lateReplyGranted_.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @return The tally of useful, duplicate, and bad nodes in the batch.
     *         Useful and bad can both be nonzero.
     */
    [[nodiscard]] SHAMapAddNode
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
     * Takes mtx_. Clamps the timeout count, then clears the failed flag and
     * restarts the timer if the map is still valid. An invalid map stays
     * failed. See SHAMap::addKnownNode for why that verdict is final.
     *
     * @return Whether the set is still worth keeping, which is false once its
     *         map is invalid.
     */
    [[nodiscard]] bool
    stillNeed();

protected:
    // Protected so a test subclass can read the map's state.
    std::shared_ptr<SHAMap> map_;

private:
    bool haveRoot_{false};

    /**
     * Every peer a request has actually been sent to, which is what earns a
     * peer the free late-reply pass. trigger() records a peer where it builds
     * the request, so selection alone does not enroll one. Survives a
     * stillNeed() revival. Guarded by mtx_.
     */
    std::set<Peer::ID> requestedPeers_;

    /**
     * Peers in requestedPeers_ that have spent the free late reply their last
     * request earned. One unspent pass per peer, renewed by each request sent
     * to it, so overlapping requests to one peer share one pass. recordAsked()
     * renews a pass wherever it records a request, for a targeted request and
     * for a broadcast alike, so a revival on its own renews nothing. Guarded
     * by mtx_.
     */
    std::set<Peer::ID> lateReplyGranted_;

    /**
     * Consecutive timer intervals a duplicate-only reply has postponed, with no
     * useful node in between. Bounded by kMaxDuplicateCredits, so the timeout
     * count always resumes. Reset by a batch that advances the set, and by
     * stillNeed() on revival.
     */
    int duplicateCredits_{0};

    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Add nodes a peer sent us, on the lock takeNodes() holds. Reached only for
     * an acquisition still running.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @param sl Proof mtx_ is held.
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

    /**
     * Whether a request has ever gone to this peer, in any round. Call under
     * mtx_.
     *
     * @param peer The peer to ask about, which must be non-null.
     * @return Whether a request was sent to this peer.
     */
    [[nodiscard]] bool
    hasAsked(std::shared_ptr<Peer> const& peer) const;

    void
    trigger(std::shared_ptr<Peer> const&);
    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;
};

}  // namespace xrpl
