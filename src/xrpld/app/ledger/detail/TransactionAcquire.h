#pragma once

#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <chrono>
#include <cstddef>
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
 *     +--------------------------------------+
 *
 * Acquisition lifecycle and where the span ends:
 *
 *     init()  -- span starts, timer set
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
 *     ~TransactionAcquire() -- span finalized (abandoned) when the set is
 *                              dropped by the round sweep or at shutdown
 *
 * @note Thread safety: unchanged by the span. `acquireSpan_` is written only
 *       under `mtx_` or from the destructor, both of which exclude every other
 *       writer. A SpanGuard owns no thread-local scope, so it can be ended on
 *       whichever JtTxnData worker reaches the terminal path.
 */
class TransactionAcquire final : public TimeoutCounter,
                                 public std::enable_shared_from_this<TransactionAcquire>,
                                 public CountedObject<TransactionAcquire>
{
public:
    using pointer = std::shared_ptr<TransactionAcquire>;

    TransactionAcquire(Application& app, uint256 const& hash, std::unique_ptr<PeerSet> peerSet);
    ~TransactionAcquire() override;

    SHAMapAddNode
    takeNodes(
        std::vector<std::pair<SHAMapNodeID, Slice>> const& data,
        std::shared_ptr<Peer> const&);

    void
    init(int startPeers);

    void
    stillNeed();

private:
    std::shared_ptr<SHAMap> map_;
    bool haveRoot_{false};
    std::unique_ptr<PeerSet> peerSet_;

    /**
     * When this acquisition started, set in init() before the first peer is
     * asked. Base for the span's `duration_ms` attribute.
     */
    std::chrono::steady_clock::time_point acquireStart_;

    /**
     * True once the timeout budget has been exhausted, so the span's outcome
     * reads `timeout` rather than the bare `failed` the flag alone would give.
     * Distinct from `failed_`, which onTimer() also sets on that path: the two
     * together are what separate "peers never supplied the set" from "a peer
     * supplied an invalid one".
     */
    bool timedOut_{false};

    /**
     * Spans the whole acquisition: started in init(), ended by
     * finalizeAcquireSpan() on whichever exit this object takes, including the
     * destructor. Held for the object's lifetime so a set that is dropped
     * mid-fetch still reports an outcome instead of exporting a span with
     * none.
     * Thread-free: a SpanGuard holds no thread-local scope, so it may be
     * created on the acquiring thread and ended on a JtTxnData worker or in
     * the destructor.
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
     * A tx-set acquisition can leave through two exits: done(), reached from
     * trigger() on success or invalid data and from onTimer() when the timeout
     * budget runs out, and the destructor, when the round sweep in
     * InboundTransactions::newRound drops a set that never arrived. Both call
     * this, so the span always carries an `outcome` and its duration always
     * ends at the real exit rather than stretching to whenever the object
     * happened to be released.
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
     * @note noexcept, and the attribute writes are wrapped, because the
     *       destructor calls this: an exception escaping a destructor during
     *       unwinding would terminate the process.
     * @note Called once per acquisition, never on the per-node path in
     *       takeNodes().
     */
    void
    finalizeAcquireSpan() noexcept;
};

}  // namespace xrpl
