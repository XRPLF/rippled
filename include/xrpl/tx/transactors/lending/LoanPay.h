#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttLOAN_PAY` transaction type (XLS-66).
 *
 *  Allows a borrower to repay an outstanding loan created by the XRPL
 *  Lending Protocol.  Each invocation may process one or more amortization
 *  installments in a single transaction, routing funds simultaneously to
 *  the vault pseudo-account (principal + interest) and the broker payee
 *  (service fee).
 *
 *  Four mutually exclusive payment modes are supported via transaction flags:
 *  - No flag: regular on-time periodic payment.
 *  - `tfLoanLatePayment`: payment after the due date; includes late interest.
 *  - `tfLoanFullPayment`: early full payoff; may apply a discount or penalty.
 *  - `tfLoanOverpayment`: pay more than one installment; triggers
 *    re-amortization of the remaining balance.
 *
 *  The fee scales with the estimated number of installments the submitted
 *  `sfAmount` would cover, charging one base-fee unit per
 *  `kLOAN_PAYMENTS_PER_FEE_INCREMENT` payments so that multi-installment
 *  submissions do not impose disproportionate cost on the network.
 *
 *  @note `ConsequencesFactory{Normal}` means a failed `LoanPay` does not
 *      block later transactions from the same account in the queue.
 */
class LoanPay : public Transactor
{
public:
    /** Consequence factory type — fee is claimed under normal conditions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Construct a `LoanPay` transactor for the given apply context.
     *
     *  @param ctx The apply context supplied by the transaction pipeline.
     */
    explicit LoanPay(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transactor on the amendments required by the lending protocol.
     *
     *  Delegates to `checkLendingProtocolDependencies`, which verifies that
     *  all feature flags the lending protocol depends on (including
     *  `featureSingleAssetVault`, `featureMPTokensV1`, and optionally
     *  `featurePermissionedDomains`) are active.  Returning `false` causes
     *  `invokePreflight` to short-circuit with `temDISABLED` before any
     *  field parsing occurs.
     *
     *  @param ctx The preflight context.
     *  @return `true` if all required amendments are enabled, `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Return the legal transaction-flag bits for this transactor.
     *
     *  The mask covers `tfLoanLatePayment`, `tfLoanFullPayment`, and
     *  `tfLoanOverpayment`; any other flag bits produce `temINVALID_FLAG`.
     *
     *  @param ctx The preflight context (unused; present for pipeline conformance).
     *  @return `tfLoanPayMask`.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless structural validation of a `ttLOAN_PAY` transaction.
     *
     *  Performs two field checks with no ledger access:
     *  - `sfLoanID` must be non-zero.
     *  - `sfAmount` must be positive.
     *
     *  Enforces mutual exclusivity of the payment-mode flags by counting
     *  set bits with `std::popcount`; a `static_assert` at compile time
     *  verifies that the three flag bits exactly cover the bits not in
     *  `tfLoanPayMask | tfUniversal`, keeping the mask and flag definitions
     *  in sync.
     *
     *  @param ctx The preflight context carrying the transaction.
     *  @return `tesSUCCESS` on success; `temINVALID` if `sfLoanID` is zero;
     *      `temBAD_AMOUNT` if `sfAmount` is non-positive; `temINVALID_FLAG`
     *      if more than one payment-mode flag is set simultaneously.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Compute the transaction fee scaled to the estimated payment count.
     *
     *  Most transactors delegate entirely to `Transactor::calculateBaseFee`.
     *  `LoanPay` is unusual because a single transaction can process many
     *  amortization installments, and charging a flat fee regardless of
     *  volume would create an attack surface for forcing expensive computation
     *  at low cost.
     *
     *  The fee estimate reads the ledger hierarchy loan → loanbroker → vault
     *  to determine the asset, periodic payment, and service fee, then
     *  computes `numPaymentEstimate = amount / regularPayment`.  One base-fee
     *  unit is charged per `kLOAN_PAYMENTS_PER_FEE_INCREMENT` estimated
     *  payments (rounded up, minimum 1).  When `fixSecurity3_1_3` is active,
     *  the total is capped at `kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION /
     *  kLOAN_PAYMENTS_PER_FEE_INCREMENT` increments — the hard processing
     *  cap that `loanMakePayment` enforces at execution time.
     *
     *  Late and full payments always return the normal single-base fee
     *  because they perform a bounded, fixed amount of work regardless of
     *  `sfAmount`.  Overpayments use upward rounding for the payment
     *  estimate (conservative billing), while regular payments use downward
     *  rounding.
     *
     *  If any ledger object along the loan → broker → vault chain is missing
     *  or the asset is mismatched, the method falls back to the normal fee
     *  and defers the error to `preclaim`.
     *
     *  @param view The read-only ledger view used to look up loan objects.
     *  @param tx   The serialized transaction.
     *  @return The scaled fee in drops; at least one base-fee unit.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Read-only ledger checks for a `ttLOAN_PAY` transaction.
     *
     *  Loads the `Loan`, `LoanBroker`, and `Vault` objects and enforces:
     *  - **Ownership**: `sfBorrower` on the loan must equal `sfAccount` on
     *    the transaction (`tecNO_PERMISSION`).
     *  - **Overpayment permission**: if `tfLoanOverpayment` is set but the
     *    loan's `lsfLoanOverpayment` flag is absent, returns `tecNO_PERMISSION`
     *    when `fixSecurity3_1_3` is active, or `temINVALID_FLAG` otherwise
     *    (a versioned correction to historical behaviour).
     *  - **Loan completeness**: if `sfPaymentRemaining == 0` or
     *    `sfPrincipalOutstanding == 0` the loan is already discharged
     *    (`tecKILLED`).
     *  - **Asset consistency**: `sfAmount`'s asset must match the vault's
     *    `sfAsset` (`tecWRONG_ASSET`).
     *  - **Freeze and authorization**: the borrower must not be frozen for
     *    the asset; the vault pseudo-account must not be deep-frozen; the
     *    borrower must hold the required authorization.
     *  - **Balance sufficiency**: the borrower must hold at least the full
     *    `sfAmount`; partial-payment semantics are explicitly rejected.
     *
     *  @note The "broker does not exist" and "vault does not exist" branches
     *      are marked `LCOV_EXCL_*` because the protocol maintains referential
     *      integrity between Loan → LoanBroker → Vault — reaching them
     *      implies ledger corruption.
     *
     *  @param ctx The preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all checks pass; a `tec`/`ter` code otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the loan repayment and update all affected ledger objects.
     *
     *  Mutates three ledger objects atomically: `Loan`, `LoanBroker`, and
     *  `Vault`.  The sequence is:
     *
     *  1. **Impairment unwind**: if the loan carries `lsfLoanImpaired`,
     *     calls `LoanManage::unimpairLoan` to restore tracked fields before
     *     payment arithmetic runs.  Aborts on failure.
     *  2. **Payment dispatch**: maps transaction flags to a `LoanPaymentType`
     *     enum value and calls `loanMakePayment`, which performs the
     *     amortization computation and modifies `loanSle` in-place, returning
     *     a `LoanPaymentParts` struct (`principalPaid`, `interestPaid`,
     *     `feePaid`, `valueChange`).
     *  3. **Broker fee routing**: directs the service fee to the broker
     *     owner's account if cover available ≥ minimum required (rounded up),
     *     the owner is not deep-frozen, and the owner holds authorization;
     *     otherwise accumulates the fee in the broker pseudo-account (the
     *     first-loss cover pool).  If both the owner and the pseudo-account
     *     are deep-frozen, the transaction fails.
     *  4. **Vault accounting**: credits `sfAssetsAvailable` by
     *     `totalPaidToVaultRounded` (principal + interest rounded down to
     *     vault scale) and adjusts `sfAssetsTotal` by `valueChange`.  Adjusts
     *     the broker's `sfDebtTotal` via `adjustImpreciseNumber`, which
     *     re-rounds to vault scale and floors at zero to absorb accumulated
     *     rounding drift.
     *  5. **Fund transfer**: a single `accountSendMulti` call moves funds
     *     from the borrower to the vault pseudo-account and the broker payee
     *     with `WaiveTransferFee::Yes` — transfer fees are waived because
     *     the lending protocol's amortization schedule dictates exact amounts.
     *
     *  @note In debug builds, two `#if !NDEBUG` blocks verify conservation of
     *      funds and that `sfAssetsAvailable` agrees with the vault
     *      pseudo-account's actual token balance before and after the transfer.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if any required SLE
     *      is missing; a `tec` code from `loanMakePayment` or
     *      `LoanManage::unimpairLoan` on computation failure; `tecINTERNAL`
     *      if vault invariants are violated post-accounting.
     */
    TER
    doApply() override;

    /** Accumulate per-entry state for transaction-specific invariant checks.
     *
     *  Reserved for future `LoanPay`-specific invariant logic; currently a
     *  no-op.  The framework calls this once per modified SLE during the
     *  invariant-check phase.
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

    /** Verify that transaction-specific invariants hold after `doApply`.
     *
     *  Reserved for future `LoanPay`-specific invariant logic; currently
     *  always returns `true`.  Returning `false` would trigger
     *  `tecINVARIANT_FAILED`.
     *
     *  @param tx     The applied transaction.
     *  @param result The TER returned by `doApply`.
     *  @param fee    The fee deducted from the submitting account.
     *  @param view   Read-only view of the post-transaction ledger.
     *  @param j      Journal for invariant-failure diagnostics.
     *  @return `true` always (no checks implemented yet).
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
