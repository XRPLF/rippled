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

    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer);

    void
    init(int startPeers);

    void
    stillNeed();

protected:
    // Kept protected so a test subclass (see TransactionAcquire_test) can read the map's state,
    // which nothing else publishes. Production callers reach a set through InboundTransactions.
    std::shared_ptr<SHAMap> map_;

private:
    bool haveRoot_{false};
    std::unique_ptr<PeerSet> peerSet_;

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
