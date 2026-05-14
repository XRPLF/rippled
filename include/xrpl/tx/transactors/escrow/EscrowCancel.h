#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the EscrowCancel transaction type.
 *
 *  Returns locked funds to the escrow creator once the escrow's
 *  `sfCancelAfter` time has elapsed.  This is the "reclaim" counterpart to
 *  `EscrowFinish`: where `EscrowFinish` releases funds to the intended
 *  recipient when conditions are met, `EscrowCancel` unwinds the escrow
 *  entirely and restores the held amount to the originating account.
 *
 *  The `kCONSEQUENCES_FACTORY` is `Normal` because cancellation unlocks
 *  funds rather than locking them; no bespoke `makeTxConsequences` is
 *  needed.  Contrast with `EscrowCreate`, which uses `Custom` to account for
 *  the arbitrary amount of XRP or tokens it locks up.
 *
 *  The three-phase interface (`preflight`, `preclaim`, `doApply`) is invoked
 *  by the framework through compile-time template dispatch — not virtual
 *  dispatch.  Derived classes must match these exact signatures.
 */
class EscrowCancel : public Transactor
{
public:
    /** Uses the standard fee-consequence model; no custom factory needed. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit EscrowCancel(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless preflight validation for EscrowCancel.
     *
     *  There are no field-level constraints on `EscrowCancel` beyond what the
     *  framework's `preflight1`/`preflight2` wrappers already enforce (fee
     *  validity, flags, signature format).  All meaningful constraints —
     *  escrow existence, cancel-time expiry, asset authorization — require
     *  ledger state and are therefore deferred to `preclaim` and `doApply`.
     *
     *  @param ctx  stateless preflight context carrying the raw transaction.
     *  @return     always `tesSUCCESS`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks for EscrowCancel.
     *
     *  When `featureTokenEscrow` is active, verifies that the escrow object
     *  identified by `{sfOwner, sfOfferSequence}` exists (returning
     *  `tecNO_TARGET` if not), and for non-XRP amounts dispatches to
     *  `escrowCancelPreclaimHelper<Issue>` (IOU) or
     *  `escrowCancelPreclaimHelper<MPTIssue>` (MPT) to guard against
     *  `requireAuth` violations: if the issuer enabled authorization
     *  requirements after the escrow was created and the original escrow
     *  account is no longer authorized, cancellation is blocked.
     *
     *  Without `featureTokenEscrow`, this method returns `tesSUCCESS`
     *  unconditionally.
     *
     *  @param ctx  read-only preclaim context with ledger access.
     *  @return     `tesSUCCESS` on success; `tecNO_TARGET` if the escrow does
     *              not exist; a `requireAuth` error code if the escrow account
     *              is no longer authorized to hold the asset.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the EscrowCancel transaction to the mutable ledger.
     *
     *  Locates the escrow by `{sfOwner, sfOfferSequence}`, enforces that
     *  `sfCancelAfter` is set and has elapsed (returning `tecNO_PERMISSION`
     *  if either condition fails), then:
     *
     *  1. Removes the escrow from the owner's directory.
     *  2. Removes the escrow from the recipient's directory if
     *     `sfDestinationNode` is present.
     *  3. Returns the locked amount to the owner:
     *     - XRP: credited directly to the owner's `sfBalance`.
     *     - Non-XRP (`featureTokenEscrow` required): routed through
     *       `escrowUnlockApplyHelper`, followed by removal from the issuer's
     *       directory via `sfIssuerNode`.
     *  4. Decrements the owner's reserve count via `adjustOwnerCount`.
     *  5. Erases the escrow SLE.
     *
     *  If the escrow is absent at apply time with `featureTokenEscrow` active,
     *  `tecINTERNAL` is returned (considered unreachable in practice; marked
     *  `LCOV_EXCL_LINE`).  The legacy path returns `tecNO_TARGET`.
     *
     *  @return `tesSUCCESS` on successful cancellation; `tecNO_PERMISSION` if
     *          `sfCancelAfter` is absent or has not yet elapsed;
     *          `tecINTERNAL` or `tecNO_TARGET` if the escrow is missing at
     *          apply time; `tefBAD_LEDGER` if a directory removal fails
     *          (indicates ledger corruption).
     */
    TER
    doApply() override;

    /** No transaction-specific invariant entries to inspect yet.
     *
     *  Reserved for future per-entry post-condition checks.  Currently a
     *  no-op; the base-class invariant framework still runs its protocol-level
     *  checkers.
     *
     *  @param isDelete  true if the entry was erased.
     *  @param before    entry state before the transaction (nullptr if new).
     *  @param after     entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No transaction-specific finalization invariants yet.
     *
     *  Reserved for future post-condition checks after all entries have been
     *  visited.  Currently returns `true` unconditionally.
     *
     *  @param tx     the transaction being applied.
     *  @param result the tentative TER result.
     *  @param fee    fee consumed by the transaction.
     *  @param view   read-only view of the ledger after the transaction.
     *  @param j      journal for logging invariant failures.
     *  @return       always `true`.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
