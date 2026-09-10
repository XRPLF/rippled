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
#include <xrpl/telemetry/Recording.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

// VFALCO TODO rename to PeerTxRequest
/**
 * A transaction set we are trying to acquire.
 *
 * The tx-set sibling of InboundLedger: same TimeoutCounter base, same
 * trigger / onTimer / takeNodes shape, fetching the transaction SHAMap a
 * consensus proposal referenced rather than a whole ledger.
 *
 *     +-----------------+
 *     | TimeoutCounter  |  timer loop, timeouts_/complete_/failed_ flags
 *     +--------+--------+
 *              |
 *     +--------v-----------------------------+
 *     | TransactionAcquire                   |
 *     |  map_      : the tx SHAMap being     |
 *     |              assembled               |
 *     |  peerSet_  : peers asked for nodes   |
 *     |  acquireSpan_ : "txset.acquire" span |
 *     |  roundParentHash_ / roundLedgerSeq_  |
 *     |            : round that started it   |
 *     +--------------------------------------+
 *
 * Acquisition lifecycle and where the span ends:
 *
 *     init()  -- span starts, timer set, first round.request event
 *       |        (does nothing at all if the set was already dropped)
 *       |
 *       +--> recordRequestingRound() -- one more round.request event per LATER
 *       |        round that still needs the same set
 *       |
 *       +--> trigger() ---- root or missing nodes requested from a peer
 *       |        ^                    |
 *       |        |                    v
 *       +--> takeNodes() -- nodes applied, trigger() again
 *       |
 *       +--> onTimer() ---- retry, or give up past the timeout budget
 *       |
 *       v
 *     done()  -- span finalized (complete | failed | timeout)
 *       or
 *     abandonAcquireSpan() -- span finalized (abandoned); InboundTransactions
 *                             stopped pursuing the fetch
 *       or
 *     cancel() -- span finalized; the base marks the task failed without
 *                 reaching done()
 *       then, on any of those paths
 *     ~TransactionAcquire() -- not an exit; asserts the span already ended
 *
 * @note Thread safety: unchanged by the span. `acquireSpan_` is written only
 *       under `mtx_`: init() and done() already hold it, while
 *       abandonAcquireSpan(), recordRequestingRound() and cancel() each take it
 *       because they are called from another thread. A SpanGuard owns no
 *       thread-local scope, so it can be ended on whichever JtTxnData worker
 *       reaches the terminal path.
 * @note Lock ordering: `InboundTransactions::lock_` is held across
 *       abandonAcquireSpan() and recordRequestingRound(), so the order is
 *       `lock_` then `mtx_`. Nothing under `mtx_` takes them the other way
 *       round -- done() only queues a job, and addPeers() / trigger() reach the
 *       Overlay, never InboundTransactions.
 */
class TransactionAcquire final : public TimeoutCounter,
                                 public std::enable_shared_from_this<TransactionAcquire>,
                                 public CountedObject<TransactionAcquire>
{
public:
    using pointer = std::shared_ptr<TransactionAcquire>;

    /**
     * @param roundParentHash Parent-ledger hash of the consensus round starting
     *        this fetch; copied, see `roundParentHash_`.
     * @param roundLedgerSeq  Sequence of the ledger that round is building.
     */
    TransactionAcquire(
        Application& app,
        uint256 const& hash,
        std::unique_ptr<PeerSet> peerSet,
        uint256 const& roundParentHash,
        std::uint32_t roundLedgerSeq);
    ~TransactionAcquire() override;

    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
        std::shared_ptr<Peer> const& peer);

    void
    init(int startPeers);

    void
    stillNeed();

    /**
     * Add a `round.request` event for a LATER round that still needs this set;
     * init() records the round that started the fetch. Called by
     * InboundTransactions when the asking round differs from the one that last
     * asked -- see `InboundTransactionSet::lastRound`. Takes `mtx_`,
     * called under `InboundTransactions::lock_`.
     */
    void
    recordRequestingRound(uint256 const& roundParentHash, std::uint32_t roundLedgerSeq) noexcept;

    /**
     * End the acquire span because this fetch is no longer being pursued.
     *
     * Called by InboundTransactions just before it drops its reference: giveSet,
     * the newRound sweep, stop, or the container being destroyed. Otherwise the
     * span would end whenever the last shared_ptr dropped, and `duration_ms`
     * would include however long someone held a pointer.
     *
     * @note Takes `mtx_`, called under `InboundTransactions::lock_`. noexcept,
     *       so a sweep or teardown loop cannot be interrupted part way through.
     */
    void
    abandonAcquireSpan() noexcept;

    /**
     * End the acquire span when the base cancels the task: `cancel()` sets
     * `failed_` without reaching done(). No caller reaches it for a tx set
     * today, but it is public and virtual on the base, so this is the "exit
     * added later" the destructor's assertion exists to catch.
     */
    void
    cancel() override;

private:
    std::shared_ptr<SHAMap> map_;
    bool haveRoot_{false};
    std::unique_ptr<PeerSet> peerSet_;

    /**
     * Identity of the consensus round that started this fetch: parent-ledger
     * hash and the sequence of the ledger it is building. Copies, not
     * references: we are constructed under InboundTransactions' lock but init()
     * runs after it is released, so a reference into that container or the
     * Adaptor's round state would be read with no lock held.
     */
    uint256 roundParentHash_;
    std::uint32_t roundLedgerSeq_{0};

    /**
     * How long this acquisition has been running, restarted in init() before
     * the first peer is asked. Source of the span's `duration_ms` attribute,
     * and reads no clock in a build that never reports one.
     */
    telemetry::Stopwatch acquireTimer_;

    /**
     * True once the timeout budget has been exhausted, so the span's outcome
     * reads `timeout` rather than the bare `failed` the flag alone would give.
     * Distinct from `failed_`, which onTimer() also sets on that path: the two
     * together are what separate "peers never supplied the set" from "a peer
     * supplied an invalid one".
     */
    bool timedOut_{false};

    /**
     * True once InboundTransactions has stopped pursuing this fetch.
     *
     * Makes init() a no-op. getSet() releases its own lock before calling
     * init(), so a sweep or a shutdown can drop the set in that window, and
     * proceeding would both open a span no exit is left to close and ask peers
     * for a set nobody wants. Deliberately not an input to the outcome rule: no
     * flag set already reads as `abandoned`.
     */
    bool abandoned_{false};

    /**
     * Spans the whole acquisition: started in init(), ended by
     * finalizeAcquireSpan() on whichever exit this object takes -- done(),
     * abandonAcquireSpan() or cancel(). Held for the object's lifetime so a set
     * dropped mid-fetch still reports an outcome instead of exporting a span
     * with none.
     * Thread-free: a SpanGuard holds no thread-local scope, so it may be
     * created on the acquiring thread and ended on a JtTxnData worker or on
     * whichever thread swept the set.
     */
    std::optional<telemetry::SpanGuard> acquireSpan_;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    void
    done();

    void
    addPeers(std::size_t limit);

    void
    trigger(std::shared_ptr<Peer> const&);
    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    /**
     * End the acquire span exactly once, stamping the outcome it reached.
     *
     * A tx-set acquisition can leave through three exits: done(), reached from
     * trigger() on success or invalid data and from onTimer() when the timeout
     * budget runs out; abandonAcquireSpan(), when InboundTransactions stops
     * pursuing the fetch; and cancel(), when the base marks the task failed
     * without reaching done(). All three call this, so the span always carries
     * an `outcome` and its duration always ends at the real exit rather than
     * stretching to whenever the object happened to be released.
     *
     * The destructor deliberately does NOT call this. It asserts instead, so an
     * exit added later that forgets to finalize fails loudly rather than
     * reporting a duration that runs on to whenever the last reference dropped.
     *
     * The outcome is derived from this object's own flags by the shared
     * phaseOutcome() rule rather than passed in, so no call site can mislabel
     * an exit and no exit added later can forget one:
     *   failed_ -> "failed", timedOut_ -> "timeout", complete_ -> "complete",
     *   otherwise "abandoned" (dropped with no result).
     *
     * Idempotent: the span handle is cleared here, so the second and later
     * calls do nothing and cannot overwrite the outcome the real exit
     * recorded.
     *
     * @note noexcept, and the attribute writes are wrapped, because
     *       abandonAcquireSpan() calls this from the middle of a round sweep and
     *       from shutdown, where an escaping exception would abandon the rest of
     *       the loop.
     * @note Callers must hold `mtx_`; every caller already does.
     * @note Called once per acquisition, never on the per-node path in
     *       takeNodes().
     */
    void
    finalizeAcquireSpan() noexcept;

    /**
     * Add one `round.request` event naming the round that is asking. The single
     * place the event is built, shared by init() and recordRequestingRound(), so
     * the two cannot record a requester differently. Callers must hold `mtx_`;
     * noexcept because the string conversions allocate.
     */
    void
    addRoundRequestEvent(uint256 const& roundParentHash, std::uint32_t roundLedgerSeq) noexcept;
};

}  // namespace xrpl
