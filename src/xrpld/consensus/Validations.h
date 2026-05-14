/** @file
 *  Ledger validation tracking for the XRPL consensus engine.
 *
 *  Defines `ValidationParms`, `SeqEnforcer`, `isCurrent()`, `ValStatus`, and
 *  the primary `Validations<Adaptor>` template. Together these components
 *  receive validator attestations from the network, enforce freshness and
 *  monotonicity invariants, index them for efficient querying, and feed the
 *  `LedgerTrie` that drives preferred-ledger selection.
 *
 *  The entire implementation is generic: the `Adaptor` template parameter
 *  supplies types and callbacks so that simulations and the production rippled
 *  application can share the same logic while substituting test clocks or
 *  storage backends.
 */
#pragma once

#include <xrpld/consensus/LedgerTrie.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/container/aged_container_utility.h>
#include <xrpl/beast/container/aged_unordered_map.h>
#include <xrpl/protocol/PublicKey.h>

#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

/** Protocol timing thresholds that govern validation staleness and expiration.
 *
 *  All durations are compared against `NetClock` (second-resolution network
 *  time) for the wall-clock checks, and against `std::chrono::steady_clock`
 *  for the local-observation check.
 *
 *  @note These are protocol-level parameters; changing them without careful
 *      consideration can break consensus safety or liveness. They are
 *      intentionally *not* `static constexpr` so that simulation code can
 *      inject alternate values without recompiling.
 */
struct ValidationParms
{
    explicit ValidationParms() = default;

    /** The number of seconds a validation remains current after its ledger's
        close time.

        This is a safety to protect against very old validations and the time
        it takes to adjust the close time accuracy window.
    */
    std::chrono::seconds validationCURRENT_WALL = std::chrono::minutes{5};

    /** Duration a validation remains current after first observed.

        The number of seconds a validation remains current after the time we
        first saw it. This provides faster recovery in very rare cases where the
        number of validations produced by the network is lower than normal
    */
    std::chrono::seconds validationCURRENT_LOCAL = std::chrono::minutes{3};

    /** Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds validationCURRENT_EARLY = std::chrono::minutes{3};

    /** Duration a set of validations for a given ledger hash remain valid

        The number of seconds before a set of validations for a given ledger
        hash can expire.  This keeps validations for recent ledgers available
        for a reasonable interval.
    */
    std::chrono::seconds validationSET_EXPIRES = std::chrono::minutes{10};

    /** Maximum age of a validation (by local seen-time) for it to be
     *  considered from a *live* proposer in `laggards()` queries.
     *
     *  A validation is "fresh" if `now < seenTime + validationFRESHNESS`.
     *  This threshold is used only for online/laggard detection, not for
     *  general staleness — see `isCurrent()` for that.  The value must exceed
     *  `ledgerMAX_CONSENSUS` so that validators waiting on slow peers are not
     *  misclassified as offline.
     */
    std::chrono::seconds validationFRESHNESS = std::chrono::seconds{20};
};

/** Enforces a monotonically-increasing ledger sequence invariant per validator.
 *
 *  Each instance tracks the highest validation sequence seen for one
 *  particular validator. A new validation is accepted only when its sequence
 *  number strictly exceeds every unexpired prior sequence from that validator.
 *
 *  The high-water mark resets to zero after `validationSET_EXPIRES` elapses
 *  with no new validation, so a validator returning after a long offline
 *  period is not permanently locked out — it can start fresh at the current
 *  network sequence.
 *
 *  @tparam Seq Ledger sequence type (must be default-constructible to zero and
 *      support `<=` and `>` comparisons).
 */
template <class Seq>
class SeqEnforcer
{
    using time_point = std::chrono::steady_clock::time_point;
    Seq seq_{0};
    time_point when_;

public:
    /** Try advancing the largest observed validation ledger sequence

        Try setting the largest validation sequence observed, but return false
        if it violates the invariant that a validation must be larger than all
        unexpired validation sequence numbers.

        @param now The current time
        @param s The sequence number we want to validate
        @param p Validation parameters

        @return Whether the validation satisfies the invariant
    */
    bool
    operator()(time_point now, Seq s, ValidationParms const& p)
    {
        if (now > (when_ + p.validationSET_EXPIRES))
            seq_ = Seq{0};
        if (s <= seq_)
            return false;
        seq_ = s;
        when_ = now;
        return true;
    }

    /** Return the highest accepted sequence number, or zero if no unexpired
     *  validation has been accepted yet (including after a reset).
     */
    [[nodiscard]] Seq
    largest() const
    {
        return seq_;
    }
};

/** Determine whether a validation is still considered current.
 *
 *  Checks two independent time conditions:
 *  1. **Sign-time window** — the validator's declared signing time must fall
 *     within `(now - validationCURRENT_EARLY, now + validationCURRENT_WALL)`,
 *     rejecting both ancient and future-dated attestations.
 *  2. **Seen-time backstop** — if the local node has recorded a first-seen
 *     time, it must be no later than `now + validationCURRENT_LOCAL`, guarding
 *     against extreme local clock drift.
 *
 *  @note Because `signTime` originates from an untrusted remote node, all
 *      arithmetic is performed after promoting the unsigned 32-bit NetClock
 *      epoch values to signed 64-bit to prevent underflow on subtraction.
 *
 *  @param p        Timing thresholds governing the staleness windows.
 *  @param now      Current network time used as the reference point.
 *  @param signTime The time the validation was signed by the remote node.
 *  @param seenTime When this node first observed the validation; pass the
 *                  default-constructed `NetClock::time_point{}` if unknown.
 *  @return `true` if the validation passes both time-window checks.
 */
inline bool
isCurrent(
    ValidationParms const& p,
    NetClock::time_point now,
    NetClock::time_point signTime,
    NetClock::time_point seenTime)
{
    return (signTime > (now - p.validationCURRENT_EARLY)) &&
        (signTime < (now + p.validationCURRENT_WALL)) &&
        ((seenTime == NetClock::time_point{}) || (seenTime < (now + p.validationCURRENT_LOCAL)));
}

/** Classification of a received validation returned by `Validations::add()`.
 *
 *  The values form a rough severity ordering. Only `Current` means the
 *  validation was accepted and stored. All other values indicate the
 *  validation was discarded, with different implications for monitoring:
 *  `Conflicting` is the most serious (possible Byzantine behavior), while
 *  `Stale` is routine churn.
 */
enum class ValStatus {
    /// Validation was new and accepted into the tracking structures.
    Current,
    /// Validation was not current (too old or older than an existing one from
    /// this node); discarded without further processing.
    Stale,
    /// Sequence number is not strictly greater than an unexpired prior
    /// validation from the same node; plain regression, not necessarily
    /// malicious.
    BadSeq,
    /// Same node, same sequence, same ledger but different cookie — likely
    /// accidental misconfiguration (e.g., duplicate validator restart).
    Multiple,
    /// Same node, same sequence, different ledger or different sign time —
    /// indicates a potentially Byzantine validator signing conflicting ledgers.
    Conflicting
};

/** Convert a `ValStatus` to its lowercase string representation for logging.
 *
 *  @param m The status value to convert.
 *  @return A lowercase string (`"current"`, `"stale"`, `"badSeq"`,
 *          `"multiple"`, `"conflicting"`, or `"unknown"`).
 */
inline std::string
to_string(ValStatus m)
{
    switch (m)
    {
        case ValStatus::Current:
            return "current";
        case ValStatus::Stale:
            return "stale";
        case ValStatus::BadSeq:
            return "badSeq";
        case ValStatus::Multiple:
            return "multiple";
        case ValStatus::Conflicting:
            return "conflicting";
        default:
            return "unknown";
    }
}

/** Tracks ledger validations received from the network and drives preferred-
 *  ledger selection.
 *
 *  Maintains five coordinated data structures under a single `Mutex`:
 *  - `current_` — most recent valid validation per node (fast quorum path).
 *  - `byLedger_` — all validations indexed by ledger ID with LRU expiry.
 *  - `bySequence_` — validations indexed by sequence for Byzantine detection.
 *  - `trie_` — `LedgerTrie` reflecting all trusted validators for preferred-
 *    ledger computation.
 *  - `acquiring_` — trusted validations waiting on locally-unavailable ledgers.
 *
 *  The critical path is `add()`, which enforces freshness (via `isCurrent()`),
 *  monotonic sequence (via `SeqEnforcer`), and Byzantine detection before
 *  inserting into the indexes and the trie.
 *
 *  All trie queries flow through `withTrie()`, which flushes stale entries
 *  and promotes pending acquisitions before delegating to the trie — ensuring
 *  the trie is always consistent with `current_`.
 *
 *  @warning `Adaptor::Mutex` protects all private members of this class but
 *      does *not* cover the `Adaptor` instance itself — the adaptor manages
 *      its own synchronization.
 *
 *  @note Stored validations are not necessarily from trusted nodes. Callers
 *      must use the `trusted`-prefixed query methods or check
 *      `Validation::trusted()` explicitly.
 *
 *  @tparam Adaptor Supplies `Mutex`, `Validation`, `Ledger`, `now()`, and
 *      `acquire()`. See the code stub below for the full interface contract.
 *
 *  @code

    // Conforms to the Ledger type requirements of LedgerTrie
    struct Ledger;

    struct Validation
    {
        using NodeID = ...;
        using NodeKey = ...;

        // Ledger ID associated with this validation
        Ledger::ID ledgerID() const;

        // Sequence number of validation's ledger (0 means no sequence number)
        Ledger::Seq seq() const

        // When the validation was signed
        NetClock::time_point signTime() const;

        // When the validation was first observed by this node
        NetClock::time_point seenTime() const;

        // Signing key of node that published the validation
        NodeKey key() const;

        // Whether the publishing node was trusted at the time the validation
        // arrived
        bool trusted() const;

        // Set the validation as trusted
        void setTrusted();

        // Set the validation as untrusted
        void setUntrusted();

        // Whether this is a full or partial validation
        bool full() const;

        // Identifier for this node that remains fixed even when rotating
        // signing keys
        NodeID nodeID()  const;

        implementation_specific_t
        unwrap() -> return the implementation-specific type being wrapped

        // ... implementation specific
    };

    class Adaptor
    {
        using Mutex = std::mutex;
        using Validation = Validation;
        using Ledger = Ledger;

        // Return the current network time (used to determine staleness)
        NetClock::time_point now() const;

        // Attempt to acquire a specific ledger.
        std::optional<Ledger> acquire(Ledger::ID const & ledgerID);

        // ... implementation specific
    };
    @endcode

*/
template <class Adaptor>
class Validations
{
    using Mutex = typename Adaptor::Mutex;
    using Validation = typename Adaptor::Validation;
    using Ledger = typename Adaptor::Ledger;
    using ID = typename Ledger::ID;
    using Seq = typename Ledger::Seq;
    using NodeID = typename Validation::NodeID;
    using NodeKey = typename Validation::NodeKey;

    using WrappedValidationType =
        std::decay_t<std::invoke_result_t<decltype(&Validation::unwrap), Validation>>;

    // Protects all mutable state below. The adaptor_ member is explicitly
    // excluded and manages its own synchronization.
    mutable Mutex mutex_;

    // Most recent valid validation from each known node (partial and full).
    // Continuously pruned for staleness by current(); the fast path for
    // quorum and laggard queries.
    hash_map<NodeID, Validation> current_;

    // SeqEnforcer for the local node; tracks the highest sequence this node
    // has validated so that canValidateSeq() can enforce the monotonic
    // sequence invariant locally.
    SeqEnforcer<Seq> localSeqEnforcer_;

    // Per-peer SeqEnforcers used in add() to reject sequence regressions and
    // to classify Byzantine violations.
    hash_map<NodeID, SeqEnforcer<Seq>> seqEnforcers_;

    //! All validations (partial and full) grouped by ledger ID with LRU-style
    //! time-based expiry via beast::expire(). Access via byLedger() to get
    //! automatic LRU touch.
    beast::aged_unordered_map<
        ID,
        hash_map<NodeID, Validation>,
        std::chrono::steady_clock,
        beast::Uhash<>>
        byLedger_;

    // All validations (partial and full) grouped by ledger sequence number.
    // Used exclusively in add() for Byzantine detection — allows checking
    // whether any prior validation from the same node exists for the same
    // sequence.
    beast::aged_unordered_map<
        Seq,
        hash_map<NodeID, Validation>,
        std::chrono::steady_clock,
        beast::Uhash<>>
        bySequence_;

    // Half-open range [low, high) of sequence numbers that expire() must not
    // evict. Set via setSeqToKeep(); entries are "touched" just before their
    // natural expiry time to reset their LRU timestamp.
    struct KeepRange
    {
        Seq low;
        Seq high;
    };
    std::optional<KeepRange> toKeep_;

    // Compressed prefix trie over ledger ancestry, keyed on sequence-indexed
    // ancestor IDs. Drives getPreferred(). Must only be accessed through
    // withTrie() to ensure stale entries are flushed first.
    LedgerTrie<Ledger> trie_;

    // Maps each node to the last ledger it contributed to the trie. Used by
    // removeTrie() to atomically undo a node's prior trie contribution before
    // inserting the new one, keeping trie updates effectively atomic.
    hash_map<NodeID, Ledger> lastLedger_;

    // Trusted validations whose target ledger has not yet been locally
    // acquired. Keyed by (Seq, ID); value is the set of validating nodes.
    // When the ledger becomes available, all nodes are promoted into the trie.
    hash_map<std::pair<Seq, ID>, hash_set<NodeID>> acquiring_;

    // Staleness and expiration thresholds; immutable after construction.
    ValidationParms const parms_;

    // Adaptor instance — NOT protected by mutex_; adaptor manages its own
    // synchronization.
    Adaptor adaptor_;

private:
    /** Remove a node's contribution to the trie and/or acquiring_ map.
     *
     *  Erases `nodeID` from `acquiring_` for the ledger described by `val`
     *  (if present) and, if `lastLedger_` records the same ledger for that
     *  node, removes it from `trie_` and clears the `lastLedger_` entry.
     *
     *  @param lock  Proof that the caller holds `mutex_`.
     *  @param nodeID  The node whose trie contribution is being withdrawn.
     *  @param val  The validation identifying the ledger to un-register.
     */
    void
    removeTrie(std::scoped_lock<Mutex> const&, NodeID const& nodeID, Validation const& val)
    {
        {
            auto it = acquiring_.find(std::make_pair(val.seq(), val.ledgerID()));
            if (it != acquiring_.end())
            {
                it->second.erase(nodeID);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }
        {
            auto it = lastLedger_.find(nodeID);
            if (it != lastLedger_.end() && it->second.id() == val.ledgerID())
            {
                trie_.remove(it->second);
                lastLedger_.erase(nodeID);
            }
        }
    }

    /** Promote any pending acquisitions whose ledger is now locally available.
     *
     *  Iterates `acquiring_` and calls `adaptor_.acquire()` for each pending
     *  ledger. For any that are now available, inserts all waiting node IDs
     *  into the trie and erases the entry from `acquiring_`.
     *
     *  @param lock  Proof that the caller holds `mutex_`.
     */
    void
    checkAcquired(std::scoped_lock<Mutex> const& lock)
    {
        for (auto it = acquiring_.begin(); it != acquiring_.end();)
        {
            if (std::optional<Ledger> ledger = adaptor_.acquire(it->first.second))
            {
                for (NodeID const& nodeID : it->second)
                    updateTrie(lock, nodeID, *ledger);

                it = acquiring_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    /** Insert or update a node's trie contribution with a directly-acquired ledger.
     *
     *  If the node already has a `lastLedger_` entry, removes the old ledger
     *  from the trie before inserting the new one, keeping the trie consistent.
     *
     *  @param lock    Proof that the caller holds `mutex_`.
     *  @param nodeID  The validating node.
     *  @param ledger  The locally-acquired ledger to register in the trie.
     */
    void
    updateTrie(std::scoped_lock<Mutex> const&, NodeID const& nodeID, Ledger ledger)
    {
        auto const [it, inserted] = lastLedger_.emplace(nodeID, ledger);
        if (!inserted)
        {
            trie_.remove(it->second);
            it->second = ledger;
        }
        trie_.insert(ledger);
    }

    /** Process a new validation

        Process a new trusted validation from a validator. This will be
        reflected only after the validated ledger is successfully acquired by
        the local node. In the interim, the prior validated ledger from this
        node remains.

        @param lock Existing lock of mutex_
        @param nodeID The node identifier of the validating node
        @param val The trusted validation issued by the node
        @param prior If not none, the last current validated ledger Seq,ID of
                     key
    */
    void
    updateTrie(
        std::scoped_lock<Mutex> const& lock,
        NodeID const& nodeID,
        Validation const& val,
        std::optional<std::pair<Seq, ID>> prior)
    {
        XRPL_ASSERT(val.trusted(), "xrpl::Validations::updateTrie : trusted input validation");

        // Clear any prior acquiring ledger for this node
        if (prior)
        {
            auto it = acquiring_.find(*prior);
            if (it != acquiring_.end())
            {
                it->second.erase(nodeID);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }

        checkAcquired(lock);

        std::pair<Seq, ID> const valPair{val.seq(), val.ledgerID()};
        auto it = acquiring_.find(valPair);
        if (it != acquiring_.end())
        {
            it->second.insert(nodeID);
        }
        else
        {
            if (std::optional<Ledger> ledger = adaptor_.acquire(val.ledgerID()))
            {
                updateTrie(lock, nodeID, *ledger);
            }
            else
            {
                acquiring_[valPair].insert(nodeID);
            }
        }
    }

    /** Execute a query against the trie after flushing stale state.
     *
     *  This is the **only** sanctioned entry point into the trie. It first
     *  calls `current()` to evict stale entries from `current_` (and remove
     *  their trie contributions), then calls `checkAcquired()` to promote any
     *  newly-available ledgers. This lazy-flush design keeps the trie accurate
     *  without a background sweep.
     *
     *  @param lock  Proof that the caller holds `mutex_`.
     *  @param f     Callable with signature `auto(LedgerTrie<Ledger>&)`.
     *               Invoked with the trie under lock; must be a simple,
     *               non-blocking transformation — no external I/O.
     *  @return      Whatever `f` returns.
     */
    template <class F>
    auto
    withTrie(std::scoped_lock<Mutex> const& lock, F&& f)
    {
        current(lock, [](auto) {}, [](auto, auto) {});
        checkAcquired(lock);
        return f(trie_);
    }

    /** Iterate all non-stale entries in `current_`, evicting stale ones.
     *
     *  Walks `current_`, calling `isCurrent()` on each entry. Stale entries
     *  are removed from `current_` and their trie contributions withdrawn via
     *  `removeTrie()`. Live entries are passed to `f`.
     *
     *  @param lock  Proof that the caller holds `mutex_`.
     *  @param pre   Called once before the loop with the *current* (pre-
     *               eviction) map size — useful for `reserve()` calls.
     *               Signature: `void(std::size_t)`.
     *  @param f     Called for each live entry. Signature:
     *               `void(NodeID const&, Validation const&)`. Invoked under
     *               lock; must be a simple, non-blocking transformation.
     *
     *  @note `pre` receives an upper bound, not an exact count: stale entries
     *      are evicted *during* iteration, so the actual number of `f` calls
     *      may be lower.
     */

    template <class Pre, class F>
    void
    current(std::scoped_lock<Mutex> const& lock, Pre&& pre, F&& f)
    {
        NetClock::time_point const t = adaptor_.now();
        pre(current_.size());
        auto it = current_.begin();
        while (it != current_.end())
        {
            if (!isCurrent(parms_, t, it->second.signTime(), it->second.seenTime()))
            {
                removeTrie(lock, it->first, it->second);
                it = current_.erase(it);
            }
            else
            {
                auto cit = typename decltype(current_)::const_iterator{it};
                f(cit->first, cit->second);
                ++it;
            }
        }
    }

    /** Iterate all validations stored for a specific ledger ID.
     *
     *  Looks up `ledgerID` in `byLedger_` and, if found, touches the entry
     *  (resetting its LRU timestamp) then invokes `pre` and `f`.
     *
     *  @param lock      Proof that the caller holds `mutex_`.
     *  @param ledgerID  The ledger hash to query.
     *  @param pre       Called with the exact count of validations before any
     *                   `f` invocations — suitable for `reserve()`. Signature:
     *                   `void(std::size_t)`.
     *  @param f         Called for each stored validation. Signature:
     *                   `void(NodeID const&, Validation const&)`. Invoked
     *                   under lock; must be a simple, non-blocking
     *                   transformation.
     *
     *  @note Does nothing (no calls to `pre` or `f`) if `ledgerID` is absent
     *      from `byLedger_`.
     */
    template <class Pre, class F>
    void
    byLedger(std::scoped_lock<Mutex> const&, ID const& ledgerID, Pre&& pre, F&& f)
    {
        auto it = byLedger_.find(ledgerID);
        if (it != byLedger_.end())
        {
            // Update set time since it is being used
            byLedger_.touch(it);
            pre(it->second.size());
            for (auto const& [key, val] : it->second)
                f(key, val);
        }
    }

public:
    /** Constructor

        @param p ValidationParms to control staleness/expiration of validations
        @param c Clock to use for expiring validations stored by ledger
        @param ts Parameters for constructing Adaptor instance
    */
    template <class... Ts>
    Validations(
        ValidationParms const& p,
        beast::AbstractClock<std::chrono::steady_clock>& c,
        Ts&&... ts)
        : byLedger_(c), bySequence_(c), parms_(p), adaptor_(std::forward<Ts>(ts)...)
    {
    }

    /** Return the adaptor instance
     */
    Adaptor const&
    adaptor() const
    {
        return adaptor_;
    }

    /** Return the validation timing parameters
     */
    ValidationParms const&
    parms() const
    {
        return parms_;
    }

    /** Check whether the local node may issue a validation for sequence `s`.
     *
     *  Delegates to the local `SeqEnforcer` — returns `true` only if `s`
     *  strictly exceeds every unexpired sequence previously validated by this
     *  node. On success, `s` becomes the new high-water mark. On failure, the
     *  high-water mark is unchanged.
     *
     *  @param s  The ledger sequence number the local node wants to validate.
     *  @return   `true` if the validation may proceed; `false` if `s` would
     *            violate the monotonic sequence invariant.
     */
    bool
    canValidateSeq(Seq const s)
    {
        std::scoped_lock const lock{mutex_};
        return localSeqEnforcer_(byLedger_.clock().now(), s, parms_);
    }

    /** Process and store an incoming validation.
     *
     *  Executes the following checks in order before storing:
     *  1. **Freshness** — `isCurrent()` is called *before* acquiring the lock;
     *     stale validations are discarded immediately as `ValStatus::Stale`.
     *  2. **Byzantine detection** — `bySequence_` is inspected for any prior
     *     validation from `nodeID` at the same sequence number. Conflicts on
     *     ledger ID or sign time return `ValStatus::Conflicting`; same-ledger
     *     cookie collisions return `ValStatus::Multiple`.
     *  3. **Monotonic sequence** — the per-node `SeqEnforcer` rejects
     *     regressions as `ValStatus::BadSeq`.
     *  4. **Currency** — if a newer validation from this node already exists
     *     in `current_`, the incoming one is discarded as `ValStatus::Stale`.
     *
     *  Trusted validations that pass all checks are routed to `updateTrie()`,
     *  which either inserts them into the trie immediately (if the ledger is
     *  locally available) or parks `nodeID` in `acquiring_` to wait.
     *
     *  @param nodeID  The stable node identifier of the validating node.
     *  @param val     The validation to store.
     *  @return        Classification of the validation (see `ValStatus`). Only
     *                 `ValStatus::Current` means the validation was accepted.
     */
    ValStatus
    add(NodeID const& nodeID, Validation const& val)
    {
        if (!isCurrent(parms_, adaptor_.now(), val.signTime(), val.seenTime()))
            return ValStatus::Stale;

        {
            std::scoped_lock const lock{mutex_};

            // Check that validation sequence is greater than any non-expired
            // validations sequence from that validator; if it's not, perform
            // additional work to detect Byzantine validations
            auto const now = byLedger_.clock().now();

            auto const [seqit, seqinserted] = bySequence_[val.seq()].emplace(nodeID, val);

            if (!seqinserted)
            {
                // Check if the entry we're already tracking was signed
                // long enough ago that we can disregard it.
                auto const diff = std::max(seqit->second.signTime(), val.signTime()) -
                    std::min(seqit->second.signTime(), val.signTime());

                if (diff > parms_.validationCURRENT_WALL &&
                    val.signTime() > seqit->second.signTime())
                    seqit->second = val;
            }

            // Enforce monotonically increasing sequences for validations
            // by a given node, and run the active Byzantine detector:
            if (auto& enf = seqEnforcers_[nodeID]; !enf(now, val.seq(), parms_))
            {
                // If the validation is for the same sequence as one we are
                // tracking, check it closely:
                if (seqit->second.seq() == val.seq())
                {
                    // Two validations for the same sequence but for different
                    // ledgers. This could be the result of misconfiguration
                    // but it can also mean a Byzantine validator.
                    if (seqit->second.ledgerID() != val.ledgerID())
                        return ValStatus::Conflicting;

                    // Two validations for the same sequence and for the same
                    // ledger with different sign times. This could be the
                    // result of a misconfiguration but it can also mean a
                    // Byzantine validator.
                    if (seqit->second.signTime() != val.signTime())
                        return ValStatus::Conflicting;

                    // Two validations for the same sequence but with different
                    // cookies. This is probably accidental misconfiguration.
                    if (seqit->second.cookie() != val.cookie())
                        return ValStatus::Multiple;
                }

                return ValStatus::BadSeq;
            }

            byLedger_[val.ledgerID()].insert_or_assign(nodeID, val);

            auto const [it, inserted] = current_.emplace(nodeID, val);
            if (!inserted)
            {
                // Replace existing only if this one is newer
                Validation const& oldVal = it->second;
                if (val.signTime() > oldVal.signTime())
                {
                    std::pair<Seq, ID> old(oldVal.seq(), oldVal.ledgerID());
                    it->second = val;
                    if (val.trusted())
                        updateTrie(lock, nodeID, val, old);
                }
                else
                {
                    return ValStatus::Stale;
                }
            }
            else if (val.trusted())
            {
                updateTrie(lock, nodeID, val, std::nullopt);
            }
        }

        return ValStatus::Current;
    }

    /** Pin a half-open range of sequence numbers against expiry.
     *
     *  `expire()` normally evicts `byLedger_` and `bySequence_` entries older
     *  than `validationSET_EXPIRES`. This method designates the range
     *  `[low, high)` as protected: `expire()` will "touch" those entries just
     *  before their natural expiry time, resetting the LRU timestamp and
     *  preventing eviction.
     *
     *  @param low   First sequence number to protect (inclusive).
     *  @param high  One past the last sequence number to protect (exclusive).
     *  @pre  `low < high`.
     *
     *  @note Only one range can be active at a time; calling this again
     *      replaces the previous range.
     */
    void
    setSeqToKeep(Seq const& low, Seq const& high)
    {
        std::scoped_lock const lock{mutex_};
        XRPL_ASSERT(low < high, "xrpl::Validations::setSeqToKeep : valid inputs");
        toKeep_ = {low, high};
    }

    /** Evict aged entries from `byLedger_` and `bySequence_`.
     *
     *  Calls `beast::expire()` on both indexes, removing entries that have
     *  not been touched for longer than `validationSET_EXPIRES`.
     *
     *  If a `setSeqToKeep()` range is active, this method first "touches" all
     *  matching entries — but only once per `(validationSET_EXPIRES -
     *  validationFRESHNESS)` window — to reset their LRU timestamps and
     *  prevent premature eviction.
     *
     *  @param j  Journal for debug-level timing diagnostics.
     */
    void
    expire(beast::Journal& j)
    {
        auto const start = std::chrono::steady_clock::now();
        {
            std::scoped_lock const lock{mutex_};
            if (toKeep_)
            {
                // We only need to refresh the keep range when it's just about
                // to expire. Track the next time we need to refresh.
                static std::chrono::steady_clock::time_point kREFRESH_TIME;
                if (auto const now = byLedger_.clock().now(); kREFRESH_TIME <= now)
                {
                    // The next refresh time is shortly before the expiration
                    // time from now.
                    kREFRESH_TIME = now + parms_.validationSET_EXPIRES - parms_.validationFRESHNESS;

                    for (auto i = byLedger_.begin(); i != byLedger_.end(); ++i)
                    {
                        auto const& validationMap = i->second;
                        if (!validationMap.empty())
                        {
                            auto const seq = validationMap.begin()->second.seq();
                            if (toKeep_->low <= seq && seq < toKeep_->high)
                            {
                                byLedger_.touch(i);
                            }
                        }
                    }

                    for (auto i = bySequence_.begin(); i != bySequence_.end(); ++i)
                    {
                        if (toKeep_->low <= i->first && i->first < toKeep_->high)
                        {
                            bySequence_.touch(i);
                        }
                    }
                }
            }

            beast::expire(byLedger_, parms_.validationSET_EXPIRES);
            beast::expire(bySequence_, parms_.validationSET_EXPIRES);
        }
        JLOG(j.debug()) << "Validations sets sweep lock duration "
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count()
                        << "ms";
    }

    /** Propagate a UNL membership change through all stored validations.
     *
     *  Iterates both `current_` and the full `byLedger_` index to mark
     *  validations as trusted or untrusted. Additionally updates the trie so
     *  that it reflects only currently-trusted validators:
     *  - Nodes in `added` have their `current_` entry inserted into the trie.
     *  - Nodes in `removed` have their trie contribution withdrawn.
     *
     *  @param added    Node IDs now considered trusted (joined the UNL).
     *  @param removed  Node IDs no longer considered trusted (left the UNL).
     *
     *  @note This iterates the entire `byLedger_` map; it is not on the hot
     *      path but may be slow if the map is large.
     */
    void
    trustChanged(hash_set<NodeID> const& added, hash_set<NodeID> const& removed)
    {
        std::scoped_lock const lock{mutex_};

        for (auto& [nodeId, validation] : current_)
        {
            if (added.find(nodeId) != added.end())
            {
                validation.setTrusted();
                updateTrie(lock, nodeId, validation, std::nullopt);
            }
            else if (removed.find(nodeId) != removed.end())
            {
                validation.setUntrusted();
                removeTrie(lock, nodeId, validation);
            }
        }

        for (auto& [_, validationMap] : byLedger_)
        {
            (void)_;
            for (auto& [nodeId, validation] : validationMap)
            {
                if (added.find(nodeId) != added.end())
                {
                    validation.setTrusted();
                }
                else if (removed.find(nodeId) != removed.end())
                {
                    validation.setUntrusted();
                }
            }
        }
    }

    /** Return a JSON representation of the ledger trie for diagnostics.
     *
     *  @return JSON object describing the trie's current structure and support
     *          counts; intended for debug endpoints and monitoring tools.
     */
    json::Value
    getJsonTrie() const
    {
        std::scoped_lock const lock{mutex_};
        return trie_.getJson();
    }

    /** Determine the preferred working ledger according to trusted validators.
     *
     *  Three-tier fallback:
     *  1. **Trie** — delegates to `LedgerTrie::getPreferred()` using the
     *     local high-water sequence, which weights branches by trusted-validator
     *     support while accounting for uncommitted validators.
     *  2. **Acquiring** — if the trie has no trusted entries, falls back to the
     *     `acquiring_` map entry with the most waiting validators (tie-broken
     *     by ledger ID).
     *  3. **Nullopt** — if both are empty, returns `std::nullopt`, signalling
     *     the caller to rely on raw peer counts.
     *
     *  Conservative switch rule: if the preferred ledger is the immediate child
     *  of `curr` (same ancestry), the node stays on `curr` — it may be about to
     *  build that child itself. A genuinely different chain or a ledger further
     *  ahead overrides the working ledger unconditionally.
     *
     *  @param curr  The local node's current working ledger.
     *  @return      `{seq, id}` of the preferred ledger, or `std::nullopt` if
     *               there are no trusted validations to guide the choice.
     */
    std::optional<std::pair<Seq, ID>>
    getPreferred(Ledger const& curr)
    {
        std::scoped_lock const lock{mutex_};
        std::optional<SpanTip<Ledger>> preferred = withTrie(lock, [this](LedgerTrie<Ledger>& trie) {
            return trie.getPreferred(localSeqEnforcer_.largest());
        });
        if (!preferred)
        {
            auto it = std::max_element(
                acquiring_.begin(), acquiring_.end(), [](auto const& a, auto const& b) {
                    std::pair<Seq, ID> const& aKey = a.first;
                    typename hash_set<NodeID>::size_type const& aSize = a.second.size();
                    std::pair<Seq, ID> const& bKey = b.first;
                    typename hash_set<NodeID>::size_type const& bSize = b.second.size();
                    return std::tie(aSize, aKey.second) < std::tie(bSize, bKey.second);
                });
            if (it != acquiring_.end())
                return it->first;
            return std::nullopt;
        }

        if (preferred->seq == curr.seq() + Seq{1} && preferred->ancestor(curr.seq()) == curr.id())
            return std::make_pair(curr.seq(), curr.id());

        if (preferred->seq > curr.seq())
            return std::make_pair(preferred->seq, preferred->id);

        if (curr[preferred->seq] != preferred->id)
            return std::make_pair(preferred->seq, preferred->id);

        return std::make_pair(curr.seq(), curr.id());
    }

    /** Return the ID of the preferred working ledger, subject to a minimum
     *  sequence constraint.
     *
     *  Delegates to `getPreferred(Ledger const&)`. If the preferred ledger's
     *  sequence is at least `minValidSeq` it is returned; otherwise the
     *  current working ledger's ID is returned unchanged.
     *
     *  @param curr         Current working ledger.
     *  @param minValidSeq  Minimum acceptable sequence number for the result.
     *  @return             ID of the preferred ledger if its sequence ≥
     *                      `minValidSeq`, otherwise `curr.id()`.
     */
    ID
    getPreferred(Ledger const& curr, Seq minValidSeq)
    {
        std::optional<std::pair<Seq, ID>> preferred = getPreferred(curr);
        if (preferred && preferred->first >= minValidSeq)
            return preferred->second;
        return curr.id();
    }

    /** Select the preferred last-closed ledger to open the next consensus round.
     *
     *  Uses trusted validations when available; falls back to raw peer counts
     *  when no trusted validations exist.
     *
     *  Decision order:
     *  1. If trusted validations are available and the preferred ledger's
     *     sequence ≥ `minSeq`, return its ID.
     *  2. If trusted validations are available but the preferred ledger is too
     *     old (sequence < `minSeq`), stay on `lcl`.
     *  3. If no trusted validations, return the ID with the highest peer count
     *     (tie-broken by ledger ID).
     *  4. If `peerCounts` is also empty, return `lcl.id()`.
     *
     *  @param lcl         Last closed ledger reported by this node.
     *  @param minSeq      Minimum acceptable sequence for a trusted result.
     *  @param peerCounts  Map from ledger ID to the number of peers reporting
     *                     that ledger as their LCL; used only when no trusted
     *                     validations are available.
     *  @return            ID of the preferred last-closed ledger.
     *
     *  @note `minSeq` applies only to the trusted-validation path; peer counts
     *      are accepted regardless of sequence because sequence is not known
     *      from `peerCounts` alone.
     */
    ID
    getPreferredLCL(Ledger const& lcl, Seq minSeq, hash_map<ID, std::uint32_t> const& peerCounts)
    {
        std::optional<std::pair<Seq, ID>> preferred = getPreferred(lcl);

        if (preferred)
            return (preferred->first >= minSeq) ? preferred->second : lcl.id();

        // max_element expects true if a < b; prefer larger counts then larger IDs on ties.
        auto it = std::max_element(peerCounts.begin(), peerCounts.end(), [](auto& a, auto& b) {
            return std::tie(a.second, a.first) < std::tie(b.second, b.first);
        });

        if (it != peerCounts.end())
            return it->first;
        return lcl.id();
    }

    /** Count current trusted validators whose validated ledger descends from
     *  `ledgerID`.
     *
     *  Used during the establish phase to assess how many peers have already
     *  advanced beyond the current preferred ledger.
     *
     *  When `ledger.id() == ledgerID`, the full trie (`branchSupport -
     *  tipSupport`) is used, counting all descendants at any depth.  When
     *  they differ (we are on a different chain), the method falls back to
     *  counting entries in `lastLedger_` whose parent ID equals `ledgerID`,
     *  i.e. immediate children only.
     *
     *  @param ledger    The local node's current working ledger.
     *  @param ledgerID  The preferred ledger whose descendants to count.
     *  @return          Number of current trusted validators working on a
     *                   ledger that descends from `ledgerID`.
     */
    std::size_t
    getNodesAfter(Ledger const& ledger, ID const& ledgerID)
    {
        std::scoped_lock const lock{mutex_};

        if (ledger.id() == ledgerID)
        {
            return withTrie(lock, [&ledger](LedgerTrie<Ledger>& trie) {
                return trie.branchSupport(ledger) - trie.tipSupport(ledger);
            });
        }

        return std::count_if(lastLedger_.begin(), lastLedger_.end(), [&ledgerID](auto const& it) {
            auto const& curr = it.second;
            return curr.seq() > Seq{0} && curr[curr.seq() - Seq{1}] == ledgerID;
        });
    }

    /** Return the unwrapped form of all current trusted full validations.
     *
     *  Flushes stale entries from `current_` (via `current()`), then returns
     *  the underlying validation objects for every entry that is both trusted
     *  and full. Partial validations are excluded.
     *
     *  @return  Vector of `WrappedValidationType` values; empty if no current
     *           trusted full validations exist.
     */
    std::vector<WrappedValidationType>
    currentTrusted()
    {
        std::vector<WrappedValidationType> ret;
        std::scoped_lock const lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                    ret.push_back(v.unwrap());
            });
        return ret;
    }

    /** Return the node IDs of all validators with a current (non-stale)
     *  validation.
     *
     *  Stale entries are evicted from `current_` as a side effect (via
     *  `current()`).  Both trusted and untrusted nodes are included.
     *
     *  @return  Set of `NodeID` values for every node that has a live entry
     *           in `current_`.
     */
    auto
    getCurrentNodeIDs() -> hash_set<NodeID>
    {
        hash_set<NodeID> ret;
        std::scoped_lock const lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeID const& nid, Validation const&) { ret.insert(nid); });

        return ret;
    }

    /** Count trusted full validations stored for a specific ledger hash.
     *
     *  Queries `byLedger_` (touching the entry to defer its expiry). Partial
     *  validations are not counted.
     *
     *  @param ledgerID  The ledger hash to query.
     *  @return          Number of trusted full validations for `ledgerID`;
     *                   zero if no validations are stored for that ledger.
     */
    std::size_t
    numTrustedForLedger(ID const& ledgerID)
    {
        std::size_t count = 0;
        std::scoped_lock const lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t) {},  // nothing to reserve
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                    ++count;
            });
        return count;
    }

    /** Return the unwrapped trusted full validations for a specific ledger.
     *
     *  Queries `byLedger_` for `ledgerID`, then filters to entries that are
     *  trusted, full, and whose recorded sequence equals `seq`. The `seq`
     *  filter guards against stale cross-sequence entries that may share a
     *  ledger ID under unusual conditions.
     *
     *  @param ledgerID  The ledger hash to query.
     *  @param seq       The expected ledger sequence number.
     *  @return          Vector of matching `WrappedValidationType` values;
     *                   empty if none match.
     */
    std::vector<WrappedValidationType>
    getTrustedForLedger(ID const& ledgerID, Seq const& seq)
    {
        std::vector<WrappedValidationType> res;
        std::scoped_lock const lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full() && v.seq() == seq)
                    res.emplace_back(v.unwrap());
            });

        return res;
    }

    /** Collect the load fees reported by trusted full validators for a ledger.
     *
     *  For each trusted full validation stored under `ledgerID`, appends the
     *  validator's reported load fee (if present) or `baseFee` as a fallback.
     *  The resulting vector is consumed by fee-scaling logic to compute a
     *  consensus fee level.
     *
     *  @param ledgerID  The ledger hash to query.
     *  @param baseFee   Fee value to use when a validator's validation does not
     *                   include an explicit load fee.
     *  @return          One fee entry per trusted full validation for
     *                   `ledgerID`; empty if no matching validations exist.
     */
    std::vector<std::uint32_t>
    fees(ID const& ledgerID, std::uint32_t baseFee)
    {
        std::vector<std::uint32_t> res;
        std::scoped_lock const lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                {
                    std::optional<std::uint32_t> loadFee = v.loadFee();
                    if (loadFee)
                    {
                        res.push_back(*loadFee);
                    }
                    else
                    {
                        res.push_back(baseFee);
                    }
                }
            });
        return res;
    }

    /** Clear all entries from `current_`.
     *
     *  Used during shutdown or test teardown to release references to
     *  validation objects. Does not evict `byLedger_` or `bySequence_`.
     */
    void
    flush()
    {
        std::scoped_lock const lock{mutex_};
        current_.clear();
    }

    /** Count lagging proposers and cull the online-proposer set.
     *
     *  Iterates current validations looking for proposers whose signing key
     *  is in `trustedKeys` and whose most-recent validation was seen within
     *  `validationFRESHNESS`. For each such *online* proposer, the key is
     *  removed from `trustedKeys` (so the caller can detect truly offline
     *  nodes as the keys left in the set) and, if that proposer's validation
     *  sequence is below `seq`, the laggard counter is incremented.
     *
     *  Callers use the laggard count and the residual `trustedKeys` set to
     *  decide whether to pause consensus and wait for slow validators.
     *
     *  @note The `Validation::trusted()` flag is deliberately ignored here;
     *      freshness of the signing key against the caller-supplied
     *      `trustedKeys` set is sufficient.
     *
     *  @param seq         The local node's current validation sequence number.
     *  @param trustedKeys In/out: public keys of all trusted proposers. Online
     *                     proposers are erased during the call; remaining keys
     *                     on return represent offline nodes.
     *  @return            Number of online proposers whose sequence < `seq`.
     */
    std::size_t
    laggards(Seq const seq, hash_set<NodeKey>& trustedKeys)
    {
        std::size_t laggards = 0;

        current(
            std::scoped_lock{mutex_},
            [](std::size_t) {},
            [&](NodeID const&, Validation const& v) {
                if (adaptor_.now() < v.seenTime() + parms_.validationFRESHNESS &&
                    trustedKeys.find(v.key()) != trustedKeys.end())
                {
                    trustedKeys.erase(v.key());
                    if (seq > v.seq())
                        ++laggards;
                }
            });

        return laggards;
    }

    /** Return the number of entries in the `current_` map (diagnostic). */
    std::size_t
    sizeOfCurrentCache() const
    {
        std::scoped_lock const lock{mutex_};
        return current_.size();
    }

    /** Return the number of per-node `SeqEnforcer` entries cached (diagnostic). */
    std::size_t
    sizeOfSeqEnforcersCache() const
    {
        std::scoped_lock const lock{mutex_};
        return seqEnforcers_.size();
    }

    /** Return the number of distinct ledger IDs in `byLedger_` (diagnostic). */
    std::size_t
    sizeOfByLedgerCache() const
    {
        std::scoped_lock const lock{mutex_};
        return byLedger_.size();
    }

    /** Return the number of distinct sequence numbers in `bySequence_` (diagnostic). */
    std::size_t
    sizeOfBySequenceCache() const
    {
        std::scoped_lock const lock{mutex_};
        return bySequence_.size();
    }
};

}  // namespace xrpl
