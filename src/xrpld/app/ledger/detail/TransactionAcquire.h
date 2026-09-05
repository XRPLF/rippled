#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
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
     * How long to wait between retries, and so how long each timeout counted
     * against the acquisition takes. Short, since a set is wanted for the
     * consensus round that asked for it or not at all.
     */
    static constexpr std::chrono::milliseconds kRetryInterval{250};

    /**
     * @param app The application to run in.
     * @param hash The set to acquire.
     * @param peerSet Which peers to ask, and how to reach them.
     * @param retryInterval How long to wait between retries. Defaulted in
     *        production; TransactionAcquire_test passes a short one so a whole
     *        timeout chain runs in a fraction of the time. TimeoutCounter
     *        requires more than 10ms.
     */
    TransactionAcquire(
        Application& app,
        uint256 const& hash,
        std::unique_ptr<PeerSet> peerSet,
        std::chrono::milliseconds retryInterval = kRetryInterval);
    ~TransactionAcquire() override = default;

    /**
     * Add nodes a peer sent us to the set we are acquiring.
     *
     * Charges the peer for data it declines, since the fee depends on
     * whether the map stayed sound and on whether we were still asking for
     * the set, and only this function holds the lock that decides either.
     * A node that leaves the map invalid also fails the acquisition; see
     * SHAMap::addKnownNode for why that verdict is final. A reply arriving
     * after the set was settled is free once per peer we asked, since that
     * many can be in flight; beyond that the sender is replaying.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @return The tally of useful, unwanted, and bad nodes in the batch.
     *         Useful and bad can both be nonzero, since only the node the
     *         batch stops on is bad.
     */
    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer);

    void
    init(int startPeers);

    /**
     * Resume a timed-out acquisition, or leave it alone.
     *
     * Always clamps the timeout count. An acquisition that failed with
     * its map still valid has its timer chain stopped, so this also
     * clears the failed flag and restarts the timer. One that failed
     * because its map went invalid cannot be satisfied by any peer (see
     * SHAMap::addKnownNode), so it stays failed.
     *
     * @return Whether the set is still worth keeping. False only for one that
     *         cannot be revived, so the caller stops refreshing the window that
     *         decides when it is swept.
     */
    [[nodiscard]] bool
    stillNeed();

protected:
    // Kept protected so a test subclass (see TransactionAcquire_test) can read the map's state,
    // which nothing else publishes. Production callers reach a set through InboundTransactions.
    std::shared_ptr<SHAMap> map_;

private:
    bool haveRoot_{false};

    /**
     * The peers a targeted request has actually been sent to.
     *
     * Broader than peerSet_->getPeerIds(): that only tracks peers addPeers()
     * selected itself, but takeNodesLocked() can also trigger() an
     * unsolicited sender directly, which sends a targeted request without
     * ever going through addPeers(). Recording every peer a request went to,
     * however it was chosen, is what bounds the free allowance to peers who
     * could actually have a reply in flight. Not reset by stillNeed(): a
     * peer already asked stays one we asked, whichever round its reply
     * arrives in.
     */
    std::set<Peer::id_t> requestedPeers_;

    /**
     * Peers in requestedPeers_ that have already spent this round's free
     * late reply.
     *
     * Membership, not a count: a shared counter compared against
     * requestedPeers_.size() would let one peer's replayed replies exhaust
     * the allowance a different, honest peer in requestedPeers_ is still
     * owed, since a count does not know whose slot it is spending.
     * Charging is refused only the first time a peer already in
     * requestedPeers_ shows up in takeNodesLocked()'s isDone() branch, so
     * each peer's pass is its own. Reset by stillNeed() on revival, since a
     * fresh round re-broadcasts to every peer in requestedPeers_ and so
     * owes each of them a fresh pass.
     */
    std::set<Peer::id_t> lateReplyGranted_;

    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Add nodes a peer sent us, on the lock takeNodes() holds.
     *
     * Split out so recording what the batch achieved happens on one exit
     * rather than on each of the several this has, including the ones
     * that stop the batch early.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them, charged here if the data is
     *        declined.
     * @return The tally of useful, unwanted, and bad nodes in the batch.
     */
    SHAMapAddNode
    takeNodesLocked(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer,
        ScopedLockType&);

    void
    onTimer(bool progress, ScopedLockType& sl) override;

    /**
     * Settle the acquired set and hand it on, or report the failure. Call under
     * mtx_.
     *
     * Runs at most once per outcome rather than once in total, since
     * stillNeed() can revive a timed-out acquisition that then finishes.
     */
    void
    done();

    void
    addPeers(std::size_t limit);

    void
    trigger(std::shared_ptr<Peer> const&);
    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;
};

}  // namespace xrpl
