/** @file
 *  Declares `ApplyViewBase`, the abstract concrete base class shared by all
 *  buffered mutable ledger views used during transaction application.
 *
 *  `ApplyViewBase` lives in `xrpl::detail` to signal that it is internal
 *  infrastructure; transaction processing code works with `ApplyView` or
 *  `ApplyViewImpl` references.  The three concrete subclasses —
 *  `ApplyViewImpl`, `Sandbox`, and `PaymentSandbox` — are the only types
 *  that need to reach into this layer directly.
 */

#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyStateTable.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl::detail {

/** Concrete base for buffered mutable ledger views.
 *
 *  Implements the full `ApplyView` and `RawView` interfaces on top of two
 *  members: a read-only pointer to the base ledger snapshot (`base_`) and
 *  an `ApplyStateTable` change buffer (`items_`).  Queries that need
 *  awareness of pending mutations (e.g. `exists`, `read`, `peek`) merge
 *  `items_` with `base_`; purely structural queries (`header`, `fees`,
 *  `rules`) and SLE iterators bypass `items_` and forward directly to
 *  `base_`.
 *
 *  Not copyable; move-constructible only.  Subclasses (`ApplyViewImpl`,
 *  `Sandbox`) supply lifecycle logic such as `apply()`.
 *
 *  @note The `erase()` and `update()` methods enforce pointer identity: the
 *      caller must pass the exact `shared_ptr` returned by `peek()` on
 *      **this** view instance.  Passing an SLE obtained from a different view
 *      results in a `LogicError`.
 */
class ApplyViewBase : public ApplyView, public RawView
{
public:
    ApplyViewBase() = delete;
    ApplyViewBase(ApplyViewBase const&) = delete;
    ApplyViewBase&
    operator=(ApplyViewBase&&) = delete;
    ApplyViewBase&
    operator=(ApplyViewBase const&) = delete;

    ApplyViewBase(ApplyViewBase&&) = default;

    /** Construct over an existing read-only ledger snapshot.
     *
     *  @param base Non-owning pointer to the base ledger state; must
     *      outlive this view.  All reads that bypass the change buffer
     *      are forwarded here.
     *  @param flags Per-transaction policy flags (retry mode, dry-run,
     *      unlimited, etc.) that are carried through the apply pass and
     *      exposed via `flags()`.
     */
    ApplyViewBase(ReadView const* base, ApplyFlags flags);

    // ReadView

    /** @return `true` if the underlying view represents an open ledger. */
    [[nodiscard]] bool
    open() const override;

    /** @return The ledger header from the base snapshot. */
    [[nodiscard]] LedgerHeader const&
    header() const override;

    /** @return The fee schedule from the base snapshot. */
    [[nodiscard]] Fees const&
    fees() const override;

    /** @return The amendment rules from the base snapshot. */
    [[nodiscard]] Rules const&
    rules() const override;

    /** Test whether a ledger object exists, accounting for pending changes.
     *
     *  Returns `false` for objects pending erasure, `true` for objects
     *  buffered as inserted or modified, and delegates to `base_` for
     *  keys not yet in the change buffer.
     *
     *  @param k Keylet identifying the object.
     *  @return `true` if the object will exist after the pending changes.
     */
    [[nodiscard]] bool
    exists(Keylet const& k) const override;

    /** Find the next live key after `key`, accounting for pending changes.
     *
     *  Merges the base key space (skipping keys pending deletion) with the
     *  local change buffer (skipping erased entries) and returns the smaller
     *  candidate key that is less than `last`.
     *
     *  @param key Exclusive lower bound.
     *  @param last Optional exclusive upper bound.
     *  @return The next live key, or `std::nullopt` if none exists in range.
     */
    [[nodiscard]] std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const override;

    /** Read a ledger object as an immutable snapshot, accounting for pending
     *  changes.
     *
     *  Returns `nullptr` for objects pending erasure or when the keylet check
     *  fails; returns the buffered SLE for inserted/modified entries; falls
     *  back to `base_` for unknown keys.
     *
     *  @param k Keylet identifying the object.
     *  @return A `const`-qualified handle to the SLE, or `nullptr`.
     */
    [[nodiscard]] std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    /** @name SLE iterators (base snapshot only)
     *
     *  These iterators forward directly to `base_` and do **not** reflect
     *  pending insertions or deletions in the change buffer.  This is
     *  intentional: the apply phase never needs to iterate its own buffered
     *  writes, and bypassing the buffer keeps SLE traversal consistent with
     *  the base ledger snapshot.
     */
    /** @{ */
    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesBegin() const override;

    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesEnd() const override;

    /** Return an iterator to the first SLE whose key is not less than `key`,
     *  drawn from the base snapshot only.
     *
     *  @param key The lower-bound key for the search.
     *  @return An iterator into the base SLE map at or after `key`.
     */
    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override;
    /** @} */

    /** @name Transaction-map accessors (forwarded to base snapshot) */
    /** @{ */
    [[nodiscard]] std::unique_ptr<TxsType::iter_base>
    txsBegin() const override;

    [[nodiscard]] std::unique_ptr<TxsType::iter_base>
    txsEnd() const override;

    /** Test whether a transaction exists in the base snapshot's tx-map.
     *
     *  @param key The transaction ID to look up.
     *  @return `true` if the transaction is present in the base ledger's
     *      transaction map.
     */
    [[nodiscard]] bool
    txExists(key_type const& key) const override;

    /** Read a transaction and its metadata from the base snapshot's tx-map.
     *
     *  @param key The transaction ID to retrieve.
     *  @return A pair of `(STTx, STObject metadata)` for the transaction,
     *      or `{nullptr, nullptr}` if not found.
     */
    [[nodiscard]] tx_type
    txRead(key_type const& key) const override;
    /** @} */

    // ApplyView

    /** Return the flags governing this transaction apply pass.
     *
     *  @return The `ApplyFlags` bitmask set at construction.
     */
    [[nodiscard]] ApplyFlags
    flags() const override;

    /** Check out a ledger entry for in-place mutation.
     *
     *  Loads the entry into the change buffer on first access (tagged
     *  `Cache`).  Returns the same `shared_ptr` on subsequent calls.
     *  The returned pointer must later be passed to `update()` or `erase()`
     *  on **this** view instance to record the intended change.
     *
     *  @param k Keylet identifying the entry.
     *  @return A mutable handle to the buffered SLE, or `nullptr` if the
     *      entry does not exist (including if it is pending erasure).
     */
    std::shared_ptr<SLE>
    peek(Keylet const& k) override;

    /** Stage a deletion for a checked-out entry.
     *
     *  Transitions the buffer entry from `Cache` or `Modify` to `Erase`.
     *  If the entry was inserted within this same transaction, it is removed
     *  entirely (net-zero effect on the base).
     *
     *  @param sle The exact `shared_ptr` previously returned by `peek()`
     *      on this view instance.
     *  @throws std::logic_error If the pointer does not match the buffered
     *      entry or the entry is already erased.
     */
    void
    erase(std::shared_ptr<SLE> const& sle) override;

    /** Stage a new ledger entry for insertion.
     *
     *  Records the SLE under `Action::Insert`.  If the key was previously
     *  erased within this transaction the action is collapsed to `Modify`.
     *
     *  @param sle The new entry; its key must not already exist in the view.
     *  @throws std::logic_error If the key already exists with a non-erase
     *      action in the buffer.
     */
    void
    insert(std::shared_ptr<SLE> const& sle) override;

    /** Promote a checked-out entry to a definitive write.
     *
     *  Transitions the buffer action from `Cache` to `Modify`; `Insert` and
     *  `Modify` entries are left unchanged (already write-intent).
     *
     *  @param sle The exact `shared_ptr` previously returned by `peek()`
     *      on this view instance.
     *  @throws std::logic_error If the pointer does not match, the entry is
     *      erased, or the key is unknown.
     */
    void
    update(std::shared_ptr<SLE> const& sle) override;

    // RawView

    /** Erase a ledger entry without enforcing pointer-identity ownership.
     *
     *  Bypasses the `peek()`-pointer ownership check enforced by `erase()`.
     *  Used by `RawView` callers (e.g. `Sandbox::apply`) that flush changes
     *  from another view's table and cannot satisfy the same-instance
     *  invariant.
     *
     *  @param sle An SLE whose key identifies the object to erase.
     *  @throws std::logic_error If the object is already pending erasure.
     */
    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    /** Insert a ledger entry via the same validated path as `insert()`.
     *
     *  Despite being a raw-tier operation, this method calls the same
     *  `items_.insert()` that the high-level `insert()` uses; the
     *  distinction is that callers from the `RawView` flush path are not
     *  required to have obtained the SLE from `peek()`.
     *
     *  @param sle The new entry to stage for insertion.
     *  @throws std::logic_error If the key already exists with a non-erase
     *      action.
     */
    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    /** Unconditionally overwrite the buffered SLE for an existing key.
     *
     *  Records the SLE under `Action::Modify`, replacing any `Cache` or
     *  `Insert` entry.  Unlike `update()`, does not enforce pointer identity.
     *
     *  @param sle The SLE to store; its key must exist in this view.
     *  @throws std::logic_error If the key is currently pending erasure.
     */
    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    /** Record XRP drops destroyed by fees within this transaction's scope.
     *
     *  Accumulates into the change buffer and is forwarded to the parent
     *  view's `rawDestroyXRP()` when the buffer is committed.
     *
     *  @param feeDrops The amount of XRP to permanently remove from
     *      circulation.
     */
    void
    rawDestroyXRP(XRPAmount const& feeDrops) override;

protected:
    /** Per-transaction policy flags set at construction; exposed via `flags()`. */
    ApplyFlags flags_;

    /** Non-owning pointer to the base ledger snapshot.
     *
     *  All reads that do not need awareness of pending changes are forwarded
     *  here.  The pointed-to view must outlive this object.
     */
    ReadView const* base_;

    /** Change buffer accumulating per-transaction ledger mutations.
     *
     *  Maps each touched `uint256` key to an `(Action, SLE)` pair.  Flushed
     *  to the parent view atomically on `apply()`; discarded on destruction.
     */
    detail::ApplyStateTable items_;
};

}  // namespace xrpl::detail
