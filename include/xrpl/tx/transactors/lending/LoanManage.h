#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the ttLOAN_MANAGE transaction type (XLS-66).
 *
 *  Transitions a loan through its three credit-quality states —
 *  **unimpaired → impaired → defaulted** — on behalf of the `LoanBroker`
 *  owner that issued the loan.  State regressions (impaired → unimpaired)
 *  are also supported; once a loan reaches `lsfLoanDefault` it is
 *  permanently frozen.
 *
 *  Exactly one of `tfLoanDefault`, `tfLoanImpair`, or `tfLoanUnimpair` may
 *  be set per transaction.  Omitting all flags is a valid (no-op) form.
 *
 *  @note The three core operations (`defaultLoan`, `impairLoan`,
 *      `unimpairLoan`) are exposed as public static helpers so other
 *      transactors can invoke the same accounting logic without constructing
 *      a synthetic transaction.
 */
class LoanManage : public Transactor
{
public:
    /** Consequence factory type — fee is claimed under normal conditions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a LoanManage transactor for the given apply context.
     *
     *  @param ctx The apply context supplied by the transaction pipeline.
     */
    explicit LoanManage(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transactor on the amendments required by the lending protocol.
     *
     *  Delegates to `checkLendingProtocolDependencies`, which requires
     *  `featureSingleAssetVault` and `featureMPTokensV1` (plus
     *  `featurePermissionedDomains` when `sfDomainID` is present).
     *  Returning `false` causes `invokePreflight` to return `temDISABLED`.
     *
     *  @param ctx The preflight context.
     *  @return `true` if all required amendments are enabled, `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the legal transaction-flag bits for this transactor.
     *
     *  The mask covers exactly `tfLoanDefault`, `tfLoanImpair`, and
     *  `tfLoanUnimpair`; any other flag bits produce `temINVALID_FLAG`.
     *
     *  @param ctx The preflight context (unused; present for pipeline conformance).
     *  @return `tfLoanManageMask`.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless validation of a ttLOAN_MANAGE transaction.
     *
     *  Checks that `sfLoanID` is non-zero and that at most one of the three
     *  action flags is set (mutual exclusivity is tested with the
     *  `(flags & (flags - 1)) != 0` idiom).  No ledger access occurs here.
     *
     *  @param ctx The preflight context carrying the transaction.
     *  @return `tesSUCCESS` on success; `temBAD_TRANSACTION` if `sfLoanID` is
     *      zero or multiple action flags are set simultaneously.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks that enforce the loan state machine.
     *
     *  In addition to verifying that the loan, broker, and vault objects
     *  exist, this method enforces:
     *  - A defaulted loan cannot be modified further (`lsfLoanDefault`).
     *  - A fully-paid loan (`sfPaymentRemaining == 0`) cannot be modified.
     *  - An already-impaired loan cannot be impaired again.
     *  - An unimpaired loan cannot be unimpaired.
     *  - Defaulting is only permitted after `sfNextPaymentDueDate` plus the
     *    grace period has elapsed on the current ledger.
     *  - Only the account that owns the `LoanBroker` may submit this transaction.
     *
     *  @param ctx The preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the state transition is legal; an appropriate
     *      `tec` or `ter` code otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Realize a loan default, absorbing losses via first-loss capital.
     *
     *  Implements XLS-66 §3.2.3.2.  The broker's cover capital absorbs the
     *  loss up to `sfCoverRateMinimum` / `sfCoverRateLiquidation` (in
     *  tenth-basis-points), further capped by `sfCoverAvailable`.  Rounding
     *  for the minimum coverage is always upward so the broker cannot
     *  under-cover.  The remaining unabsorbed loss is deducted from the
     *  vault's `sfAssetsTotal`.  Any previously recorded `sfLossUnrealized`
     *  (from a prior impairment) is cleared since the loss is now realized.
     *  All outstanding balances (`sfTotalValueOutstanding`,
     *  `sfPrincipalOutstanding`, `sfManagementFeeOutstanding`,
     *  `sfPaymentRemaining`, `sfNextPaymentDueDate`) are zeroed and
     *  `lsfLoanDefault` is set.  The covered amount is transferred from the
     *  broker pseudo-account to the vault pseudo-account via `accountSend`
     *  with `WaiveTransferFee::Yes`.
     *
     *  @param view      Mutable ledger view.
     *  @param loanSle   Mutable SLE for the Loan object.
     *  @param brokerSle Mutable SLE for the LoanBroker object.
     *  @param vaultSle  Mutable SLE for the associated Vault object.
     *  @param vaultAsset The asset denomination of the vault.
     *  @param j         Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tecINTERNAL` or `tefBAD_LEDGER` if
     *      ledger state is inconsistent (should be unreachable in a valid ledger).
     */
    static TER
    defaultLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref brokerSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Mark a loan as impaired, recording a paper loss in the vault.
     *
     *  Sets `lsfLoanImpaired` on the loan and writes
     *  `sfTotalValueOutstanding - sfManagementFeeOutstanding` into
     *  `sfLossUnrealized` on the vault, reducing the effective NAV of vault
     *  shares without moving any funds.  If `sfNextPaymentDueDate` has not
     *  yet passed, it is advanced to the current ledger close time to
     *  accelerate the payment schedule.  The operation is rejected with
     *  `tecLIMIT_EXCEEDED` if the unrealized loss would exceed the vault's
     *  unavailable assets (`sfAssetsTotal - sfAssetsAvailable`), which would
     *  leave the vault in an inconsistent state.
     *
     *  @param view      Mutable ledger view.
     *  @param loanSle   Mutable SLE for the Loan object.
     *  @param vaultSle  Mutable SLE for the associated Vault object.
     *  @param vaultAsset The asset denomination of the vault.
     *  @param j         Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tecLIMIT_EXCEEDED` if the unrealized
     *      loss would exceed available capacity; `tecINTERNAL` on ledger
     *      inconsistency.
     */
    static TER
    impairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Reverse a prior impairment, restoring the loan to normal status.
     *
     *  Clears `lsfLoanImpaired` and removes the `sfLossUnrealized` entry
     *  from the vault, reversing the paper loss recorded by `impairLoan`.
     *  The `sfNextPaymentDueDate` is restored: if the original date has not
     *  yet passed, the amortization schedule is unchanged; otherwise the due
     *  date is reset to the current ledger close time plus one payment
     *  interval, giving the borrower a full interval to make the next payment.
     *
     *  The `[[nodiscard]]` attribute enforces that callers (e.g., `LoanPay`)
     *  always propagate failures — silently ignoring a failed rollback would
     *  leave the vault's NAV accounting in an inconsistent state.
     *
     *  @param view      Mutable ledger view.
     *  @param loanSle   Mutable SLE for the Loan object.
     *  @param vaultSle  Mutable SLE for the associated Vault object.
     *  @param vaultAsset The asset denomination of the vault.
     *  @param j         Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tecINTERNAL` on ledger inconsistency.
     */
    [[nodiscard]] static TER
    unimpairLoan(
        ApplyView& view,
        SLE::ref loanSle,
        SLE::ref vaultSle,
        Asset const& vaultAsset,
        beast::Journal j);

    /** Execute the loan state transition.
     *
     *  Fetches the mutable loan, broker, and vault SLEs via `view().peek()`,
     *  then dispatches to `defaultLoan`, `impairLoan`, or `unimpairLoan`
     *  based on the transaction flags.  No flags is a valid no-op form.
     *  After the dispatch, calls `associateAsset` on all three SLEs when
     *  the `fixSecurity3_1_3` amendment is active, ensuring stored numeric
     *  values are rounded to the asset's precision.
     *
     *  @return `tesSUCCESS` on success; a `tec` code from the dispatched
     *      helper on failure; `tefBAD_LEDGER` if required SLEs are missing.
     */
    TER
    doApply() override;

    /** Accumulate per-entry state for the `ValidLoan` invariant checker.
     *
     *  Called once per modified SLE during the invariant-check phase.
     *  Records before/after snapshots of Loan, Vault, and LoanBroker entries
     *  so that `finalizeInvariants` can verify conservation of value across
     *  the transaction.
     *
     *  @param isDelete `true` if the entry was erased.
     *  @param before   SLE state before the transaction (may be null for
     *      newly created entries).
     *  @param after    SLE state after the transaction (may be null for
     *      deleted entries).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Verify that the `ValidLoan` invariant holds after the transaction.
     *
     *  Checks that outstanding balances, unrealized-loss accounting, and
     *  vault asset totals are internally consistent after the state
     *  transition.  Returns `false` (triggering `tecINVARIANT_FAILED`) if
     *  any conservation property is violated.
     *
     *  @param tx     The applied transaction.
     *  @param result The TER returned by `doApply`.
     *  @param fee    The fee deducted from the submitting account.
     *  @param view   Read-only view of the post-transaction ledger.
     *  @param j      Journal for invariant-failure diagnostics.
     *  @return `true` if all invariants pass, `false` otherwise.
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
