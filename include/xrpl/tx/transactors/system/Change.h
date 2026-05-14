#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Applies consensus-driven, system-level protocol mutations to a closed ledger.
 *
 *  `Change` handles three distinct pseudo-transaction types — `ttAMENDMENT`,
 *  `ttFEE`, and `ttUNL_MODIFY` — under one class, dispatching in `doApply()`
 *  to `applyAmendment()`, `applyFee()`, or `applyUNLModify()` based on the
 *  transaction type.
 *
 *  These transactions are synthesized internally during ledger close and are
 *  never submitted by real accounts. Accordingly, `Transactor::invokePreflight<Change>`
 *  is fully specialized (in `Change.cpp`) to bypass the normal amendment-gating,
 *  flag-mask, and signature-validation sequence, performing only `preflight0`
 *  plus zero-account/zero-fee/zero-sequence checks.
 *
 *  @note `calculateBaseFee()` unconditionally returns zero — pseudo-transactions
 *      are not subject to fee markets or reserve checks.
 *  @see EnableAmendment, SetFee, UNLModify
 */
class Change : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit Change(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Dispatch to `applyAmendment()`, `applyFee()`, or `applyUNLModify()`.
     *
     *  The `default` branch is `UNREACHABLE`; `preclaim()` rejects unknown
     *  transaction types before `doApply()` is ever called.
     *
     *  @return `tesSUCCESS` on success, a `tef*` code on protocol-level
     *      constraint violations, or `temINVALID_FLAG` for conflicting amendment
     *      flags.
     */
    TER
    doApply() override;

    /** Assert that the synthesized account field is the zero account. */
    void
    preCompute() override;

    /** No-op; `Change` has no transaction-specific invariant entries yet. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op; `Change` has no transaction-specific invariant finalization yet.
     *
     *  @return Always `true`.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Return zero; pseudo-transactions carry no fee.
     *
     *  @return `XRPAmount{0}` unconditionally.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx)
    {
        return XRPAmount{0};
    }

    /** Validate that this transaction is being applied to a closed ledger and
     *  enforce format compatibility for `ttFEE` transactions.
     *
     *  Returns `temINVALID` when the view is open, because the open/closed
     *  distinction is not available during preflight. For `ttFEE`, the set of
     *  required and forbidden fields differs depending on whether `featureXRPFees`
     *  is active: the legacy format uses fee-unit integers (`sfBaseFee`,
     *  `sfReferenceFeeUnits`, `sfReserveBase`, `sfReserveIncrement`), while the
     *  new format uses drop amounts (`sfBaseFeeDrops`, `sfReserveBaseDrops`,
     *  `sfReserveIncrementDrops`). Mixing field sets returns `temMALFORMED` or
     *  `temDISABLED`. `ttAMENDMENT` and `ttUNL_MODIFY` require no extra checks
     *  here; unknown types return `temUNKNOWN`.
     *
     *  @param ctx Read-only context including the ledger view and transaction.
     *  @return `tesSUCCESS`, `temINVALID`, `temMALFORMED`, `temDISABLED`, or
     *      `temUNKNOWN`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

private:
    /** Record a `tfGotMajority` / `tfLostMajority` event, or activate an
     *  amendment on the ledger.
     *
     *  Writes to the `keylet::amendments()` SLE. On activation, calls
     *  `AmendmentTable::enable()` and, if the amendment is unsupported by this
     *  node, invokes `NetworkOPs::setAmendmentBlocked()` to halt consensus
     *  participation.
     *
     *  @return `tesSUCCESS`, `tefALREADY` (duplicate majority record or removal
     *      of a non-existent majority), or `temINVALID_FLAG` (both majority flags
     *      set simultaneously).
     */
    TER
    applyAmendment();

    /** Write the new fee schedule into the `keylet::fees()` SLE.
     *
     *  Under `featureXRPFees`, sets the drop-denominated fields and explicitly
     *  removes the legacy fee-unit fields via `makeFieldAbsent` to prevent stale
     *  data from persisting. Without the feature, the legacy fields are written
     *  and the drop fields remain absent.
     *
     *  @return `tesSUCCESS` always.
     */
    TER
    applyFee();

    /** Nominate a validator for disabling or re-enabling in the Negative UNL.
     *
     *  Only valid on flag ledgers (`isFlagLedger(view().seq())`); returns
     *  `tefFAILURE` on any other ledger sequence. Enforces these consistency
     *  invariants on `keylet::negativeUNL()`:
     *  - At most one pending `sfValidatorToDisable` and one `sfValidatorToReEnable`.
     *  - A validator cannot be nominated for disabling if it is already in the
     *    Negative UNL.
     *  - A validator cannot be nominated for re-enabling if it is not in the
     *    Negative UNL.
     *  - The same validator cannot simultaneously occupy both pending slots.
     *
     *  @return `tesSUCCESS` on success, `tefFAILURE` for any format error or
     *      invariant violation.
     */
    TER
    applyUNLModify();
};

/** Semantic alias for `Change` when dispatching amendment activation pseudo-transactions. */
using EnableAmendment = Change;

/** Semantic alias for `Change` when dispatching fee-schedule update pseudo-transactions. */
using SetFee = Change;

/** Semantic alias for `Change` when dispatching Negative UNL modification pseudo-transactions. */
using UNLModify = Change;

}  // namespace xrpl
