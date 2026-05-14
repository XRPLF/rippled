#pragma once

#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Loan origination transactor for the XLS-66 lending protocol.
 *
 * `LoanSet` creates a new `Loan` ledger entry, transferring principal from a
 * vault's pseudo-account to a borrower, deducting any origination fee to the
 * broker owner, and recording the full amortization schedule.  It is the
 * entry point to the loan lifecycle; `LoanPay` services existing loans and
 * `LoanDelete` terminates them.
 *
 * Every `LoanSet` requires a cryptographic signature from the counterparty
 * (the broker owner), because the transaction binds both the lender's vault
 * and the borrower.  The sole exception is when the transaction runs as a
 * Batch inner transaction (`tfInnerBatchTxn`), where the outer batch
 * authorization subsumes the counterparty signature.
 *
 * @note `ConsequencesFactory` is `Normal` — this transaction does not
 *     unconditionally block later transactions from the same account.
 */
class LoanSet : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Gates the entire transactor on all XLS-66 prerequisite amendments.
     *
     * Delegates to `checkLendingProtocolDependencies`, which verifies that
     * every required feature flag is active in `ctx.rules`.  Returning
     * `false` causes the framework to reject the transaction before preflight
     * runs.
     *
     * @param ctx Preflight context providing active amendment rules.
     * @return `true` if all XLS-66 dependencies are enabled; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /**
     * Returns the set of flags valid for this transaction type.
     *
     * Only `tfLoanOverpayment` (which permits the borrower to pay ahead of
     * schedule) is meaningful.  Any other flag bits are rejected by
     * `preflight0`.
     *
     * @param ctx Preflight context (unused; present for framework uniformity).
     * @return `tfLoanSetMask` — the bitmask of allowed transaction flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /**
     * Stateless validation pass — no ledger access.
     *
     * Enforces the following invariants in order:
     * - A Batch inner transaction without `sfCounterparty` is rejected
     *   (`temBAD_SIGNER`), as batch authorization replaces the counterparty
     *   signature only when `sfCounterparty` is explicitly supplied.
     * - A non-batch transaction without `sfCounterpartySignature` is rejected
     *   (`temBAD_SIGNER`).
     * - Rate fields (`sfInterestRate`, `sfLateInterestRate`,
     *   `sfCloseInterestRate`, `sfOverpaymentInterestRate`, `sfOverpaymentFee`)
     *   are range-checked against their respective protocol maxima.
     * - Fee fields (`sfLoanServiceFee`, `sfLatePaymentFee`,
     *   `sfClosePaymentFee`) must be non-negative.
     * - `sfPrincipalRequested` must be positive; `sfLoanOriginationFee`
     *   must not exceed it.
     * - `sfPaymentInterval` must be ≥ `kMIN_PAYMENT_INTERVAL`.
     * - `sfGracePeriod`, when present, must be in
     *   [`kDEFAULT_GRACE_PERIOD`, `paymentInterval`].
     * - `sfLoanBrokerID` must not be the zero hash.
     *
     * @param ctx Preflight context carrying the transaction and active rules.
     * @return `tesSUCCESS` on success; a `tem*` code on any validation failure.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Verifies both the primary and counterparty signatures.
     *
     * Calls the base-class `Transactor::checkSign` for the transaction
     * submitter, then resolves the counterparty identity: if `sfCounterparty`
     * is present in the transaction it is used directly; otherwise the broker's
     * `sfOwner` field is read from the ledger view.  The resolved identity is
     * then passed back into `Transactor::checkSign` with the
     * `sfCounterpartySignature` sub-object, which may itself be a single
     * signature or a full multisig quorum.
     *
     * @param ctx Preclaim context providing the read-only ledger view.
     * @return `tesSUCCESS` if both signatures are valid; `temBAD_SIGNER` if
     *     the counterparty cannot be resolved; otherwise a signature-check
     *     error from the base class.
     */
    static NotTEC
    checkSign(PreclaimContext const& ctx);

    /**
     * Prices the transaction, adding one base fee per counterparty signer.
     *
     * The base cost is computed by `Transactor::calculateBaseFee`.  Each
     * signer in `sfCounterpartySignature` — whether a single-signature entry
     * (`sfTxnSignature` present) or each member of a multisig quorum
     * (`sfSigners` present) — adds one additional `baseFee` unit.  This
     * mirrors how the base class charges for entries in `sfSigners` on
     * multisig transactions.
     *
     * @param view Read-only ledger view used to obtain the current base fee.
     * @param tx   The transaction being fee-priced.
     * @return Total fee in drops: normal cost + (counterparty signer count ×
     *     base fee).
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /**
     * Returns the `STNumber` fields that must be representable in the vault's
     * asset type without precision loss.
     *
     * The returned list covers: `sfPrincipalRequested`, `sfLoanOriginationFee`,
     * `sfLoanServiceFee`, `sfLatePaymentFee`, and `sfClosePaymentFee`.
     * Overpayment fee is excluded because it is stored as a rate, not an
     * absolute amount.
     *
     * This list is checked twice: once coarsely in `preclaim` (before the
     * final loan scale is known, catching obvious type mismatches for integral
     * asset types), and again in `doApply` after `computeLoanProperties`
     * establishes the exact scale (necessary for IOU assets, where decimal
     * precision can push a value below the type's resolution).
     *
     * @return A static reference to the list of optioned `STNumber` fields.
     */
    static std::vector<OptionaledField<STNumber>> const&
    getValueFields();

    /**
     * Ledger-state-dependent validation — read-only.
     *
     * Performs the following checks in order:
     * 1. **Schedule overflow guard**: verifies that
     *    `startDate + (interval × total) + gracePeriod` cannot exceed
     *    `std::numeric_limits<uint32_t>::max()`.  Returns `tecKILLED` if any
     *    intermediate value would overflow, before loading any SLEs.
     * 2. Confirms the `LoanBroker` SLE exists; returns `tecNO_ENTRY` if not.
     * 3. Verifies that the transaction submitter or explicit counterparty is
     *    the broker's owner; returns `tecNO_PERMISSION` otherwise.
     * 4. Confirms the borrower account exists; returns `terNO_ACCOUNT` if not.
     * 5. Checks that the vault's `sfAssetsTotal` has not reached
     *    `sfAssetsMaximum`; returns `tecLIMIT_EXCEEDED` if so.
     * 6. Validates coarse representability of all value fields in the vault
     *    asset type; returns `tecPRECISION_LOSS` on failure.
     * 7. Confirms there is capacity to add a new holding; delegates to
     *    `canAddHolding`.
     * 8. Checks that the vault pseudo-account, broker pseudo-account, borrower,
     *    and broker owner are not frozen for the loan asset.
     *
     * @param ctx Preclaim context providing the read-only ledger view.
     * @return `tesSUCCESS` on success; a `tec*` or `ter*` code on failure.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Executes the loan origination, mutating ledger state.
     *
     * The apply sequence is:
     * 1. Recompute `computeLoanProperties` (amortization schedule, periodic
     *    payment, management fees) and verify the interest due would not push
     *    the vault over its `sfAssetsMaximum`.
     * 2. Re-check value-field precision with the final loan scale (IOU assets
     *    only; integral types were fully checked in `preclaim`).
     * 3. Run `checkLoanGuards` to enforce numeric precision invariants at the
     *    computed scale.
     * 4. Verify the broker's `sfDebtTotal` would not exceed `sfDebtMaximum`.
     * 5. Verify `sfCoverAvailable` meets `sfCoverRateMinimum` after the new
     *    loan is added — minimum cover is rounded upward to protect solvency.
     * 6. Adjust the borrower's owner count (+1) and check XRP reserve.
     * 7. Create trust-line / MPT holdings for the borrower and broker owner on
     *    demand; require `StrongAuth` authorization for both.
     * 8. Disburse funds atomically via `accountSendMulti`:
     *    `(principalRequested − originationFee)` to the borrower and
     *    `originationFee` to the broker owner, both in one operation.
     * 9. Insert the `Loan` SLE with all amortization fields populated.
     * 10. Decrement `sfAssetsAvailable` and increment `sfAssetsTotal` on the
     *     vault.
     * 11. Increment `sfDebtTotal`, `sfLoanSequence`, and owner count on the
     *     broker SLE (rolls back with `tecMAX_SEQUENCE_REACHED` on overflow).
     * 12. Link the loan into the broker pseudo-account's directory and the
     *     borrower's owner directory.
     * 13. Call `associateAsset` on the vault, broker, and loan SLEs.
     *
     * @return `tesSUCCESS` on success; `tec*` or `tef*` on failure.
     *     Any `tec*` return causes the framework to discard all state changes
     *     via `ApplyContext::discard()` and re-apply only the fee.
     */
    TER
    doApply() override;

    /**
     * Per-entry callback for the transaction's invariant checker.
     *
     * No transaction-specific invariants are registered yet; this is a
     * placeholder for future work.
     *
     * @param isDelete `true` if the entry is being deleted.
     * @param before   SLE state before the transaction (may be null for new
     *     entries).
     * @param after    SLE state after the transaction (may be null for deleted
     *     entries).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Final invariant check called once after all entries are visited.
     *
     * No transaction-specific invariants are registered yet; always returns
     * `true`.
     *
     * @param tx     The transaction that was applied.
     * @param result The TER returned by `doApply`.
     * @param fee    The fee charged to the submitting account.
     * @param view   Read-only view of the post-apply ledger state.
     * @param j      Journal for diagnostic logging.
     * @return `true` — no invariant violations detected.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

public:
    /// Minimum number of scheduled payments; allows balloon (single-payment)
    /// loans as the degenerate case.
    static std::uint32_t constexpr kMIN_PAYMENT_TOTAL = 1;

    /// Default number of scheduled payments when `sfPaymentTotal` is absent.
    static std::uint32_t constexpr kDEFAULT_PAYMENT_TOTAL = 1;
    static_assert(kDEFAULT_PAYMENT_TOTAL >= kMIN_PAYMENT_TOTAL);

    /// Minimum payment cadence in seconds; prevents loans that fire faster than
    /// ledger close times can reliably track.
    static std::uint32_t constexpr kMIN_PAYMENT_INTERVAL = 60;

    /// Default payment interval in seconds when `sfPaymentInterval` is absent.
    static std::uint32_t constexpr kDEFAULT_PAYMENT_INTERVAL = 60;
    static_assert(kDEFAULT_PAYMENT_INTERVAL >= kMIN_PAYMENT_INTERVAL);

    /// Default grace period in seconds when `sfGracePeriod` is absent.
    /// Must be ≥ `kMIN_PAYMENT_INTERVAL` to avoid negative-time schedules.
    static std::uint32_t constexpr kDEFAULT_GRACE_PERIOD = 60;
    static_assert(kDEFAULT_GRACE_PERIOD >= kMIN_PAYMENT_INTERVAL);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
