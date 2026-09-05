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

#include <xrpl.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class Peer;
class Transaction;

/**
 * Tracks the current ledger and any ledgers in the process of closing.
 * Tracks ledger history. Tracks held transactions.
 *
 * Four ledgers are tracked, and they can all differ:
 *
 *   current    open ledger new transactions go into (owned by OpenLedger)
 *   closed     most recently closed ledger, not yet fully validated
 *   validated  highest ledger with a quorum of trusted validations
 *   published  highest ledger handed to subscribed clients; lags validated
 *
 *   RCLConsensus ──switchLCL/consensusBuilt──> LedgerMaster
 *   PeerImp ──────gotFetchPack/makeFetchPack─>     │
 *                                                  ├─> LedgerHistory (cache)
 *                                                  ├─> InboundLedgers (acquire)
 *                                                  ├─> NetworkOPs (publish)
 *                                                  ├─> PathRequestManager
 *                                                  ├─> LedgerReplayer
 *                                                  └─> Application (everything else)
 *
 * Advancing the stream is a state machine run on a JobQueue thread. Callers
 * only ever poke it with tryAdvance(); the loop decides what to do next:
 *
 *   tryAdvance()
 *      │ (one AdvanceLedger job at a time)
 *      v
 *   doAdvance() ──> findNewLedgersToPublish()
 *      ^                 │
 *      │            has ledgers? ──yes──> setFullLedger, setPubLedger,
 *      │                 │                NetworkOPs::pubLedger, newPFWork
 *      │                 no
 *      │                 v
 *      │            caught up and idle? ──no──> stop
 *      │                 │ yes
 *      │                 v
 *      │            fetchForHistory(newest gap below pubLedger) ──> tryFill()
 *      └──────── repeat while progress was made
 *
 * @code
 * // Read the validated ledger, if the node has one yet.
 * if (ledgerMaster.haveValidated())
 *     auto const seq = ledgerMaster.getValidatedLedger()->header().seq;
 *
 * // After consensus closes a ledger: adopt it, then let the stream catch up.
 * ledgerMaster.switchLCL(closed);
 * ledgerMaster.tryAdvance();
 *
 * // Edge case: on a node that has never validated, the age accessors report
 * // two weeks rather than zero, so "stale" checks fail closed.
 * ledgerMaster.getValidatedLedgerAge();  // 2 weeks
 * @endcode
 *
 * @note Thread-safe. Two recursive mutexes guard the state: mutex_ for the
 * tracked ledgers and the job bookkeeping, completeLock_ for completeLedgers_
 * only. When both are needed, mutex_ is taken first. The sequence numbers are
 * atomics and are read without either lock.
 * @note Several accessors can block for a long time: anything that reaches
 * InboundLedgers::acquire() may wait on the network, and anything walking a
 * skip list may throw SHAMapMissingNode. Do not call them while holding a lock
 * a network thread needs.
 * @note The published stream is best-effort. If it falls more than 100 ledgers
 * behind, the gap is abandoned and publication jumps to the validated ledger.
 */
class LedgerMaster : public AbstractFetchPackContainer
{
public:
    /**
     * Constructs the tracker; reads history and fetch limits from the config.
     *
     * @param app Owning application, used to reach every other subsystem.
     * @param stopwatch Clock driving expiry of the fetch-pack cache.
     * @param collector Sink the ledger-age gauges are registered with.
     * @param journal Log sink.
     */
    explicit LedgerMaster(
        Application& app,
        Stopwatch& stopwatch,
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal);

    /**
     * Destroys the tracker. Outstanding JobQueue work must already have been
     * drained, since those jobs capture this.
     */
    ~LedgerMaster() override = default;

    /**
     * @return Sequence of the open ledger transactions are being applied to.
     */
    LedgerIndex
    getCurrentLedgerIndex();

    /**
     * @return Sequence of the last fully validated ledger, or 0 if there is none.
     */
    LedgerIndex
    getValidLedgerIndex();

    /**
     * Tests whether a view can belong to the same chain as what we validated.
     *
     * @param view Ledger view to test.
     * @param s Stream the reason for a mismatch is logged to.
     * @param reason Caller-supplied label for that log line.
     * @return false when the view conflicts with the validated ledger or with
     * the highest ledger known to have a validation quorum.
     */
    bool
    isCompatible(ReadView const&, beast::Journal::Stream, char const* reason);

    /**
     * Exposes the lock guarding the tracked ledgers, so a caller can hold it
     * across several calls and see one consistent snapshot.
     *
     * @return The recursive mutex. Callers must not keep it past a blocking call.
     */
    std::recursive_mutex&
    peekMutex();

    /**
     * The current ledger is the ledger we believe new transactions should go in.
     *
     * @return The open ledger.
     */
    std::shared_ptr<ReadView const>
    getCurrentLedger();

    /**
     * The finalized ledger is the last closed/accepted ledger.
     *
     * @return That ledger, or null before the first close.
     */
    std::shared_ptr<Ledger const>
    getClosedLedger()
    {
        return closedLedger_.get();
    }

    /**
     * The validated ledger is the last fully validated ledger.
     *
     * @return That ledger, or null if none is resident.
     */
    std::shared_ptr<Ledger const>
    getValidatedLedger();

    /**
     * The Rules are in the last fully validated ledger if there is one.
     *
     * @return Those Rules, or ones built from the configured amendments when
     * nothing is validated yet.
     */
    Rules
    getValidatedRules();

    /**
     * This is the last ledger we published to clients and can lag the validated
     * ledger.
     *
     * @return That ledger, or null until the first publication.
     */
    std::shared_ptr<ReadView const>
    getPublishedLedger();

    /**
     * @return Seconds between network time and the close time of the last
     * published ledger; two weeks when nothing has been published, and never
     * negative.
     */
    std::chrono::seconds
    getPublishedLedgerAge();

    /**
     * @return Seconds between network time and the validation sign time of the
     * last validated ledger; two weeks when nothing is validated.
     */
    std::chrono::seconds
    getValidatedLedgerAge();

    /**
     * Decides whether the node is close enough to the network to serve clients.
     *
     * @param reason Set to a human-readable explanation when the answer is false.
     * @return false when nothing has been published, the published ledger is
     * over three minutes old, or validation leads publication by over 90s.
     */
    bool
    isCaughtUp(std::string& reason);

    /**
     * Get the earliest ledger we will let peers fetch.
     *
     * @return fetchDepth_ behind the closed ledger, floored at zero.
     * @note Requires a closed ledger to exist; do not call this before the
     * first ledger has closed.
     */
    std::uint32_t
    getEarliestFetch();

    /**
     * Adds a ledger to the history cache. Does not itself validate the ledger,
     * but honours the flag the ledger already carries: a ledger marked validated
     * is also indexed by sequence.
     *
     * @param ledger Immutable ledger to cache.
     * @return true if we already had the ledger.
     */
    bool
    storeLedger(std::shared_ptr<Ledger const> ledger);

    /**
     * A new ledger has been accepted as part of the trusted chain: mark it
     * validated and full, record it as resident, persist it, and repair the
     * chain behind it if the parent turns out to disagree.
     *
     * @param ledger Ledger to accept.
     * @param isSynchronous true to write it to the database before returning.
     * @param isCurrent true when the ledger is part of the live stream rather
     * than back-filled history; only current ledgers enter the index.
     */
    void
    setFullLedger(std::shared_ptr<Ledger const> const& ledger, bool isSynchronous, bool isCurrent);

    /**
     * Check the sequence number and parent close time of a
     * ledger against our clock and last validated ledger to
     * see if it can be the network's current ledger
     *
     * @param ledger Candidate ledger; must not be null.
     * @return false when the candidate precedes the validated ledger, its
     * parent close time is over five minutes from network time, or its
     * sequence runs further ahead than elapsed time could explain.
     */
    bool
    canBeCurrent(std::shared_ptr<Ledger const> const& ledger);

    /**
     * Adopts a ledger as the last closed ledger, then either accepts it
     * directly (standalone) or tests it for validation (networked).
     *
     * @param lastClosed Closed, immutable ledger. Passing an open or mutable
     * ledger is a logic error and terminates the process.
     */
    void
    switchLCL(std::shared_ptr<Ledger const> const& lastClosed);

    /**
     * Marks a ledger no longer resident after its save failed, and starts
     * fetching it again.
     *
     * @param seq Sequence of the ledger that failed to save.
     * @param hash Hash of that ledger, used to re-acquire it.
     */
    void
    failedSave(std::uint32_t seq, uint256 const& hash);

    /**
     * @return Resident ledger sequences as ranges, e.g. "3-8,10", or "empty".
     */
    std::string
    getCompleteLedgers() const;

    /**
     * Counts gaps in a closed sequence interval.
     *
     * @param first Lowest sequence to test; must not exceed last.
     * @param last Highest sequence to test.
     * @return How many sequences in [first, last] are not resident.
     */
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
     *
     * @param tx Transaction that was just applied; its account and sequence
     * select the successor.
     * @return The next held transaction for that account, or null if none.
     */
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    /**
     * Get a ledger's hash by sequence number using the cache
     *
     * @param index Ledger sequence to look up.
     * @return The hash, or zero when neither the cache nor the SQL index has it.
     */
    uint256
    getHashBySeq(std::uint32_t index);

    /**
     * Walk to a ledger's hash using the skip list
     *
     * @param index Ledger sequence wanted.
     * @param reason Why the ledger is needed, for any acquire this triggers.
     * @return The hash, or nullopt when there is no validated ledger to walk
     * back from, or when the walk itself cannot produce it.
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
     *
     * @param index Ledger sequence wanted.
     * @param referenceLedger Ledger to walk back from.
     * @param reason Why the ledger is needed, for any acquire this triggers.
     * @return The hash, or nullopt when the reference ledger is null or
     * precedes index, when the intervening hash page is missing, or when
     * acquiring the intermediate ledger fails.
     */
    std::optional<LedgerHash>
    walkHashBySeq(
        std::uint32_t index,
        std::shared_ptr<ReadView const> const& referenceLedger,
        InboundLedger::Reason reason);

    /**
     * Finds a resident ledger by sequence, preferring the validated chain.
     *
     * @param index Ledger sequence wanted.
     * @return The ledger, or null. A miss also drops index from the resident
     * set, since we evidently do not have it.
     */
    std::shared_ptr<Ledger const>
    getLedgerBySeq(std::uint32_t index);

    /**
     * @param hash Ledger hash wanted.
     * @return The ledger from the history cache or the closed ledger, or null.
     */
    std::shared_ptr<Ledger const>
    getLedgerByHash(uint256 const& hash);

    /**
     * Records a closed range of sequences as resident.
     *
     * @param minV Lowest sequence now present.
     * @param maxV Highest sequence now present.
     */
    void
    setLedgerRangePresent(std::uint32_t minV, std::uint32_t maxV);

    /**
     * @param ledgerIndex Ledger sequence wanted.
     * @return Its close time, or nullopt when the hash or the stored header
     * cannot be found.
     */
    std::optional<NetClock::time_point>
    getCloseTimeBySeq(LedgerIndex ledgerIndex);

    /**
     * Reads a close time straight out of the serialized header in the node store.
     *
     * @param ledgerHash Hash of the ledger wanted.
     * @param ledgerIndex Its sequence, used as the node-store lookup hint.
     * @return Its close time, or nullopt when the object is absent, too short,
     * or not a ledger header.
     */
    std::optional<NetClock::time_point>
    getCloseTimeByHash(LedgerHash const& ledgerHash, LedgerIndex ledgerIndex);

    /**
     * Defers a transaction to the next open ledger.
     *
     * @param trans Transaction to hold.
     */
    void
    addHeldTransaction(std::shared_ptr<Transaction> const& trans);

    /**
     * Walks back from a ledger dropping every resident ledger whose hash
     * disagrees with that ledger's skip list, stopping at the first match.
     *
     * @param ledger Ledger whose chain is taken as correct.
     */
    void
    fixMismatch(ReadView const& ledger);

    /**
     * @param seq Ledger sequence to test.
     * @return true when every node of that ledger is held locally.
     */
    bool
    haveLedger(std::uint32_t seq) const;

    /**
     * Marks a ledger no longer resident.
     *
     * @param seq Ledger sequence to drop.
     */
    void
    clearLedger(std::uint32_t seq);

    /**
     * Tests whether a ledger is on the validated chain, using the skip list
     * when the ledger does not already say so.
     *
     * @param ledger Ledger to test; open ledgers are never validated.
     * @return true when the ledger is part of the validated chain. A true
     * answer is cached in the ledger header; a false one is recomputed on every
     * call. When the SQL index names this ledger but the validated chain does
     * not, the sequence is dropped from the resident set.
     */
    bool
    isValidated(ReadView const& ledger);

    /**
     * Returns Ledgers we have all the nodes for and are indexed: the fully
     * validated range minus any sequence still being written.
     *
     * @param minVal Set to the lowest usable sequence, or 0 if none remains.
     * @param maxVal Set to the highest usable sequence, or 0 if none remains.
     * @return false when nothing has been published yet.
     */
    bool
    getValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);

    /**
     * Returns Ledgers we have all the nodes for: the contiguous resident range
     * ending at the published ledger.
     *
     * @param minVal Set to the lowest sequence of that range.
     * @param maxVal Set to the published sequence.
     * @return false when nothing has been published yet.
     */
    bool
    getFullValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);

    /**
     * Expires stale entries from the ledger history and fetch-pack caches.
     */
    void
    sweep();

    /**
     * @return Hit rate of the ledger-by-hash cache, as a percentage.
     */
    float
    getCacheHitRate();

    /**
     * Accepts a ledger as the new last fully validated ledger if it has a
     * validation quorum, then republishes fees and pokes the stream forward.
     *
     * @param ledger Candidate ledger.
     */
    void
    checkAccept(std::shared_ptr<Ledger const> const& ledger);

    /**
     * Check if the specified ledger can become the new last fully-validated
     * ledger, fetching it if it is not resident.
     *
     * @param hash Hash of the candidate ledger.
     * @param seq Its sequence. Zero means the sequence is unknown, which skips
     * the staleness checks.
     */
    void
    checkAccept(uint256 const& hash, std::uint32_t seq);

    /**
     * Report that the consensus process built a particular ledger
     *
     * Records the built ledger, then looks for the highest ledger with enough
     * trusted validations to accept - which may not be this one.
     *
     * @param ledger Ledger consensus just built.
     * @param consensusHash Hash of the consensus transaction set.
     * @param consensus Consensus metadata for this round, kept only so a
     * built-versus-validated mismatch can be logged.
     */
    void
    consensusBuilt(
        std::shared_ptr<Ledger const> const& ledger,
        uint256 const& consensusHash,
        json::Value consensus);

    /**
     * Records which ledger consensus is currently building, so validations for
     * it are not chased separately.
     *
     * @param index Sequence being built; 0 when none is.
     */
    void
    setBuildingLedger(LedgerIndex index);

    /**
     * Signals that the ledger stream may be able to make progress. Starts the
     * advance job if it is not already running, and is cheap to call often.
     */
    void
    tryAdvance();

    /**
     * Notes a new pathfinding request and dispatches a worker if one is due.
     *
     * @return true if path request successfully placed.
     */
    bool
    newPathRequest();  // Returns true if path request successfully placed.

    /**
     * @return true if a new pathfinding request arrived since the last call.
     * Reading the flag clears it.
     */
    bool
    isNewPathRequest();

    /**
     * If the order book is radically updated, we need to reprocess all
     * pathfinding requests.
     *
     * @return true if able to fulfill request.
     */
    bool
    newOrderBookDB();  // Returns true if able to fulfill request.

    /**
     * Corrects the cached sequence-to-hash mapping for one ledger.
     *
     * @param ledgerIndex Sequence to correct.
     * @param ledgerHash Hash that sequence really has.
     * @return false when a different hash was cached, meaning the caller's
     * view of history just changed; true when it already matched or was absent.
     */
    bool
    fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);

    /**
     * Forgets that we hold any ledger below a sequence, without touching the
     * cached ledgers themselves.
     *
     * @param seq First sequence to keep.
     */
    void
    clearPriorLedgers(LedgerIndex seq);

    /**
     * Drops cached ledgers below a sequence.
     *
     * @param seq First sequence to keep.
     */
    void
    clearLedgerCachePrior(LedgerIndex seq);

    // ledger replay

    /**
     * Stores a transaction set to replay when the next ledger closes.
     *
     * @param replay Set to replay; replaces any set already held.
     */
    void
    takeReplay(std::unique_ptr<LedgerReplay> replay);

    /**
     * @return The stored replay set, transferring ownership; null if none.
     */
    std::unique_ptr<LedgerReplay>
    releaseReplay();

    // Fetch Packs

    /**
     * Signals that fetch-pack data arrived, so waiting acquires can use it.
     * At most one handler job is queued at a time.
     *
     * @param progress Unused by the current implementation.
     * @param seq Unused by the current implementation.
     */
    void
    gotFetchPack(bool progress, std::uint32_t seq);

    /**
     * Caches one fetch-pack node received from a peer.
     *
     * @param hash Hash of the node, which is its key.
     * @param data Serialized node.
     */
    void
    addFetchPack(uint256 const& hash, std::shared_ptr<Blob> data);

    /**
     * Consumes one cached fetch-pack node.
     *
     * @param hash Hash of the node wanted.
     * @return The node, or nullopt when it is absent or its contents do not
     * hash to the key. Either way the entry is removed, so a second call for
     * the same hash returns nothing.
     */
    std::optional<Blob>
    getFetchPack(uint256 const& hash) override;

    /**
     * Builds a fetch pack of the ledgers preceding the one a peer says it has,
     * and sends it. Silently declines when the node is too loaded, too far
     * behind, or the request is over a second old.
     *
     * @param wPeer Peer to reply to; nothing is sent if it has gone away.
     * @param request Originating request, whose ledger hash is echoed back.
     * @param haveLedgerHash Newest ledger the peer claims to have. Its parent
     * is the first one packed.
     * @param uptime Time the request was received, used as the deadline base.
     */
    void
    makeFetchPack(
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        UptimeClock::time_point uptime);

    /**
     * @return Number of nodes currently held in the fetch-pack cache.
     */
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

    /**
     * Returns the minimum ledger sequence in SQL database, if any.
     *
     * @return That sequence, or nullopt when the database holds no ledgers.
     */
    std::optional<LedgerIndex>
    minSqlSeq();

    /**
     * Iff a txn exists at the specified ledger and offset then return its txnid
     *
     * @param ledgerSeq Ledger to search. Not a precondition: a sequence above
     * the validated range yields nullopt rather than an error.
     * @param txnIndex Position of the transaction within that ledger.
     * @return The transaction ID, or nullopt when the ledger is unavailable or
     * holds no transaction at that position.
     */
    std::optional<uint256>
    txnIdFromIndex(uint32_t ledgerSeq, uint32_t txnIndex);

private:
    /**
     * Adopts a ledger as the last validated one and tells the rest of the
     * server, including the amendment-support checks that can block the node.
     *
     * @param l Ledger to adopt. Its recorded sign time is the median of the
     * trusted validations, or its own close time when there are too few.
     */
    void
    setValidLedger(std::shared_ptr<Ledger const> const& l);

    /**
     * Records a ledger as the newest one published to clients.
     *
     * @param l Ledger just published.
     */
    void
    setPubLedger(std::shared_ptr<Ledger const> const& l);

    /**
     * Walks back from a ledger through the SQL index, marking each ancestor
     * resident until the chain breaks or a known ledger is reached. Runs as a
     * job, and clears fillInProgress_ when it finishes.
     *
     * @param ledger Ledger to walk back from.
     */
    void
    tryFill(std::shared_ptr<Ledger const> ledger);

    /**
     * Request a fetch pack to get to the specified ledger
     *
     * @param missing Sequence we lack; the request is keyed on its successor.
     * @param reason Why the ledger is needed, for any hash walk this triggers.
     */
    void
    getFetchPack(LedgerIndex missing, InboundLedger::Reason reason);

    /**
     * Finds the hash of a ledger the history back-fill wants, preferring the
     * last back-filled ledger as the reference to walk from.
     *
     * @param index Ledger sequence wanted.
     * @param reason Why the ledger is needed, for any acquire this triggers.
     * @return The hash, or nullopt if no reference ledger yields it.
     */
    std::optional<LedgerHash>
    getLedgerHashForHistory(LedgerIndex index, InboundLedger::Reason reason);

    /**
     * Determines how many validations are needed to fully validate a ledger
     *
     * @return Number of validations needed
     */
    std::size_t
    getNeededValidations();

    /**
     * Acquires one missing historical ledger, falling back to a fetch pack and
     * then to prefetching a batch of its predecessors.
     *
     * @param missing Sequence to acquire.
     * @param progress Set to true when history moved forward, and also when no
     * hash could be found at all, so the caller stops retrying that sequence.
     * @param reason Why the ledger is needed, passed to the acquire.
     * @param sl Lock on mutex_, released for the duration of the network work.
     */
    void
    fetchForHistory(
        std::uint32_t missing,
        bool& progress,
        InboundLedger::Reason reason,
        std::unique_lock<std::recursive_mutex>&);

    /**
     * Try to publish ledgers, acquire missing ledgers.  Always called with
     * mutex_ locked.  The passed lock is a reminder to callers.
     *
     * Publishing takes priority; history is only back-filled when the node is
     * caught up, unloaded and idle. Loops until no progress is made.
     *
     * @param sl Lock on mutex_, released around publication and network work.
     */
    void
    doAdvance(std::unique_lock<std::recursive_mutex>&);

    /**
     * Collects the next run of validated ledgers to publish, acquiring or
     * replaying the ones that are missing.
     *
     * @param sl Lock on mutex_, released while ledgers are fetched.
     * @return Ledgers to publish in ascending order, empty when there is
     * nothing to do. A gap of over 100 ledgers is skipped rather than filled,
     * and the validated ledger alone is returned.
     */
    std::vector<std::shared_ptr<Ledger const>>
    findNewLedgersToPublish(std::unique_lock<std::recursive_mutex>&);

    /**
     * Runs pending pathfinding requests against the newest suitable ledger
     * until none are left. Runs as a job, and decrements pathFindThread_ on
     * each of its early returns - but not when a job-queue shutdown ends the
     * loop, which leaves the count high for the rest of the process.
     */
    void
    updatePaths();

    /**
     * A thread needs to be dispatched to handle pathfinding work of some kind.
     *
     * Returns true if work started.  Always called with mutex_ locked.
     * The passed lock is a reminder to callers.
     *
     * @param name Job name, for the perf log.
     * @param sl Lock on mutex_, held throughout.
     * @return true when a pathfinding worker is running and the server is not
     * shutting down, so the caller may expect its request to be serviced.
     */
    bool
    newPFWork(char const* name, std::unique_lock<std::recursive_mutex>&);

    /**
     * Owning application, the route to every other subsystem.
     */
    Application& app_;

    /**
     * Log sink for this component.
     */
    beast::Journal journal_;

    /**
     * Guards the tracked ledgers, the held transactions and the job
     * bookkeeping below. Recursive because the advance and pathfinding paths
     * re-enter public accessors. Taken before completeLock_.
     */
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

    /**
     * Cache of ledgers by hash and of validated sequence-to-hash mappings.
     */
    LedgerHistory ledgerHistory_;

    /**
     * Transactions deferred to the next open ledger, in canonical order.
     */
    CanonicalTXSet heldTransactions_{uint256()};

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData_;

    /**
     * Guards completeLedgers_ only. Always taken after mutex_.
     */
    std::recursive_mutex mutable completeLock_;

    /**
     * Sequences of every ledger held locally in full.
     */
    RangeSet<std::uint32_t> completeLedgers_;

    // Publish thread is running.
    bool advanceThread_{false};

    // Publish thread has work to do.
    bool advanceWork_{false};

    /**
     * Sequence a tryFill() job is currently walking back from, or 0 when no
     * fill is running. Keeps back-fill from competing with itself.
     */
    int fillInProgress_{0};

    /**
     * Never allowed above two, so pathfinding cannot starve other jobs.
     */
    int pathFindThread_{0};  // Pathfinder jobs dispatched

    /**
     * A pathfinding request arrived and has not been picked up yet.
     */
    bool pathFindNewRequest_{false};

    /**
     * Set while a GotFetchPack job is outstanding, so only one ever is.
     */
    std::atomic_flag gotFetchPackThread_ = ATOMIC_FLAG_INIT;  // GotFetchPack jobs dispatched

    /**
     * Close time of pubLedger_, in seconds since the network epoch; 0 if none.
     */
    std::atomic<std::uint32_t> pubLedgerClose_{0};

    /**
     * Sequence of pubLedger_; 0 if nothing has been published.
     */
    std::atomic<LedgerIndex> pubLedgerSeq_{0};

    /**
     * Sign time of validLedger_, in seconds since the network epoch: the median
     * of its trusted validations, or its close time when those were too few.
     */
    std::atomic<std::uint32_t> validLedgerSign_{0};

    /**
     * Sequence of validLedger_; 0 if nothing is validated.
     */
    std::atomic<LedgerIndex> validLedgerSeq_{0};

    /**
     * Sequence consensus is building; 0 when it is not building one.
     */
    std::atomic<LedgerIndex> buildingLedgerSeq_{0};

    // The server is in standalone mode
    bool const standalone_;

    // How many ledgers before the current ledger do we allow peers to request?
    std::uint32_t const fetchDepth_;

    // How much history do we want to keep
    std::uint32_t const ledgerHistorySize_;

    /**
     * Cap on ledgers acquired in one publication or prefetch pass.
     */
    std::uint32_t const ledgerFetchSize_;

    /**
     * Fetch-pack nodes keyed by node hash, expiring 45 seconds after use.
     */
    TaggedCache<uint256, Blob> fetchPacks_;

    /**
     * Sequence a fetch pack was last requested for, to avoid asking twice.
     */
    std::uint32_t fetchSeq_{0};

    // Try to keep a validator from switching from test to live network
    // without first wiping the database.
    LedgerIndex const maxLedgerDifference_{1000000};

    // Time that the previous upgrade warning was issued.
    TimeKeeper::time_point upgradeWarningPrevTime_;

private:
    /**
     * Ledger-age gauges reported to the insight collector. The collector calls
     * the hook, which samples the two ages; nothing here is configurable.
     */
    struct Stats
    {
        /**
         * Registers the hook and the gauges.
         *
         * @param handler Callable the collector invokes to sample the gauges.
         * @param collector Sink the gauges are reported to.
         */
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->makeHook(handler))
            , validatedLedgerAge(collector->makeGauge("LedgerMaster", "Validated_Ledger_Age"))
            , publishedLedgerAge(collector->makeGauge("LedgerMaster", "Published_Ledger_Age"))
        {
        }

        /**
         * Keeps the sampling callback registered for as long as Stats lives.
         */
        beast::insight::Hook hook;

        /**
         * Age of the validated ledger, in seconds.
         */
        beast::insight::Gauge validatedLedgerAge;

        /**
         * Age of the published ledger, in seconds.
         */
        beast::insight::Gauge publishedLedgerAge;
    };

    /**
     * The gauges and the collector hook that samples them.
     */
    Stats stats_;

private:
    /**
     * Samples both ledger ages into the gauges. Called by the collector on its
     * own thread, so it takes mutex_.
     */
    void
    collectMetrics()
    {
        std::scoped_lock const lock(mutex_);
        stats_.validatedLedgerAge.set(getValidatedLedgerAge().count());
        stats_.publishedLedgerAge.set(getPublishedLedgerAge().count());
    }
};

}  // namespace xrpl
