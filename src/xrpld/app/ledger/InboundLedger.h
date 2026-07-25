#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

// A ledger we are trying to acquire
class InboundLedger final : public TimeoutCounter,
                            public std::enable_shared_from_this<InboundLedger>,
                            public CountedObject<InboundLedger>
{
public:
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

    // These are the reasons we might acquire a ledger
    enum class Reason {
        HISTORY,   // Acquiring past ledger
        GENERIC,   // Generic other reasons
        CONSENSUS  // We believe the consensus round requires this ledger
    };

    InboundLedger(
        Application& app,
        uint256 const& hash,
        std::uint32_t seq,
        Reason reason,
        clock_type&,
        std::unique_ptr<PeerSet> peerSet);

    ~InboundLedger() override;

    // Called when another attempt is made to fetch this same ledger
    void
    update(std::uint32_t seq);

    /**
     * Returns true if we got all the data.
     */
    bool
    isComplete() const
    {
        return complete_;
    }

    /**
     * Returns false if we failed to get the data.
     */
    bool
    isFailed() const
    {
        return failed_;
    }

    std::shared_ptr<Ledger const>
    getLedger() const
    {
        return ledger_;
    }

    std::uint32_t
    getSeq() const
    {
        return seq_;
    }

    bool
    checkLocal();
    void
    init(ScopedLockType& collectionLock);

    bool
    gotData(std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData> const&);

    using neededHash_t = std::pair<protocol::TMGetObjectByHash::ObjectType, uint256>;

    /**
     * Return a json::ValueType::Object.
     */
    json::Value
    getJson(int);

    void
    runData();

    void
    touch()
    {
        lastAction_ = clock_.now();
    }

    clock_type::time_point
    getLastAction() const
    {
        return lastAction_;
    }

    /**
     * Outstanding missing SHAMap nodes in one of this acquire's two trees.
     *
     * Refreshed by trigger() after each getMissingNodes() sweep, which already
     * computes the count as a byproduct of its walk — this accessor adds no
     * traversal of its own and does not take the acquire lock.
     *
     * Read by the telemetry observable-gauge callback (~10 s cadence). A count
     * that stays flat and non-zero across ticks means the acquire will never
     * finish; a shrinking count means it is slow but alive.
     *
     * @param type Which tree to report: SHAMapType::TRANSACTION selects the
     *        transaction tree, every other value selects the account-state tree.
     * @return Node count from the most recent sweep of that tree; 0 before the
     *         first sweep and after the tree completes.
     *
     * @note Thread-safe and lock-free: a relaxed atomic load. The reader
     *       tolerates a value one sweep out of date.
     */
    [[nodiscard]] int
    getMissingNodeCount(SHAMapType type) const noexcept;

    /**
     * Number of peer packets stashed in receivedData_ awaiting processing.
     *
     * A deep stash means node data is arriving faster than runData() can apply
     * it, which is a processing bottleneck rather than a peer-supply one.
     *
     * @return Current stash depth; 0 when nothing is pending.
     *
     * @note Thread-safe and lock-free: a relaxed atomic load of a counter
     *       mirrored on every push/drain, so it never blocks the receive path
     *       nor waits on receivedDataLock_.
     */
    [[nodiscard]] std::size_t
    getReceivedDataDepth() const noexcept;

private:
    enum class TriggerReason { Added, Reply, Timeout };

    void
    filterNodes(std::vector<std::pair<SHAMapNodeID, uint256>>& nodes, TriggerReason reason);

    void
    trigger(std::shared_ptr<Peer> const&, TriggerReason);

    std::vector<neededHash_t>
    getNeededHashes();

    void
    addPeers();

    void
    tryDB(NodeStore::Database& srcDB);

    void
    done();

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::size_t
    getPeerCount() const;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    int
    processData(std::shared_ptr<Peer> peer, protocol::TMLedgerData const& data);

    bool
    takeHeader(std::string const& data);

    void
    receiveNode(protocol::TMLedgerData const& packet, SHAMapAddNode&);

    bool
    takeTxRootNode(Slice const& data, SHAMapAddNode&);

    bool
    takeAsRootNode(Slice const& data, SHAMapAddNode&);

    std::vector<uint256>
    neededTxHashes(int max, SHAMapSyncFilter const* filter) const;

    std::vector<uint256>
    neededStateHashes(int max, SHAMapSyncFilter const* filter) const;

    /**
     * Re-publish the missing-node counts from the completion flags.
     *
     * Called under mtx_ wherever a tree flips to complete. A tree that needs no
     * more nodes must publish 0: otherwise the last sweep's count lingers and a
     * finished acquire keeps reporting a flat non-zero, which is exactly the
     * "permanently stuck" reading the gauge exists to detect. Idempotent, so it
     * is safe to call from every flip site.
     */
    void
    refreshMissingNodeCounts() noexcept;

    /**
     * Fold one processed batch into the acquire totals and emit its telemetry.
     *
     * Both processData() branches (header batch and node batch) finished with
     * the same three steps, so they share this one helper: mark progress, add to
     * stats_, and emit the per-outcome add-node counters.
     *
     * @param san Outcome tally for the batch just processed.
     * @return Number of good nodes in the batch, which is processData()'s
     *         "useful data from this peer" return value.
     *
     * @note Called once per received packet, after the per-node loop inside
     *       receiveNode() has completed. The tallies are already aggregated, so
     *       the counters are emitted once per batch and never per node.
     * @note Call with mtx_ held, as both call sites already do.
     */
    int
    recordBatchOutcome(SHAMapAddNode const& san);

    clock_type& clock_;
    clock_type::time_point lastAction_;

    std::shared_ptr<Ledger> ledger_;
    bool haveHeader_{false};
    bool haveState_{false};
    bool haveTransactions_{false};
    bool signaled_{false};
    bool byHash_{true};
    std::uint32_t seq_;
    Reason const reason_;

    std::set<uint256> recentNodes_;

    SHAMapAddNode stats_;

    // Data we have received from peers
    std::mutex receivedDataLock_;
    std::vector<std::pair<std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData>>>
        receivedData_;
    bool receiveDispatched_{false};
    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Outstanding missing nodes in the account-state tree, as counted by the
     * last getMissingNodes() sweep in trigger(). Relaxed atomic: written by the
     * acquiring thread, read by the telemetry gauge callback, and a value one
     * sweep stale is acceptable for a ~10 s gauge.
     */
    std::atomic<int> missingStateNodes_{0};

    /**
     * Outstanding missing nodes in the transaction tree. Same ownership and
     * staleness contract as missingStateNodes_.
     */
    std::atomic<int> missingTxNodes_{0};

    /**
     * Mirror of receivedData_.size(), maintained under receivedDataLock_ on
     * every push and drain. Exists so the telemetry gauge callback can read the
     * depth without contending for that lock on the node-receive path.
     */
    std::atomic<std::size_t> receivedDataDepth_{0};

    /**
     * Spans the acquire lifecycle: started in init(), finalized in done()
     * with the outcome (complete/failed), timeout count, and peer count.
     * Gives operators visibility into back-fill / fork-recovery cost, which
     * previously emitted no span or metric.
     * Thread-free: emplaced by the acquiring thread, reset on a JtLedgerData
     * worker. A SpanGuard owns no thread-local Scope, so it can be destroyed
     * on the worker without corrupting the origin thread's context stack.
     */
    std::optional<telemetry::SpanGuard> acquireSpan_;
};

}  // namespace xrpl
