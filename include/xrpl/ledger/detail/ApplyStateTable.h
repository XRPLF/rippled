/** @file
 *  Declares `ApplyStateTable`, the per-transaction write-staging buffer used
 *  by all `ApplyView`/`ApplyViewImpl` instances. This is an implementation
 *  detail of `ApplyViewBase` and is not intended for direct use by transactors.
 */

#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>

namespace xrpl::detail {

/** Write-staging buffer for a single transaction's ledger mutations.
 *
 *  Every SLE touched by a transaction is recorded here — keyed by its
 *  `uint256` ledger key — along with an `Action` tag that tracks whether
 *  the entry was merely read (`Cache`), newly created (`Insert`), mutated
 *  (`Modify`), or scheduled for removal (`Erase`). On success the buffer
 *  is flushed atomically to the underlying view; on failure the table is
 *  simply discarded.
 *
 *  The class is the core member of `ApplyViewBase` and backs all
 *  `ApplyView`/`ApplyViewImpl` instances that transactors receive.
 *
 *  @note Not copyable. Move-constructible only to support placement inside
 *      `ApplyViewBase` during construction.
 *  @note `erase()` and `update()` enforce pointer-identity: the caller
 *      must pass the exact `shared_ptr` returned by `peek()` on this same
 *      table instance. Crossing views is a `LogicError`.
 */
class ApplyStateTable
{
public:
    using key_type = ReadView::key_type;

private:
    /** Lifecycle state of a buffered ledger entry. */
    enum class Action {
        Cache,   /**< Read from base; no write intent yet. */
        Erase,   /**< Scheduled for deletion from the base view. */
        Insert,  /**< New object not yet in the base view. */
        Modify,  /**< Existing object with pending mutations. */
    };

    using items_t = std::map<key_type, std::pair<Action, std::shared_ptr<SLE>>>;

    items_t items_;
    XRPAmount dropsDestroyed_{0};

public:
    ApplyStateTable() = default;
    ApplyStateTable(ApplyStateTable&&) = default;

    ApplyStateTable(ApplyStateTable const&) = delete;
    ApplyStateTable&
    operator=(ApplyStateTable&&) = delete;
    ApplyStateTable&
    operator=(ApplyStateTable const&) = delete;

    /** Flush all pending mutations to a raw view without generating metadata.
     *
     *  Maps each buffered action to a raw write on `to`: `Cache` entries
     *  are skipped; `Erase` → `rawErase`; `Insert` → `rawInsert`;
     *  `Modify` → `rawReplace`. Also forwards the accumulated
     *  `dropsDestroyed_` to `to.rawDestroyXRP()`.
     *
     *  Used when committing a sandbox or nested view back to its parent.
     *
     *  @param to The target raw view to receive the mutations.
     */
    void
    apply(RawView& to) const;

    /** Flush mutations to an open view, generating `TxMeta` for closed ledgers.
     *
     *  For closed ledgers (`!to.open()`) or dry-run mode (`isDryRun`),
     *  builds full `TxMeta` — classifying every pending item as
     *  `sfCreatedNode`, `sfModifiedNode`, or `sfDeletedNode` — and
     *  populates `sfPreviousFields`/`sfFinalFields`/`sfNewFields` using
     *  `SField` metadata flags. Threads `sfPreviousTxnID`/
     *  `sfPreviousTxnLgrSeq` onto affected account roots and trust-line
     *  endpoints.
     *
     *  In dry-run mode the metadata is produced but state changes and the
     *  raw tx insert are suppressed — supporting fee simulation without
     *  side effects.
     *
     *  A `sfModifiedNode` whose buffered content is byte-for-byte equal to
     *  the original is silently omitted from the metadata.
     *
     *  @param to The open view to commit into.
     *  @param tx The transaction being applied.
     *  @param ter The transaction result code; recorded in the metadata.
     *  @param deliver Optional delivered amount annotation for the metadata.
     *  @param parentBatchId Optional batch parent ID for the metadata.
     *  @param isDryRun If true, produce metadata but suppress state mutations.
     *  @param j Journal for diagnostic logging.
     *  @return The generated `TxMeta` when `!to.open() || isDryRun`;
     *      `std::nullopt` when the view is open and `isDryRun` is false
     *      (live open-ledger apply, no metadata needed).
     */
    std::optional<TxMeta>
    apply(
        OpenView& to,
        STTx const& tx,
        TER ter,
        std::optional<STAmount> const& deliver,
        std::optional<uint256 const> const& parentBatchId,
        bool isDryRun,
        beast::Journal j);

    /** Test whether a ledger object exists, accounting for pending changes.
     *
     *  Returns `false` for objects pending `Erase`; returns `true` for
     *  objects buffered as `Cache`, `Insert`, or `Modify`; falls back to
     *  `base.exists(k)` for keys not yet in the buffer.
     *
     *  @param base The underlying read view (base ledger state).
     *  @param k The keylet identifying the object to test.
     *  @return `true` if the object will exist after the pending changes.
     */
    [[nodiscard]] bool
    exists(ReadView const& base, Keylet const& k) const;

    /** Find the smallest key strictly greater than `key` that will exist
     *  after applying pending changes, up to but not including `last`.
     *
     *  Merges two sorted key spaces: the base ledger (skipping keys
     *  pending deletion) and the local `items_` map (skipping erased
     *  entries). Returns whichever candidate is smaller.
     *
     *  @param base The underlying read view supplying the base key space.
     *  @param key The starting key (exclusive lower bound).
     *  @param last Optional exclusive upper bound; if the result reaches
     *      or exceeds `last`, `std::nullopt` is returned.
     *  @return The next live key, or `std::nullopt` if none exists in
     *      range.
     */
    [[nodiscard]] std::optional<key_type>
    succ(ReadView const& base, key_type const& key, std::optional<key_type> const& last) const;

    /** Read a ledger object as an immutable snapshot, accounting for
     *  pending changes.
     *
     *  Returns `nullptr` for objects pending `Erase` or whose keylet
     *  check fails; returns the buffered SLE for `Cache`, `Insert`, and
     *  `Modify` entries; falls back to `base.read(k)` for unknown keys.
     *
     *  @param base The underlying read view.
     *  @param k The keylet identifying the object.
     *  @return A `const`-qualified `shared_ptr` to the SLE, or `nullptr`
     *      if the object does not exist or the keylet check fails.
     */
    [[nodiscard]] std::shared_ptr<SLE const>
    read(ReadView const& base, Keylet const& k) const;

    /** Obtain a mutable handle to a ledger object, loading it on first
     *  access.
     *
     *  If the key is not yet in the buffer, reads from `base` and stores
     *  a private copy under `Action::Cache`. Subsequent calls return the
     *  same `shared_ptr`. Returns `nullptr` for erased objects or when the
     *  object does not exist in `base`.
     *
     *  The returned pointer is the exact instance that must be passed to
     *  `update()` or `erase()` — pointer identity is enforced.
     *
     *  @param base The underlying read view.
     *  @param k The keylet identifying the object.
     *  @return A mutable `shared_ptr` to the buffered SLE, or `nullptr`.
     */
    std::shared_ptr<SLE>
    peek(ReadView const& base, Keylet const& k);

    /** Count pending mutations (Erase, Insert, Modify), excluding cache-only reads.
     *
     *  @return The number of entries with a write-intent action.
     */
    [[nodiscard]] std::size_t
    size() const;

    /** Invoke a callback for every pending write-intent entry.
     *
     *  Calls `func` once for each `Erase`, `Insert`, or `Modify` entry in
     *  the buffer. `Cache`-only entries are skipped. The `before` snapshot
     *  is read from `base` on each call; `after` is the buffered SLE.
     *
     *  @param base The underlying read view used to fetch the pre-change
     *      snapshots for `Erase` and `Modify` entries.
     *  @param func Callback invoked as
     *      `func(key, isDelete, before, after)`. `before` is `nullptr`
     *      for `Insert`; `after` is the pending SLE in all cases.
     */
    void
    visit(
        ReadView const& base,
        std::function<void(
            uint256 const& key,
            bool isDelete,
            std::shared_ptr<SLE const> const& before,
            std::shared_ptr<SLE const> const& after)> const& func) const;

    /** Mark a buffered object for deletion.
     *
     *  Transitions the action from `Cache` or `Modify` to `Erase`. If the
     *  object was previously `Insert`ed within this same transaction, the
     *  entry is removed entirely (net-zero effect on the base). Calling on
     *  an unknown key or a different `shared_ptr` than the one returned by
     *  `peek()` is a `LogicError`.
     *
     *  @param base The underlying read view (used for key lookup context).
     *  @param sle The exact `shared_ptr` previously obtained from `peek()`.
     *  @throws std::logic_error If the key is not in the buffer, the
     *      pointer does not match, or the entry is already erased.
     */
    void
    erase(ReadView const& base, std::shared_ptr<SLE> const& sle);

    /** Mark an object for deletion without enforcing pointer identity.
     *
     *  Behaves like `erase()` but accepts any SLE with the matching key —
     *  the caller-provided pointer replaces whatever is stored. Used by
     *  `ApplyViewBase` for raw-level operations that bypass the ownership
     *  protocol enforced by `erase()`.
     *
     *  @param base The underlying read view (used for key lookup context).
     *  @param sle An SLE whose key identifies the object to erase.
     *  @throws std::logic_error If the object is already pending erasure.
     */
    void
    rawErase(ReadView const& base, std::shared_ptr<SLE> const& sle);

    /** Stage a new ledger object for insertion.
     *
     *  Records the SLE under `Action::Insert`. If the key was previously
     *  erased within this same transaction, the action is collapsed to
     *  `Action::Modify` (insert-after-erase = replace). Attempting to
     *  insert over an existing `Cache`, `Insert`, or `Modify` entry is
     *  a `LogicError`.
     *
     *  @param base The underlying read view (used for key lookup context).
     *  @param sle The new SLE to insert.
     *  @throws std::logic_error If the key already exists with a
     *      non-erase action.
     */
    void
    insert(ReadView const& base, std::shared_ptr<SLE> const& sle);

    /** Promote a cached or new SLE to a definitive write.
     *
     *  Requires the exact `shared_ptr` returned by `peek()`. Transitions
     *  `Cache` → `Modify`; `Insert` and `Modify` are left unchanged
     *  (already write-intent). Calling on an erased or unknown entry is a
     *  `LogicError`.
     *
     *  @param base The underlying read view (used for key lookup context).
     *  @param sle The exact `shared_ptr` previously obtained from `peek()`.
     *  @throws std::logic_error If the key is missing, the pointer does not
     *      match, or the entry is already erased.
     */
    void
    update(ReadView const& base, std::shared_ptr<SLE> const& sle);

    /** Unconditionally overwrite the buffered SLE for a given key.
     *
     *  Records the SLE under `Action::Modify`, replacing any existing
     *  `Cache` or `Insert` entry with the supplied pointer. Calling on an
     *  erased entry is a `LogicError`. Unlike `update()`, does not enforce
     *  pointer identity — the caller supplies a fresh SLE.
     *
     *  @param base The underlying read view (used for key lookup context).
     *  @param sle The SLE to store.
     *  @throws std::logic_error If the key is currently pending erasure.
     */
    void
    replace(ReadView const& base, std::shared_ptr<SLE> const& sle);

    /** Record XRP drops destroyed by fees within this transaction's scope.
     *
     *  Accumulates into `dropsDestroyed_`, which is forwarded to
     *  `RawView::rawDestroyXRP()` on `apply()`.
     *
     *  @param fee The amount of XRP to permanently remove from circulation.
     */
    void
    destroyXRP(XRPAmount const& fee);

    /** Return the total XRP drops marked for destruction so far.
     *
     *  @return Reference to the accumulated destroyed-drops counter.
     */
    [[nodiscard]] XRPAmount const&
    dropsDestroyed() const
    {
        return dropsDestroyed_;
    }

private:
    /** Scratch map used during threading to track SLEs modified solely by
     *  metadata updates (i.e., objects whose only change is the addition of
     *  `sfPreviousTxnID`/`sfPreviousTxnLgrSeq` fields). These are kept
     *  separate from `items_` to avoid promoting cache entries to
     *  `Action::Modify` for transactional purposes.
     */
    using Mods = hash_map<key_type, std::shared_ptr<SLE>>;

    /** Update an SLE's thread fields and record the previous tx link in metadata.
     *
     *  Calls `sle->thread(txID, lgrSeq, ...)` to update `sfPreviousTxnID`
     *  and `sfPreviousTxnLgrSeq` in place. If there was a preceding
     *  transaction, adds those old fields to the affected node in `meta`
     *  so the chain of transactions is visible in on-ledger metadata.
     *
     *  @param meta The `TxMeta` object being built for the current transaction.
     *  @param sle The account-root SLE to thread.
     */
    static void
    threadItem(TxMeta& meta, std::shared_ptr<SLE> const& to);

    /** Retrieve an SLE for threading modification, using `mods` as a cache.
     *
     *  Checks `mods` first, then `items_` (returning non-cache entries
     *  directly), then falls back to `base`. Objects found in `items_` as
     *  `Action::Cache` are copied into `mods` so that threading-only
     *  mutations do not promote them to `Action::Modify` in the primary
     *  table. Returns `nullptr` when threading to a deleted or nonexistent
     *  account (e.g., an expired Escrow destination), which is legal.
     *
     *  @param base The underlying read view.
     *  @param key The ledger key of the SLE to retrieve.
     *  @param mods Scratch map of threading-only modifications.
     *  @param j Journal for warnings about missing or deleted targets.
     *  @return A mutable SLE, or `nullptr` if the account does not exist.
     */
    std::shared_ptr<SLE>
    getForMod(ReadView const& base, key_type const& key, Mods& mods, beast::Journal j);

    /** Thread the current transaction to a specific account's root SLE.
     *
     *  Looks up the account root via `getForMod()` and calls `threadItem()`
     *  on it. Logs a warning and returns without error if the account does
     *  not exist (e.g., destination of a deleted Escrow or PayChannel).
     *
     *  @param base The underlying read view.
     *  @param meta The `TxMeta` object being built.
     *  @param to The account whose root SLE should be threaded.
     *  @param mods Scratch map of threading-only modifications.
     *  @param j Journal for warnings about missing targets.
     */
    void
    threadTx(ReadView const& base, TxMeta& meta, AccountID const& to, Mods& mods, beast::Journal j);

    /** Thread the current transaction to all owner accounts of a ledger entry.
     *
     *  Dispatches by `LedgerEntryType`:
     *  - `ltACCOUNT_ROOT`: no-op (threading to self is handled by the caller).
     *  - `ltRIPPLE_STATE`: threads to both the low-limit and high-limit account.
     *  - All others: threads to `sfAccount` if present, and to `sfDestination`
     *      if present.
     *
     *  @param base The underlying read view.
     *  @param meta The `TxMeta` object being built.
     *  @param sle The ledger entry whose owner accounts should be threaded.
     *  @param mods Scratch map of threading-only modifications.
     *  @param j Journal for warnings about missing targets.
     */
    void
    threadOwners(
        ReadView const& base,
        TxMeta& meta,
        std::shared_ptr<SLE const> const& sle,
        Mods& mods,
        beast::Journal j);
};

}  // namespace xrpl::detail
