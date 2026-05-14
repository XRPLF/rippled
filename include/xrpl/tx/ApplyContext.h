#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>

#include <optional>

namespace xrpl {

/** Central context object for the transaction-application pipeline.
 *
 *  `ApplyContext` is created at the boundary between the *preclaim* phase
 *  (read-only authorization and fee validation) and the *apply* phase (actual
 *  ledger state mutation). It lives for the duration of apply and is passed
 *  by reference to every `Transactor` implementation, giving each handler a
 *  uniform handle to the sandboxed mutable view, the validated transaction,
 *  and the fee information.
 *
 *  The sandboxed view is stored as `std::optional<ApplyViewImpl>` layered on
 *  top of `base_`. Rollback is implemented by discarding and re-emplacing the
 *  optional rather than walking an undo log — see `discard()`. Mutations are
 *  not committed to `base_` until `apply()` is called explicitly.
 *
 *  @note The const-qualified public members (`tx`, `preclaimResult`,
 *      `baseFee`, `journal`) are immutable for the lifetime of an apply
 *      cycle. All mutable state is confined to the private `view_`,
 *      `flags_`, and `parentBatchId_` members.
 */
class ApplyContext
{
public:
    /** Construct for a batch-inner transaction.
     *
     *  Use this constructor when the transaction executes inside a `ttBATCH`
     *  envelope. The `parentBatchId` is forwarded to `ApplyViewImpl::apply()`
     *  so the generated `TxMeta` records the parent-child relationship.
     *  Asserts that `parentBatchId` is set if and only if `TapBatch` is
     *  active in `flags`.
     *
     *  @param registry  Service registry providing shared engine services.
     *  @param base      The underlying open ledger view that accumulates
     *      committed state. Never modified until `apply()` is called.
     *  @param parentBatchId  The `uint256` ID of the enclosing batch
     *      transaction. Must be non-null when `TapBatch` is set in `flags`.
     *  @param tx            The fully-validated transaction to apply.
     *  @param preclaimResult The `TER` code produced by the preclaim phase.
     *  @param baseFee       The fee determined before applying, in drops.
     *  @param flags         Apply-phase control flags (e.g., `TapDryRun`).
     *  @param journal       Logging sink; defaults to the null sink.
     */
    explicit ApplyContext(
        ServiceRegistry& registry,
        OpenView& base,
        std::optional<uint256 const> const& parentBatchId,
        STTx const& tx,
        TER preclaimResult,
        XRPAmount baseFee,
        ApplyFlags flags,
        beast::Journal journal = beast::Journal{beast::Journal::getNullSink()});

    /** Construct for a standalone (non-batch) transaction.
     *
     *  Delegates to the full constructor with `std::nullopt` for
     *  `parentBatchId`. Asserts that `TapBatch` is not set in `flags` —
     *  batch-inner transactions must use the constructor that accepts a
     *  `parentBatchId`.
     *
     *  @param registry  Service registry providing shared engine services.
     *  @param base      The underlying open ledger view.
     *  @param tx            The fully-validated transaction to apply.
     *  @param preclaimResult The `TER` code produced by the preclaim phase.
     *  @param baseFee       The fee determined before applying, in drops.
     *  @param flags         Apply-phase control flags. Must not include `TapBatch`.
     *  @param journal       Logging sink; defaults to the null sink.
     */
    explicit ApplyContext(
        ServiceRegistry& registry,
        OpenView& base,
        STTx const& tx,
        TER preclaimResult,
        XRPAmount baseFee,
        ApplyFlags flags,
        beast::Journal journal = beast::Journal{beast::Journal::getNullSink()})
        : ApplyContext(registry, base, std::nullopt, tx, preclaimResult, baseFee, flags, journal)
    {
        XRPL_ASSERT((flags & TapBatch) == 0, "Batch apply flag should not be set");
    }

    /** Service registry providing shared engine services. */
    std::reference_wrapper<ServiceRegistry> registry;

    /** The transaction being applied. Immutable for the apply lifecycle. */
    STTx const& tx;

    /** The `TER` result produced by the preclaim phase. Immutable. */
    TER const preclaimResult;

    /** The fee charged for this transaction, in drops. Immutable. */
    XRPAmount const baseFee;

    /** Logging sink. Immutable. */
    beast::Journal const journal;

    /** Access the sandboxed mutable ledger view.
     *
     *  Returns the `ApplyViewImpl` layered on top of `base_`. All
     *  transactor mutations go through this view; none reach `base_`
     *  until `apply()` is called.
     *
     *  @return A mutable reference to the sandboxed apply view.
     */
    ApplyView&
    view()
    {
        return *view_;  // NOLINT(bugprone-unchecked-optional-access) view_ emplaced in constructor
    }

    /** Access the sandboxed ledger view (read-only overload).
     *
     *  @return A const reference to the sandboxed apply view.
     */
    [[nodiscard]] ApplyView const&
    view() const
    {
        return *view_;  // NOLINT(bugprone-unchecked-optional-access) view_ emplaced in constructor
    }

    /** Access the sandboxed view as a low-level `RawView`.
     *
     *  Bypasses the higher-level constraint enforcement in `ApplyView` to
     *  allow direct ledger-entry writes. Use only where `ApplyView`'s guards
     *  are legitimately too restrictive for the operation at hand.
     *
     *  @return A mutable reference to the underlying `RawView` interface of
     *      the sandboxed view.
     *
     *  @note Prefer `view()` wherever possible. This accessor exists as an
     *      escape hatch for engine internals that must write ledger entries
     *      without the higher-level guards.
     */
    // VFALCO Unfortunately this is necessary
    RawView&
    rawView()
    {
        return *view_;  // NOLINT(bugprone-unchecked-optional-access) view_ emplaced in constructor
    }

    /** Return the apply-phase control flags for this transaction. */
    [[nodiscard]] ApplyFlags const&
    flags() const
    {
        return flags_;
    }

    /** Record the delivered amount in the transaction metadata.
     *
     *  Sets the `sfDeliveredAmount` field written into `TxMeta` when
     *  `apply()` is called. Must be called before `apply()` to take effect.
     *
     *  @param amount The amount actually delivered by this transaction.
     */
    void
    deliver(STAmount const& amount)
    {
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) view_ emplaced in constructor
        view_->deliver(amount);
    }

    /** Discard all sandboxed mutations and reset to a clean view.
     *
     *  Destroys the current `ApplyViewImpl` in-place and constructs a fresh
     *  one on top of `base_`. The base view is never touched, so all
     *  accumulated ledger changes evaporate without an undo log. Called by
     *  `Transactor::reset()` to implement `tec*` rollback and by the
     *  `tapFAIL_HARD` path to suppress even fee deduction.
     */
    void
    discard();

    /** Commit sandboxed mutations to the base view and produce metadata.
     *
     *  Delegates to `ApplyViewImpl::apply()`, which writes all accumulated
     *  state changes into `base_` and generates `TxMeta`. After this call
     *  the `ApplyViewImpl` is consumed and must not be used again.
     *
     *  @param ter The final `TER` result code for the transaction.
     *  @return The generated `TxMeta` if the transaction is committed to the
     *      ledger, or `std::nullopt` when `TapDryRun` is active.
     */
    std::optional<TxMeta> apply(TER);

    /** Return the number of pending (uncommitted) ledger-entry changes.
     *
     *  @return Count of SLE modifications accumulated in the sandboxed view
     *      since the last `discard()` or construction.
     */
    std::size_t
    size();

    /** Iterate over pending ledger-entry changes in the sandboxed view.
     *
     *  Calls `func` once for each modified, inserted, or deleted SLE
     *  tracked by the sandbox. Used by invariant checkers (via
     *  `checkInvariants`) and by `tecOVERSIZE` / `tecKILLED` cleanup
     *  handlers to identify objects that need post-failure removal.
     *
     *  @param func Callback invoked per entry. Parameters:
     *      - `key` — ledger index of the entry.
     *      - `isDelete` — true if the entry is being erased.
     *      - `before` — the SLE state before this transaction (`nullptr`
     *          for insertions).
     *      - `after` — the SLE state after this transaction (`nullptr`
     *          for deletions).
     */
    void
    visit(
        std::function<void(
            uint256 const& key,
            bool isDelete,
            std::shared_ptr<SLE const> const& before,
            std::shared_ptr<SLE const> const& after)> const& func);

    /** Burn the given XRP fee from the ledger supply.
     *
     *  Forwards to `RawView::rawDestroyXRP()` on the sandboxed view.
     *  Called by `Transactor::payFee()` as part of fee deduction.
     *
     *  @param fee Amount of XRP, in drops, to remove from the total supply.
     */
    void
    destroyXRP(XRPAmount const& fee)
    {
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) view_ emplaced in constructor
        view_->rawDestroyXRP(fee);
    }

    /** Run all registered invariant checkers and return the final result.
     *
     *  Iterates the compile-time `InvariantChecks` tuple. For each checker,
     *  calls `visitEntry()` on every pending SLE change (via `visit()`),
     *  then calls `finalize()`. Results are collected into an array before
     *  being tested with `std::all_of` — never short-circuited — so that
     *  every failing invariant emits its own fatal log message.
     *
     *  If any checker fails or throws, delegates to `failInvariantCheck()`
     *  to determine whether to return `tecINVARIANT_FAILED` (first failure,
     *  fee still charged) or `tefINVARIANT_FAILED` (repeat failure, tx not
     *  included in ledger at all).
     *
     *  @param result The `TER` produced by `doApply()`; must be
     *      `tesSUCCESS` or a `tec*` claim code.
     *  @param fee    The fee charged for this transaction, in drops.
     *  @return The original `result` if all invariants pass; otherwise
     *      `tecINVARIANT_FAILED` or `tefINVARIANT_FAILED`.
     */
    TER
    checkInvariants(TER const result, XRPAmount const fee);

private:
    /** Determine the escalated failure code for a broken invariant.
     *
     *  Returns `tefINVARIANT_FAILED` if `result` is already
     *  `tecINVARIANT_FAILED` or `tefINVARIANT_FAILED` (the fee-only retry
     *  path also broke invariants — nothing safe to commit). Returns
     *  `tecINVARIANT_FAILED` on the first failure so the transaction is
     *  still included in the ledger with a fee charge.
     *
     *  @param result The current TER before escalation.
     *  @return The escalated invariant-failure TER.
     */
    static TER
    failInvariantCheck(TER const result);

    template <std::size_t... Is>
    TER
    checkInvariantsHelper(TER const result, XRPAmount const fee, std::index_sequence<Is...>);

    OpenView& base_;
    ApplyFlags flags_;
    std::optional<ApplyViewImpl> view_;

    // The ID of the batch transaction we are executing under, if seated.
    std::optional<uint256 const> parentBatchId_;
};

}  // namespace xrpl
