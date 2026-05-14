#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for `ttLOAN_BROKER_COVER_CLAWBACK` (type 78).
 *
 *  Lets the issuer of a vault's underlying asset forcibly reclaim a portion
 *  of the cover funds held in a loan broker's pseudo-account.  Cover funds
 *  are tracked in `sfCoverAvailable`; the reclaimed amount is capped so the
 *  broker cannot be driven below the contractual minimum cover floor
 *  (`sfDebtTotal × sfCoverRateMinimum`).
 *
 *  @see LoanBrokerCoverDeposit   voluntary cover deposit by the broker operator
 *  @see LoanBrokerCoverWithdraw  voluntary cover withdrawal by the broker operator
 */
class LoanBrokerCoverClawback : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanBrokerCoverClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate on the `featureLendingProtocol` amendment and its dependencies.
     *
     *  Delegates to `checkLendingProtocolDependencies`; returns false (and
     *  therefore `temDISABLED`) if the lending protocol or any required
     *  prerequisite amendment is not yet active.
     *
     *  @param ctx  preflight context carrying the current rules set.
     *  @return true if all required amendments are enabled, false otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless field-level validation.
     *
     *  Requires at least one of `sfLoanBrokerID` or `sfAmount` to be present.
     *  If `sfLoanBrokerID` is absent the broker identity will be inferred from
     *  the IOU's issuer field in `sfAmount`, so in that case `sfAmount` must
     *  be an IOU (not MPT) and its holder must differ from the submitting
     *  account.  If `sfAmount` is provided it must be non-XRP and
     *  non-negative; zero is explicitly allowed as "take all surplus above the
     *  minimum cover floor."
     *
     *  @param ctx  preflight context; no ledger access.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation.
     *
     *  Resolves the broker identity (using `sfLoanBrokerID` if present, or by
     *  reading the pseudo-account named in the IOU issuer field), confirms the
     *  submitting account is the vault asset issuer, verifies the appropriate
     *  clawback permission flag (`lsfAllowTrustLineClawback` for IOUs,
     *  `lsfMPTCanClawback` for MPTs), and computes the effective claw amount.
     *  Performs a defensive `accountHolds` check to confirm the pseudo-account
     *  actually holds the computed amount — a mismatch indicates ledger state
     *  corruption and returns `tecINTERNAL`.
     *
     *  @param ctx  preclaim context with read-only ledger view.
     *  @return `tesSUCCESS` if the transaction may proceed; a `tec*` or
     *      `tef*` code otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the clawback to the mutable ledger.
     *
     *  Re-resolves the broker ID and claw amount (defensive re-computation
     *  against speculative-apply divergence; failures return `tecINTERNAL`
     *  and are marked `LCOV_EXCL_LINE`), decrements `sfCoverAvailable` on the
     *  broker SLE, and transfers the claw amount from the broker pseudo-account
     *  to the issuer via `accountSend` with `WaiveTransferFee::Yes`.  Calls
     *  `associateAsset` as the final step to re-round stored values to asset
     *  precision.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if broker or vault state
     *      unexpectedly diverged from what `preclaim` observed.
     */
    TER
    doApply() override;

    /** No-op stub for transaction-specific invariant entry collection.
     *
     *  No per-entry invariants are currently defined for this transaction type.
     *  Reserved for future work.
     *
     *  @param isDelete  true if the entry was erased.
     *  @param before    SLE state before the transaction.
     *  @param after     SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op stub for transaction-specific invariant finalization.
     *
     *  No post-conditions are currently defined for this transaction type.
     *  Always returns true.  Reserved for future work.
     *
     *  @param tx     the transaction being applied.
     *  @param result the tentative TER result.
     *  @param fee    fee consumed by the transaction.
     *  @param view   read-only ledger view after the transaction.
     *  @param j      journal for logging.
     *  @return true unconditionally.
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
