#pragma once

#include <xrpld/app/ledger/AbstractFetchPackContainer.h>
#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerHistory.h>
#include <xrpld/app/ledger/LedgerHolder.h>
#include <xrpld/app/ledger/LedgerReplay.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/TimeKeeper.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/RangeSet.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

class Peer;
class Transaction;

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions
class LedgerMaster : public AbstractFetchPackContainer
{
public:
    explicit LedgerMaster(
        Application& app,
        Stopwatch& stopwatch,
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal);

    ~LedgerMaster() override = default;

    LedgerIndex
    getCurrentLedgerIndex();
    LedgerIndex
    getValidLedgerIndex();

    bool
    isCompatible(ReadView const&, beast::Journal::Stream, char const* reason);

    std::recursive_mutex&
    peekMutex();

    // The current ledger is the ledger we believe new transactions should go in
    std::shared_ptr<ReadView const>
    getCurrentLedger();

    // The finalized ledger is the last closed/accepted ledger
    std::shared_ptr<Ledger const>
    getClosedLedger()
    {
        return closedLedger_.get();
    }

    // The validated ledger is the last fully validated ledger.
    std::shared_ptr<Ledger const>
    getValidatedLedger();

    // The Rules are in the last fully validated ledger if there is one.
    Rules
    getValidatedRules();

    // This is the last ledger we published to clients and can lag the validated
    // ledger
    std::shared_ptr<ReadView const>
    getPublishedLedger();

    std::chrono::seconds
    getPublishedLedgerAge();
    std::chrono::seconds
    getValidatedLedgerAge();
    bool
    isCaughtUp(std::string& reason);

    std::uint32_t
    getEarliestFetch();

    bool
    storeLedger(std::shared_ptr<Ledger const> ledger);

    void
    setFullLedger(std::shared_ptr<Ledger const> const& ledger, bool isSynchronous, bool isCurrent);

    /**
     * Check the sequence number and parent close time of a
     * ledger against our clock and last validated ledger to
     * see if it can be the network's current ledger
     */
    bool
    canBeCurrent(std::shared_ptr<Ledger const> const& ledger);

    void
    switchLCL(std::shared_ptr<Ledger const> const& lastClosed);

    void
    failedSave(std::uint32_t seq, uint256 const& hash);

    std::string
    getCompleteLedgers() const;

    std::size_t
    missingFromCompleteLedgerRange(LedgerIndex first, LedgerIndex last) const;

    /**
     * Apply held transactions to the open ledger
     * This is normally called as we close the ledger.
     * The open ledger remains open to handle new transactions
     * until a new open ledger is built.
     */
    void
    applyHeldTransactions();

    /**
     * Get the next transaction held for a particular account if any.
     * This is normally called when a transaction for that account is
     * successfully applied to the open ledger so the next transaction
     * can be resubmitted without waiting for ledger close.
     */
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    /**
     * Get a ledger's hash by sequence number using the cache
     */
    uint256
    getHashBySeq(std::uint32_t index);

    /**
     * Walk to a ledger's hash using the skip list
     */
    std::optional<LedgerHash>
    walkHashBySeq(std::uint32_t index, InboundLedger::Reason reason);

    /**
     * Walk the chain of ledger hashes to determine the hash of the
     * ledger with the specified index. The referenceLedger is used as
     * the base of the chain and should be fully validated and must not
     * precede the target index. This function may throw if nodes
     * from the reference ledger or any prior ledger are not present
     * in the node store.
     */
    std::optional<LedgerHash>
    walkHashBySeq(
        std::uint32_t index,
        std::shared_ptr<ReadView const> const& referenceLedger,
        InboundLedger::Reason reason);

    std::shared_ptr<Ledger const>
    getLedgerBySeq(std::uint32_t index);

    std::shared_ptr<Ledger const>
    getLedgerByHash(uint256 const& hash);

    void
    setLedgerRangePresent(std::uint32_t minV, std::uint32_t maxV);

    std::optional<NetClock::time_point>
    getCloseTimeBySeq(LedgerIndex ledgerIndex);

    std::optional<NetClock::time_point>
    getCloseTimeByHash(LedgerHash const& ledgerHash, LedgerIndex ledgerIndex);

    void
    addHeldTransaction(std::shared_ptr<Transaction> const& trans);
    void
    fixMismatch(ReadView const& ledger);

    bool
    haveLedger(std::uint32_t seq) const;
    void
    clearLedger(std::uint32_t seq);
    bool
    isValidated(ReadView const& ledger);
    bool
    getValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);
    bool
    getFullValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);

    void
    sweep();
    float
    getCacheHitRate();

    void
    checkAccept(std::shared_ptr<Ledger const> const& ledger);
    void
    checkAccept(uint256 const& hash, std::uint32_t seq);
    void
    consensusBuilt(
        std::shared_ptr<Ledger const> const& ledger,
        uint256 const& consensusHash,
        json::Value consensus);

    void
    setBuildingLedger(LedgerIndex index);

    void
    tryAdvance();
    bool
    newPathRequest();  // Returns true if path request successfully placed.
    bool
    isNewPathRequest();
    bool
    newOrderBookDB();  // Returns true if able to fulfill request.

    bool
    fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);

    void
    clearPriorLedgers(LedgerIndex seq);

    void
    clearLedgerCachePrior(LedgerIndex seq);

    // ledger replay
    void
    takeReplay(std::unique_ptr<LedgerReplay> replay);
    std::unique_ptr<LedgerReplay>
    releaseReplay();

    // Fetch Packs
    void
    gotFetchPack(bool progress, std::uint32_t seq);

    void
    addFetchPack(uint256 const& hash, std::shared_ptr<Blob> data);

    std::optional<Blob>
    getFetchPack(uint256 const& hash) override;

    void
    makeFetchPack(
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        UptimeClock::time_point uptime);

    std::size_t
    getFetchPackCacheSize() const;

    /**
     * Whether we have ever fully validated a ledger.
     */
    bool
    haveValidated()
    {
        return !validLedger_.empty();
    }

    // Returns the minimum ledger sequence in SQL database, if any.
    std::optional<LedgerIndex>
    minSqlSeq();

    // Iff a txn exists at the specified ledger and offset then return its txnid
    std::optional<uint256>
    txnIdFromIndex(uint32_t ledgerSeq, uint32_t txnIndex);

    /**
     * Ledgers fully validated but not yet published to clients.
     *
     * The publish pipeline (doAdvance) lags validation by design, but the
     * gap must drain. A gap that stays positive and grows means validation
     * is healthy while publishing is not, which is a different fault from
     * anything the acquire or quorum signals can show.
     *
     * @return Non-negative publish lag in ledgers; 0 when caught up, and 0
     * before the first ledger is validated.
     *
     * @note Safe to call from any thread, including a telemetry
     * observable-gauge callback: two relaxed atomic loads, no lock. The two
     * sequences are read independently, so a reading taken while
     * setValidLedger() and setPubLedger() are both running may be off by
     * one ledger for one poll. That is immaterial for a lag trend and is
     * the price of not taking the LedgerMaster mutex on the poll thread.
     */
    [[nodiscard]] std::int64_t
    getPublishLag() const noexcept
    {
        auto const valid =
            static_cast<std::int64_t>(validLedgerSeq_.load(std::memory_order_relaxed));
        auto const published =
            static_cast<std::int64_t>(pubLedgerSeq_.load(std::memory_order_relaxed));
        auto const lag = valid - published;
        return lag > 0 ? lag : 0;
    }

    /**
     * Trusted validations counted at the most recent pre-accept gate.
     *
     * checkAccept() refuses to declare a ledger validated until this tally
     * reaches getQuorumTarget(). Exposing the tally is what separates
     * "validations are accumulating, just slowly" from "validations arrive
     * but never reach quorum".
     *
     * @return Trusted validation count at the last gate evaluation; 0 before
     * the first one.
     *
     * @note Safe to call from any thread: one relaxed atomic load, no lock.
     */
    [[nodiscard]] std::int64_t
    getTrustedValidationTally() const noexcept
    {
        return lastTrustedTally_.load(std::memory_order_relaxed);
    }

    /**
     * Validations required at the most recent pre-accept gate.
     *
     * @return Quorum threshold at the last gate evaluation; 0 before the
     * first one. Reports std::numeric_limits<std::int64_t>::max() when the
     * validator list has switched quorum off entirely (see
     * getNeededValidations()), so the value never wraps negative and a
     * tally-versus-target panel cannot invert on a node that can never
     * validate.
     *
     * @note Safe to call from any thread: one relaxed atomic load, no lock.
     */
    [[nodiscard]] std::int64_t
    getQuorumTarget() const noexcept
    {
        return lastQuorumTarget_.load(std::memory_order_relaxed);
    }

    /**
     * Time from construction until the first ledger passed the pre-accept
     * gate, in microseconds.
     *
     * A one-shot measurement, like the time-to-first-FULL signal: it is
     * written once and never changes, so it has no trend. Exactly two
     * readings are meaningful — a duration, meaning the node reached its
     * first fully-validated ledger and this is how long that took, or 0,
     * meaning it never has.
     *
     * @return Microseconds to the first fully-validated ledger; 0 until then.
     *
     * @note Safe to call from any thread: one relaxed atomic load, no lock.
     */
    [[nodiscard]] std::int64_t
    getTimeToFirstValidatedUs() const noexcept
    {
        return timeToFirstValidatedUs_.load(std::memory_order_relaxed);
    }

    /**
     * Start a span that joins the per-ledger trace keyed on the ledger hash.
     *
     * One ledger's spans are produced on threads that know nothing about each
     * other: `ledger.acquire` on a JtLedgerData worker, `ledger.validate` from
     * whichever thread calls checkAccept (a peer thread via
     * handleNewValidation, the JtLedgerData "AcqDone" job, or the consensus
     * thread via switchLCL), and `ledger.store` from a fourth. No ambient
     * context reaches across those boundaries, so without an explicit join
     * each ledger's spans would be separate one-span traces and a slow ledger
     * could not be read as one unit.
     *
     * They are joined without propagating anything: `SpanGuard::hashSpan()`
     * derives the trace id from the first 16 bytes of a hash, so every span
     * that passes the SAME ledger hash lands in the SAME trace, and spans for
     * different ledgers stay in different traces. Every one of these sites
     * already holds the ledger hash, so nothing new has to be plumbed through.
     * This is the pattern the apply pipeline already uses to join preflight,
     * preclaim and the transactor on the transaction id
     * (`libxrpl/tx/applySteps.cpp`).
     *
     * The span is a true trace root (deterministic trace id, no parent), so
     * the spans of one ledger are siblings under one trace rather than a
     * parent/child chain — which is the honest shape, since none of them
     * causes another directly and their order varies with the sync path taken.
     *
     * Which stage a span is stays encoded in the span NAME rather than in an
     * extra attribute: the collector already exposes the span name as the
     * `span_name` label on the derived metrics, so a separate phase attribute
     * would be a second copy of the same fact that could disagree with it.
     *
     * @param name        Full span name (e.g. "ledger.validate"). hashSpan
     *                    takes one complete name, not a prefix/suffix pair, so
     *                    pass the joined constant.
     * @param ledgerHash  The ledger's own 32-byte hash: the join key. Its
     *                    first 16 bytes become the trace id, and the whole
     *                    hash is recorded as the `ledger_hash` attribute.
     * @param seq         Ledger sequence, recorded as `ledger_seq` so a trace
     *                    can be found by ledger number.
     * @return An active guard, or a null (no-op) guard when telemetry is off,
     *         built without telemetry, or ledger tracing is not enabled.
     *
     * Example — join the acceptance decision to the rest of the ledger's trace:
     * @code
     *   auto span = makeLedgerTraceSpan(
     *       ledger_span::validate, ledger->header().hash, ledger->header().seq);
     *   span.setAttribute(ledger_span::attr::validations, tvc);
     * @endcode
     *
     * Example — edge case: with tracing disabled the guard is null and every
     * method on it is a no-op, so no call site needs its own check:
     * @code
     *   auto span = makeLedgerTraceSpan(ledger_span::store, hash, seq);
     *   // span is false; setAttribute() below does nothing.
     * @endcode
     *
     * @note Called once per ledger per stage, never per SHAMap node or per
     * transaction. Costs one span plus two attributes when tracing is on, and
     * one predicted branch when it is off.
     * @note Thread-safe: builds a fresh guard from its arguments and the global
     * Telemetry instance, touching no LedgerMaster state, so it may be called
     * from any thread and while mutex_ is held.
     * @note Limitation: the join holds only for spans keyed on the SAME ledger
     * hash. A stage that has only a sequence number (a by-seq lookup before the
     * hash is known) cannot join the trace, and a genuinely different ledger
     * always gets a different trace.
     */
    [[nodiscard]] static telemetry::SpanGuard
    makeLedgerTraceSpan(std::string_view name, uint256 const& ledgerHash, std::uint32_t seq);

private:
    void
    setValidLedger(std::shared_ptr<Ledger const> const& l);
    void
    setPubLedger(std::shared_ptr<Ledger const> const& l);

    void
    tryFill(std::shared_ptr<Ledger const> ledger);

    void
    getFetchPack(LedgerIndex missing, InboundLedger::Reason reason);

    std::optional<LedgerHash>
    getLedgerHashForHistory(LedgerIndex index, InboundLedger::Reason reason);

    std::size_t
    getNeededValidations();
    void
    fetchForHistory(
        std::uint32_t missing,
        bool& progress,
        InboundLedger::Reason reason,
        std::unique_lock<std::recursive_mutex>&);
    // Try to publish ledgers, acquire missing ledgers.  Always called with
    // mutex_ locked.  The passed lock is a reminder to callers.
    void
    doAdvance(std::unique_lock<std::recursive_mutex>&);

    std::vector<std::shared_ptr<Ledger const>>
    findNewLedgersToPublish(std::unique_lock<std::recursive_mutex>&);

    void
    updatePaths();

    // Returns true if work started.  Always called with mutex_ locked.
    // The passed lock is a reminder to callers.
    bool
    newPFWork(char const* name, std::unique_lock<std::recursive_mutex>&);

    Application& app_;
    beast::Journal journal_;

    std::recursive_mutex mutable mutex_;

    // The ledger that most recently closed.
    LedgerHolder closedLedger_;

    // The highest-sequence ledger we have fully accepted.
    LedgerHolder validLedger_;

    // The last ledger we have published.
    std::shared_ptr<Ledger const> pubLedger_;

    // The last ledger we did pathfinding against.
    std::shared_ptr<Ledger const> pathLedger_;

    // The last ledger we handled fetching history
    std::shared_ptr<Ledger const> histLedger_;

    // Fully validated ledger, whether or not we have the ledger resident.
    std::pair<uint256, LedgerIndex> lastValidLedger_{uint256(), 0};

    LedgerHistory ledgerHistory_;

    CanonicalTXSet heldTransactions_{uint256()};

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData_;

    std::recursive_mutex mutable completeLock_;
    RangeSet<std::uint32_t> completeLedgers_;

    // Publish thread is running.
    bool advanceThread_{false};

    // Publish thread has work to do.
    bool advanceWork_{false};
    int fillInProgress_{0};

    int pathFindThread_{0};  // Pathfinder jobs dispatched
    bool pathFindNewRequest_{false};

    std::atomic_flag gotFetchPackThread_ = ATOMIC_FLAG_INIT;  // GotFetchPack jobs dispatched

    std::atomic<std::uint32_t> pubLedgerClose_{0};
    std::atomic<LedgerIndex> pubLedgerSeq_{0};
    std::atomic<std::uint32_t> validLedgerSign_{0};
    std::atomic<LedgerIndex> validLedgerSeq_{0};
    std::atomic<LedgerIndex> buildingLedgerSeq_{0};

    // The server is in standalone mode
    bool const standalone_;

    // How many ledgers before the current ledger do we allow peers to request?
    std::uint32_t const fetchDepth_;

    // How much history do we want to keep
    std::uint32_t const ledgerHistorySize_;

    std::uint32_t const ledgerFetchSize_;

    TaggedCache<uint256, Blob> fetchPacks_;

    std::uint32_t fetchSeq_{0};

    // Try to keep a validator from switching from test to live network
    // without first wiping the database.
    LedgerIndex const maxLedgerDifference_{1000000};

    // Time that the previous upgrade warning was issued.
    TimeKeeper::time_point upgradeWarningPrevTime_;

    // --- Sync diagnostics: the pre-accept quorum gate -----------------------
    //
    // checkAccept() computes a trusted-validation tally and the quorum it must
    // reach, then returns early when the tally is short. Both values used to
    // exist only for the duration of that call and a trace log line, so a node
    // with peers and validators that still refuses to validate looked
    // identical to an idle one. These snapshots keep the last gate evaluation
    // readable by the metrics poll thread.
    //
    //   checkAccept (per validated ledger, holds mutex_)
    //     |  computes tvc, minVal
    //     +--> lastTrustedTally_ / lastQuorumTarget_   (relaxed stores)
    //     |
    //     +--> gate passes --> timeToFirstValidatedUs_ (once per process)
    //
    //   OTel reader thread (~10 s tick)
    //     +--> getTrustedValidationTally() / getQuorumTarget()
    //          getTimeToFirstValidatedUs() / getPublishLag()  (relaxed loads)
    //
    // Deliberately atomics rather than values guarded by mutex_: the emit path
    // holds mutex_ while the metric macro takes an OTel-internal lock, so a
    // reader that took mutex_ from inside an OTel callback would invert that
    // order. Lock-free reads make the inversion impossible.

    /**
     * Trusted validations counted at the last checkAccept gate; 0 before the
     * first gate evaluation.
     */
    std::atomic<std::int64_t> lastTrustedTally_{0};

    /**
     * Validations required at the last checkAccept gate; 0 before the first
     * gate evaluation, and int64 max when quorum is switched off entirely.
     */
    std::atomic<std::int64_t> lastQuorumTarget_{0};

    /**
     * Microseconds from construction to the first ledger that passed the
     * pre-accept gate. Written exactly once; 0 means it has not happened.
     */
    std::atomic<std::int64_t> timeToFirstValidatedUs_{0};

    /**
     * Construction time, the epoch the first-validated milestone measures
     * from. Steady, so it is unaffected by wall-clock or NTP adjustments.
     */
    std::chrono::steady_clock::time_point const startTime_{std::chrono::steady_clock::now()};

private:
    struct Stats
    {
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->makeHook(handler))
            , validatedLedgerAge(collector->makeGauge("LedgerMaster", "Validated_Ledger_Age"))
            , publishedLedgerAge(collector->makeGauge("LedgerMaster", "Published_Ledger_Age"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge validatedLedgerAge;
        beast::insight::Gauge publishedLedgerAge;
    };

    Stats stats_;

private:
    void
    collectMetrics()
    {
        std::scoped_lock const lock(mutex_);
        stats_.validatedLedgerAge.set(getValidatedLedgerAge().count());
        stats_.publishedLedgerAge.set(getPublishedLedgerAge().count());
    }
};

}  // namespace xrpl
