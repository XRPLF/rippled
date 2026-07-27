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
#include <string_view>
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
     * Zero the missing-node counts because this acquire has finished.
     *
     * Unconditional, so it also clears after a timeout or failure, where the
     * have-tree flags are never set and the flag-guarded refresh would leave a
     * stale non-zero count visible to the gauge.
     */
    void
    clearMissingNodeCounts() noexcept;

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

    /**
     * End the acquire span exactly once, stamping the outcome it reached.
     *
     * An acquire can leave through four exits: done(), the local-store
     * shortcut in init(), the "can never be acquired" exit in init(), and the
     * destructor when the sweeper drops a fetch that never finished. Every one
     * of them calls this, so the span always carries an `outcome` and its
     * duration always ends at the real exit instead of stretching to whenever
     * the object happened to be destroyed. Without that, the one case worth
     * detecting -- an acquire that never completes -- was the one case with no
     * span data.
     *
     * The outcome is derived from the acquire's own flags rather than passed
     * in, so no call site can label an exit wrongly:
     *   failed_ -> "failed", else complete_ -> "complete", else "abandoned".
     * "abandoned" therefore means exactly "destroyed with no result".
     *
     * Idempotent: the span handle is cleared here, so the second and later
     * calls do nothing. A no-op when telemetry is disabled (the guard is
     * inactive) or when the span was never created.
     *
     * @param peerCount Peers still reachable for this fetch, or nullopt when
     *        the caller cannot safely look them up. The destructor passes
     *        nullopt because it can run while InboundLedgers holds its
     *        collection lock, and the lookup would take the Overlay lock
     *        underneath it.
     *
     * @note noexcept, and every fallible step is wrapped, because the
     *       destructor calls this: an escaping exception there would
     *       terminate the process during unwinding.
     * @note Called once per acquire exit, never on a per-SHAMap-node path.
     */
    void
    finalizeAcquireSpan(std::optional<std::size_t> peerCount) noexcept;

    /**
     * Open the span for the fetch phase now in progress and close any phase
     * whose data has arrived.
     *
     * One acquire is really three sequential fetches -- the header, then the
     * account-state tree, then the transaction tree -- and on a fresh sync the
     * state tree dominates. The parent span is flat, so it cannot say which of
     * the three a stuck acquire is stuck in. These child spans can.
     *
     * Written as an idempotent state sync over the `have*_` flags rather than
     * as open/close calls scattered through the fetch code: the flags are the
     * real phase boundary, so deriving the span state from them cannot drift
     * out of step with the fetch, and the function is safe to call from any
     * progress point however often.
     *
     * The tree phases never open before the header arrives, because the header
     * is what names their root hashes -- until then there is nothing to fetch.
     *
     * @note noexcept and allocation-free on the disabled path: with telemetry
     *       off the parent span is inactive, so no child is ever created.
     * @note Call with mtx_ held, as every call site does. Called at phase
     *       boundaries and per received packet, never per SHAMap node.
     */
    void
    syncPhaseSpans() noexcept;

    /**
     * Start one phase child span, parented to the acquire span.
     *
     * Parented through the acquire span's captured context rather than the
     * thread's ambient context, so a phase opened on a JtLedgerData worker
     * still lands under the right acquire.
     *
     * @param span  The phase span handle to fill; untouched if already open.
     * @param name  Full span name from LedgerSpanNames.h.
     *
     * @note A no-op when the acquire span is absent or inactive, which is what
     *       makes the whole phase-span feature cost nothing when telemetry is
     *       disabled.
     */
    void
    beginPhaseSpan(std::optional<telemetry::SpanGuard>& span, std::string_view name) noexcept;

    /**
     * End one phase child span exactly once, stamping its outcome.
     *
     * Idempotent by the same rule as finalizeAcquireSpan(): the handle is
     * cleared, so a later call finds nothing and cannot overwrite the outcome
     * the real phase end recorded.
     *
     * @param span         The phase span handle to end.
     * @param complete     Whether this phase's data was fully assembled.
     * @param missingNodes Nodes still outstanding in this phase's tree, or
     *        nullopt for the header phase, which has no tree.
     *
     * @note noexcept, and the attribute writes are wrapped, because this is
     *       reached from ~InboundLedger via finalizeAcquireSpan().
     */
    void
    endPhaseSpan(
        std::optional<telemetry::SpanGuard>& span,
        bool complete,
        std::optional<int> missingNodes) noexcept;

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
     * Spans the acquire lifecycle: started in init(), ended by
     * finalizeAcquireSpan() with the outcome (complete/failed/abandoned),
     * timeout count, and peer count. Gives operators visibility into
     * back-fill / fork-recovery cost, which previously emitted no span or
     * metric.
     * Held for the object's whole lifetime so the destructor can still stamp
     * an outcome on a fetch that never reached done().
     * Thread-free: emplaced by the acquiring thread, reset on a JtLedgerData
     * worker or in the destructor. A SpanGuard owns no thread-local Scope, so
     * it can be destroyed on any thread without corrupting the origin
     * thread's context stack.
     */
    std::optional<telemetry::SpanGuard> acquireSpan_;

    /**
     * Child spans for the three fetch phases of this acquire, each a child of
     * acquireSpan_ and each open only while its phase is in progress.
     *
     * Present so a stuck acquire names the phase it is stuck in: on a fresh
     * sync the account-state tree is nearly all of the work, and the flat
     * parent span cannot separate it from the small transaction tree or from
     * the header wait that gates both.
     *
     * Same ownership contract as acquireSpan_: written under mtx_ or from the
     * destructor, and thread-free, so a phase may be ended on whichever worker
     * receives its last node.
     */
    std::optional<telemetry::SpanGuard> headerSpan_;
    std::optional<telemetry::SpanGuard> asTreeSpan_;
    std::optional<telemetry::SpanGuard> txTreeSpan_;

    /**
     * True once the acquire has exhausted its timeout budget, so each phase
     * still open at that point reports `timeout` rather than `abandoned`.
     * Distinct from `failed_`, which the same path also sets: `failed_` is how
     * the timer loop stops, while this is what says the cause was peers not
     * supplying data rather than data that would not apply.
     */
    bool timedOut_{false};
};

}  // namespace xrpl
