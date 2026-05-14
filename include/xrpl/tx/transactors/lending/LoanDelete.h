#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Closes out a fully-repaid loan object from the XRP Ledger.
 *
 *  Removes a `Loan` SLE after the borrower has satisfied all payment
 *  obligations (`sfPaymentRemaining == 0`). Releasing the loan releases the
 *  owner-count reserves held against both the borrower's account and the loan
 *  broker's pseudo-account. Either the broker owner or the borrower may
 *  submit this transaction.
 *
 *  @note This transactor is part of the on-chain lending protocol (XLS-66).
 *      Deletion of an active loan is rejected with `tecHAS_OBLIGATIONS`.
 *  @see LoanSet, LoanPay, LoanManage
 */
class LoanDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gates execution on the lending protocol amendments being active.
     *
     *  Delegates to `checkLendingProtocolDependencies()` so the framework
     *  rejects the transaction before any field-level validation when the
     *  required amendments are not enabled.
     *
     *  @param ctx Preflight context carrying current ledger rules.
     *  @return `true` if all required lending-protocol amendments are active;
     *      `false` otherwise (transaction will be rejected).
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Validates that `sfLoanID` is not the zero hash.
     *
     *  All other checks — existence, ownership, and repayment state — require
     *  ledger access and are deferred to `preclaim`.
     *
     *  @param ctx Preflight context carrying the transaction fields.
     *  @return `temINVALID` if `sfLoanID` is the zero hash; `tesSUCCESS`
     *      otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Verifies that the loan exists, is fully repaid, and the caller is
     *  authorized to delete it.
     *
     *  Two invariants are enforced before allowing the transaction to proceed:
     *  - The loan referenced by `sfLoanID` must exist (`tecNO_ENTRY` if not).
     *  - `sfPaymentRemaining` must be zero; a non-zero value returns
     *    `tecHAS_OBLIGATIONS`.
     *  - The submitting account must be either the broker owner (resolved via
     *    the `LoanBroker` SLE) or the direct borrower (`sfBorrower`); any
     *    other account returns `tecNO_PERMISSION`.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; `tecNO_ENTRY`,
     *      `tecHAS_OBLIGATIONS`, `tecNO_PERMISSION`, or `tecINTERNAL` on
     *      failure.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Removes the loan object and releases associated reserves.
     *
     *  Performs the following steps in order:
     *  1. Evicts the loan key from the broker pseudo-account's owner directory
     *     (using `sfLoanBrokerNode` as the hint) and from the borrower's owner
     *     directory (using `sfOwnerNode`).
     *  2. Erases the `Loan` SLE.
     *  3. Decrements the broker's `sfOwnerCount` (which tracks outstanding
     *     loans, distinct from the pseudo-account's own count).
     *  4. If the broker's `sfOwnerCount` reaches zero, any residual non-zero
     *     `sfDebtTotal` is forcibly zeroed. An `XRPL_ASSERT_PARTS` verifies
     *     the remainder rounds to zero at the vault's asset scale — this
     *     prevents sub-precision rounding dust from permanently stranding the
     *     broker.
     *  5. Decrements the borrower's `sfOwnerCount`, releasing the XRP reserve
     *     locked at loan creation.
     *  6. Calls `associateAsset` on the loan, broker, and vault SLEs as a
     *     defensive consistency measure.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if any SLE that
     *      `preclaim` guaranteed to exist is unexpectedly absent (marked
     *      `LCOV_EXCL_LINE` — indicates ledger corruption).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor — reserved for future use.
     *
     *  No transaction-specific invariants are currently checked. The override
     *  is a placeholder for future additions.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before The SLE state before the transaction, or `nullptr`.
     *  @param after The SLE state after the transaction, or `nullptr`.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalizes per-transaction invariants — reserved for future use.
     *
     *  No transaction-specific invariants are currently checked. Always
     *  returns `true`.
     *
     *  @param tx The transaction being applied.
     *  @param result The TER code returned by `doApply`.
     *  @param fee The fee deducted for this transaction.
     *  @param view Read-only view of the ledger after application.
     *  @param j Journal for diagnostic logging.
     *  @return `true` unconditionally.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

}  // namespace xrpl
