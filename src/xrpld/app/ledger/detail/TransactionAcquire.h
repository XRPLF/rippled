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
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them.
     * @return The tally of useful, duplicate, and bad nodes in the batch. Useful and
     *         bad can both be nonzero, since only the node the batch stops on is
     *         bad.
     */
    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer);

    void
    init(int startPeers);

    /**
     * Resume a timed-out acquisition, or leave a running one alone.
     *
     * Always clamps the timeout count. An acquisition that failed has its timer
     * chain stopped, so this also clears the failed flag and restarts the timer;
     * one that is still running already has a timer pending.
     */
    void
    stillNeed();

protected:
    // Kept protected so a test subclass (see TransactionAcquire_test) can read the map's state.
    // Production callers reach a set through InboundTransactions.
    std::shared_ptr<SHAMap> map_;

private:
    bool haveRoot_{false};
    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Add nodes a peer sent us, on the lock takeNodes() holds.
     *
     * Split out so recording what the batch achieved happens on one exit rather
     * than on each of the several this has, including the ones that stop the batch
     * early.
     *
     * @param data The nodes to add, each with its claimed position.
     * @param peer The peer that sent them.
     * @return The tally of useful, duplicate, and bad nodes in the batch.
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
     * mtx_. Runs once per outcome, and a stillNeed() revival gives one object a
     * second outcome.
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
