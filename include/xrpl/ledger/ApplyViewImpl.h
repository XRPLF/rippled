#pragma once

#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/** Per-transaction scratch-pad view that buffers ledger mutations and
 *  constructs `TxMeta` on commit.
 *
 *  `ApplyViewImpl` is the concrete view handed to every `Transactor`
 *  during the apply phase. It sits at the top of the view hierarchy:
 *  `ReadView` → `ApplyView` → `detail::ApplyViewBase` → `ApplyViewImpl`.
 *  All ledger mutations are buffered in the inherited `items_`
 *  (`ApplyStateTable`) and are not visible to the parent `OpenView`
 *  until `apply()` is called. If the transaction fails, the view is
 *  discarded and `base_` is left unchanged.
 *
 *  The object is move-constructible but neither copyable nor
 *  move-assignable, ensuring that at most one instance can commit a
 *  given transaction's buffered state.
 *
 *  @note `base_` is held as a raw `const*` (not a shared pointer) for
 *      performance. The caller must ensure the underlying view outlives
 *      this object.
 */
class ApplyViewImpl final : public detail::ApplyViewBase
{
public:
    ApplyViewImpl() = delete;
    ApplyViewImpl(ApplyViewImpl const&) = delete;
    ApplyViewImpl&
    operator=(ApplyViewImpl&&) = delete;
    ApplyViewImpl&
    operator=(ApplyViewImpl const&) = delete;

    ApplyViewImpl(ApplyViewImpl&&) = default;

    /** Construct a transaction apply view over an existing read view.
     *
     *  @param base  The underlying ledger state to read from. Must remain
     *      valid for the lifetime of this object.
     *  @param flags Apply-phase control flags (e.g., `tapRETRY`,
     *      `tapDRY_RUN`, `tapBATCH`) that influence commit behavior and
     *      the metadata produced by `apply()`.
     */
    ApplyViewImpl(ReadView const* base, ApplyFlags flags);

    /** Flush buffered mutations to `to` and produce transaction metadata.
     *
     *  Delegates to `ApplyStateTable::apply()`, which drains every
     *  pending insert, modify, and erase into `to` and builds the
     *  `TxMeta` record — including `sfCreatedNode`, `sfModifiedNode`,
     *  and `sfDeletedNode` entries with `sfPreviousFields`/`sfFinalFields`
     *  — for the closed ledger. If `isDryRun` is `true`, metadata is
     *  computed and returned but state changes are suppressed, supporting
     *  fee-simulation paths without side effects.
     *
     *  When `parentBatchId` is set (i.e., `tapBATCH` is active), the
     *  generated metadata records the parent batch transaction ID so
     *  individual results can be traced back to their enclosing batch.
     *
     *  @param to            The target open view that accumulates all
     *      committed transaction changes for the current ledger round.
     *  @param tx            The transaction being applied.
     *  @param ter           The final result code; recorded in metadata.
     *  @param parentBatchId The ID of the enclosing `ttBATCH` transaction,
     *      or `std::nullopt` for standalone transactions.
     *  @param isDryRun      If `true`, produce metadata without mutating `to`.
     *  @param j             Journal for diagnostic logging.
     *  @return The `TxMeta` for closed-ledger commits and dry-run
     *      evaluation; `std::nullopt` when `to` is still open and
     *      `isDryRun` is `false`.
     *
     *  @note After this call returns, the only valid operation on this
     *      object is destruction. The internal `ApplyStateTable` is
     *      drained and must not be accessed again.
     */
    std::optional<TxMeta>
    apply(
        OpenView& to,
        STTx const& tx,
        TER ter,
        std::optional<uint256> parentBatchId,
        bool isDryRun,
        beast::Journal j);

    /** Record the amount delivered by a payment transaction.
     *
     *  Stores `amount` so that `ApplyStateTable::apply()` can write the
     *  `sfDeliveredAmount` field into the resulting `TxMeta`. The
     *  delivered amount can differ from the send amount in cross-currency
     *  or partial-payment scenarios. If never called, `sfDeliveredAmount`
     *  is omitted from the metadata.
     *
     *  Must be called before `apply()` to take effect.
     *
     *  @param amount The currency amount actually received by the destination.
     */
    void
    deliver(STAmount const& amount)
    {
        deliver_ = amount;
    }

    /** Return the number of pending write-intent entries.
     *
     *  Counts only entries with an `Erase`, `Insert`, or `Modify` action;
     *  cache-only reads are excluded. Used by `ApplyContext::size()` to
     *  support batch-processing decisions before committing.
     *
     *  @return Count of SLE mutations buffered since construction or the
     *      last `discard()`.
     */
    std::size_t
    size();

    /** Iterate every pending write-intent entry, invoking a callback per entry.
     *
     *  Delegates to `ApplyStateTable::visit()`. `Cache`-only reads are
     *  skipped. Used by invariant checkers and batch-processing logic to
     *  inspect accumulated changes before deciding whether to commit them.
     *
     *  @param target The open view used to fetch pre-change SLE snapshots
     *      for `Erase` and `Modify` entries.
     *  @param func   Callback invoked once per pending entry with:
     *      - `key` — ledger index of the entry.
     *      - `isDelete` — `true` if the entry is being erased.
     *      - `before` — the SLE state before this transaction (`nullptr`
     *          for insertions).
     *      - `after` — the pending SLE state (`nullptr` for deletions).
     */
    void
    visit(
        OpenView& target,
        std::function<void(
            uint256 const& key,
            bool isDelete,
            std::shared_ptr<SLE const> const& before,
            std::shared_ptr<SLE const> const& after)> const& func);

private:
    std::optional<STAmount> deliver_;
};

}  // namespace xrpl
