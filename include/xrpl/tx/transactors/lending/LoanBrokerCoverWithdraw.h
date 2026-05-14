#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for withdrawing cover assets from a LoanBroker.
 *
 *  Lets the broker owner reclaim cover funds held in the broker's
 *  pseudo-account, crediting the owner's account or a named third-party
 *  destination. Enforces the minimum cover ratio
 *  (`sfDebtTotal × sfCoverRateMinimum`) so that outstanding loan
 *  obligations remain adequately backed after the withdrawal.
 *
 *  Part of the XRPL lending protocol (XLS-66).
 *
 *  @see LoanBrokerCoverDeposit   voluntary cover deposit by the broker owner
 *  @see LoanBrokerCoverClawback  forced cover reclaim by the vault asset issuer
 */
class LoanBrokerCoverWithdraw : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanBrokerCoverWithdraw(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gates the transaction on the lending protocol amendment and its dependencies.
     *
     *  Delegates to `checkLendingProtocolDependencies`. Amendment checks
     *  belong here, not in `preflight`.
     *
     *  @param ctx Preflight context providing the current ledger rules.
     *  @return `true` if all required amendments are enabled; `false` otherwise,
     *      causing `invokePreflight` to return `temDISABLED`.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of transaction fields.
     *
     *  Rejects a zero `sfLoanBrokerID`, a non-positive or legally-invalid
     *  `sfAmount`, and a zero-value `sfDestination` when that field is
     *  present. No ledger access is performed.
     *
     *  @param ctx Preflight context carrying the transaction fields.
     *  @return `temINVALID` if `sfLoanBrokerID` is zero; `temBAD_AMOUNT` if
     *      `sfAmount` is non-positive or fails `isLegalNet`; `temMALFORMED`
     *      if `sfDestination` is present but zero; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates the withdrawal against current ledger state (read-only).
     *
     *  Checks in order:
     *  1. Destination (defaulting to the submitter) is not a pseudo-account
     *     (`tecPSEUDO_ACCOUNT`).
     *  2. The `LoanBroker` object exists (`tecNO_ENTRY`).
     *  3. The sender is the broker's `sfOwner` (`tecNO_PERMISSION`).
     *  4. The broker's associated vault exists (`tefBAD_LEDGER` — indicates
     *     ledger corruption; structurally unreachable under normal conditions).
     *  5. `sfAmount` asset matches the vault's `sfAsset` (`tecWRONG_ASSET`).
     *  6. Asset transfer guards: non-transferable (`canTransfer`), source-side
     *     freeze (`checkFrozen`), and deep-freeze on the destination
     *     (`checkDeepFrozen`). Freeze checks are skipped when sending directly
     *     to the asset issuer.
     *  7. When `sfDestination` names a third party, `canWithdraw` is called
     *     and `authType` is upgraded to `StrongAuth`, requiring the destination
     *     to have already consented to receive the asset (`requireAuth`).
     *  8. Minimum cover enforcement: `(sfCoverAvailable - sfAmount)` must be
     *     at least `roundUp(tenthBipsOfValue(sfDebtTotal, sfCoverRateMinimum))`.
     *     Both the cover-availability and the minimum-cover-ratio conditions
     *     return `tecINSUFFICIENT_FUNDS`.
     *  9. A final `accountHolds` guard against the broker pseudo-account
     *     confirms the asset is actually present (`tecINSUFFICIENT_FUNDS`).
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return A `TER` indicating the first failing check, or `tesSUCCESS`.
     *  @note The missing-vault branch is excluded from coverage
     *      (`LCOV_EXCL_*`) because ledger integrity constraints make it
     *      unreachable in practice.
     *  @note The minimum cover calculation uses `NumberRoundModeGuard` set to
     *      `Number::upward` so fractional asset units are always rounded up,
     *      never truncated.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Executes the cover withdrawal against the mutable ledger view.
     *
     *  Decrements `sfCoverAvailable` on the `LoanBroker` SLE by `sfAmount`,
     *  calls `associateAsset` to re-round stored values to asset precision,
     *  then delegates the actual token movement to `doWithdraw`, debiting
     *  the broker's pseudo-account and crediting the destination.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if the broker or vault
     *      SLE is unexpectedly absent (indicates ledger corruption —
     *      unreachable under normal conditions; marked `LCOV_EXCL_LINE`).
     */
    TER
    doApply() override;

    /** No-op stub for transaction-specific invariant entry collection.
     *
     *  No per-entry invariants are currently defined for this transaction type.
     *  Reserved for future work.
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

    /** No-op stub for transaction-specific invariant finalization.
     *
     *  No post-conditions are currently defined for this transaction type.
     *  Always returns `true`. Reserved for future work.
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
