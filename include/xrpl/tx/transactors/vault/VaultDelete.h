/** @file
 *  Declares the `VaultDelete` transactor, which tears down a Single-Sided AMM
 *  vault and all of its associated ledger objects.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttVAULT_DELETE` transaction type.
 *
 *  Performs a full multi-object teardown of a vault that has been fully
 *  drained by its depositors.  The teardown sequence is:
 *  1. Remove the pseudo-account's zero-balance asset holding.
 *  2. Remove the vault owner's MPToken for share issuance (if any).
 *  3. Erase the share MPTokenIssuance and its directory entry.
 *  4. Erase the vault pseudo-account.
 *  5. Remove the vault SLE from the owner's directory and decrement the
 *     owner count by 2 (one for the vault object, one for the pseudo-account).
 *
 *  Deletion is only possible once every depositor has withdrawn: `preclaim`
 *  enforces that `sfAssetsAvailable`, `sfAssetsTotal`, and the share
 *  issuance's `sfOutstandingAmount` are all zero before allowing `doApply`
 *  to run.  This makes `VaultDelete` the terminal state of the vault lifecycle
 *  and the strict inverse of `VaultCreate`.
 *
 *  @note This transactor is gated on the `featureSingleAssetVault` amendment
 *      and is not delegable (`Delegation::NotDelegable`).
 */
class VaultDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless preflight validation.
     *
     *  Confirms that `sfVaultID` is non-zero.  A zero vault ID cannot
     *  correspond to any ledger object and is rejected immediately.
     *
     *  @return `temMALFORMED` if `sfVaultID` is zero; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger checks before state mutation.
     *
     *  Enforces the three-invariant guard that makes safe deletion possible:
     *  - The submitting account must be the vault `sfOwner`.
     *  - `sfAssetsAvailable` on the vault must be zero.
     *  - `sfAssetsTotal` on the vault must be zero.
     *  - `sfOutstandingAmount` on the share MPTokenIssuance must be zero.
     *
     *  Any outstanding assets or shares indicate depositors have not yet
     *  withdrawn; the vault cannot be deleted while it holds funds.
     *
     *  @return `tecNO_ENTRY` if the vault does not exist; `tecNO_PERMISSION`
     *      if the submitter is not the vault owner or the share issuance owner
     *      is mismatched; `tecHAS_OBLIGATIONS` if any asset or share balance
     *      is non-zero; `tecOBJECT_NOT_FOUND` if the share issuance SLE is
     *      missing; `tesSUCCESS` otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the vault teardown, mutating ledger state.
     *
     *  Removes five categories of ledger objects in dependency order.
     *  Defensive `tecHAS_OBLIGATIONS` guards (marked `LCOV_EXCL`) verify the
     *  pseudo-account reaches a clean state (zero balance, zero owner count,
     *  no directory) before it is erased; these guards protect against bugs
     *  in earlier cleanup steps and are not reachable through valid
     *  transaction sequences.  Conditions that indicate ledger corruption
     *  (missing pseudo-account, mismatched issuance owner) return
     *  `tefBAD_LEDGER` or `tefINTERNAL` rather than a fee-claiming `tec`
     *  code.
     *
     *  @return `tesSUCCESS` on complete teardown; a `tef` code if ledger
     *      corruption is detected; a `tec` code from `removeEmptyHolding` if
     *      a holding cannot be removed.
     */
    TER
    doApply() override;

    /** Per-SLE visitor called by the invariant checker framework.
     *
     *  Accumulates per-entry accounting needed by `finalizeInvariants`.
     *  Called once for each SLE that was modified, inserted, or deleted
     *  during `doApply`.
     *
     *  @param isDelete  `true` if the SLE was erased.
     *  @param before    SLE state before the transaction; `nullptr` for
     *      newly inserted entries.
     *  @param after     SLE state after the transaction; `nullptr` for
     *      deleted entries.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Terminal invariant check called after all entries have been visited.
     *
     *  Validates that the net effect of the transaction satisfies the
     *  `ValidVault` invariant (asset/share conservation, immutable field
     *  constraints).  Returns `false` and logs diagnostics if any violation
     *  is detected, causing the framework to escalate to
     *  `tecINVARIANT_FAILED`.
     *
     *  @param tx      The transaction being applied.
     *  @param result  The TER returned by `doApply`.
     *  @param fee     The fee charged for the transaction.
     *  @param view    Read-only view of the post-apply ledger state.
     *  @param j       Journal for diagnostic logging.
     *  @return `true` if all invariants pass; `false` if any violation is
     *      detected.
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
