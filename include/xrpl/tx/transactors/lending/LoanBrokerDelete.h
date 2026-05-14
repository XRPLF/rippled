#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for deleting a LoanBroker and recovering its cover assets.
 *
 *  Processes `ttLOAN_BROKER_DELETE` (type 75) transactions. A `LoanBroker`
 *  intermediates between a vault and individual borrowers, holding cover
 *  collateral in an associated pseudo-account. This transactor tears down
 *  the broker once all loans are closed: it returns any remaining cover to
 *  the human owner, removes the pseudo-account, and erases both SLEs.
 *
 *  @see LoanBrokerSet, LoanBrokerCoverDeposit, LoanBrokerCoverWithdraw
 */
class LoanBrokerDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanBrokerDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gates the transaction on the lending protocol amendment.
     *
     *  Delegates to `checkLendingProtocolDependencies`. Amendment checks
     *  belong here, not in `preflight`.
     *
     *  @param ctx Preflight context providing ledger rules.
     *  @return `true` if the lending protocol amendment is enabled;
     *      `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of transaction fields.
     *
     *  Rejects a zero `sfLoanBrokerID` (null broker reference). No ledger
     *  access is performed.
     *
     *  @param ctx Preflight context carrying the transaction fields.
     *  @return `temINVALID` if `sfLoanBrokerID` is zero; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates broker deletion eligibility against current ledger state (read-only).
     *
     *  Checks in order:
     *  1. The `LoanBroker` object exists (`tecNO_ENTRY`).
     *  2. The sender is the broker's `sfOwner` (`tecNO_PERMISSION`).
     *  3. The broker's `sfOwnerCount` is zero — no active `Loan` objects
     *     reference it (`tecHAS_OBLIGATIONS`).
     *  4. The associated vault exists (`tefBAD_LEDGER` — ledger corruption;
     *     structurally unreachable under normal conditions).
     *  5. Any residual `sfDebtTotal` rounds to zero against the vault's asset
     *     scale (`tecHAS_OBLIGATIONS` — defensive guard; should have been
     *     cleared by the last `LoanDelete`).
     *  6. If `sfCoverAvailable > 0`, the broker owner is not deep-frozen for
     *     the vault asset (`tecFROZEN`) — cover will be returned on deletion,
     *     so the owner must be able to receive it.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return A `TER` indicating the first failing check, or `tesSUCCESS`.
     *  @note The missing-vault and non-zero-rounded-debt branches are excluded
     *      from coverage (`LCOV_EXCL_*`) because they are unreachable in
     *      practice under a consistent ledger.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Executes broker deletion against the mutable ledger view.
     *
     *  In order:
     *  1. Removes the broker from both the human owner's directory
     *     (`sfOwnerNode`) and the vault pseudo-account's directory
     *     (`sfVaultNode`).
     *  2. Transfers any `sfCoverAvailable` from the broker pseudo-account to
     *     the human owner via `accountSend` with `WaiveTransferFee::Yes`
     *     (cleanup transfer, not user-initiated).
     *  3. Calls `removeEmptyHolding` to clear the trust line or MPT position
     *     on the broker pseudo-account for the vault asset.
     *  4. Validates that the pseudo-account has no remaining balance, owner
     *     count, or directory — guards are `LCOV_EXCL` because prior steps
     *     should have eliminated all obligations.
     *  5. Erases the broker pseudo-account SLE, then the broker SLE.
     *  6. Decrements the human owner's `sfOwnerCount` by 2 (one for the
     *     `LoanBroker` object, one for its pseudo-account).
     *  7. Calls `associateAsset` to record the vault asset touched by this
     *     transaction.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if a required SLE is
     *      missing or a directory operation fails (indicates ledger corruption);
     *      `tecHAS_OBLIGATIONS` if the pseudo-account retains residual state
     *      after cleanup (defensive, should be unreachable).
     */
    TER
    doApply() override;

    /** Per-entry hook for transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; this
     *  override is a placeholder for future work.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before SLE state before the transaction, or `nullptr` for new entries.
     *  @param after SLE state after the transaction, or `nullptr` for deleted entries.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalizes transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; always
     *  returns `true`. Placeholder for future work.
     *
     *  @param tx The transaction being applied.
     *  @param result The `TER` result from `doApply`.
     *  @param fee The XRP fee charged for the transaction.
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
