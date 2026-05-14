#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Transactor that removes a permissioned domain from the ledger.
 *
 * A permissioned domain constrains access to certain ledger operations (AMM,
 * DEX) to accounts that satisfy a configured set of credentials. This
 * transactor is the controlled teardown path: the domain owner deletes the
 * domain SLE, removes it from the owner directory, and recovers the base
 * reserve that was locked at creation time.
 *
 * Unlike `PermissionedDomainSet`, this class intentionally omits a
 * `checkExtraFeatures` override. The base-class implementation returns `true`
 * unconditionally, so deletion is always permitted once the domain object
 * exists — objects must never be stranded by an amendment state change.
 *
 * Pipeline: `preflight` (structural check) → `preclaim` (existence + ownership)
 * → `doApply` (directory removal, owner-count decrement, SLE erasure).
 */
class PermissionedDomainDelete : public Transactor
{
public:
    /**
     * Consequences factory type for this transactor.
     *
     * `Normal` indicates that the worst-case impact on the submitting account's
     * spendable balance is the transaction fee alone. Deleting a domain returns
     * a reserve increment but does not transfer XRP between parties in an
     * unbounded way, so no special queuing treatment is required.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit PermissionedDomainDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Stateless preflight validation — no ledger access.
     *
     * Confirms that `sfDomainID` is non-zero. A zero value is structurally
     * malformed; all ledger-state checks (existence, ownership) are deferred
     * to `preclaim` so they can influence fee-claiming behaviour.
     *
     * @param ctx  The preflight context containing the transaction.
     * @return `temMALFORMED` if `sfDomainID` is zero; `tesSUCCESS` otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Read-only ledger validation — existence and ownership checks.
     *
     * Resolves `sfDomainID` to a `PermissionedDomain` SLE via
     * `keylet::permissionedDomain` and verifies two conditions:
     * - The domain exists in the current ledger view.
     * - The submitting account (`sfAccount`) is the domain's `sfOwner`.
     *
     * Placing these checks here rather than in `doApply` ensures that an
     * unauthorized or nonexistent-domain delete still results in a fee claim.
     *
     * @param ctx  The preclaim context with read-only ledger view.
     * @return `tecNO_ENTRY` if the domain does not exist;
     *         `tecNO_PERMISSION` if the submitter is not the domain owner;
     *         `tesSUCCESS` otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Apply the domain deletion to the mutable ledger view.
     *
     * Performs three mutations in order:
     * 1. `view().dirRemove()` — removes the SLE from the owner's directory,
     *    which is required for correct reserve accounting. Failure here
     *    indicates ledger corruption and returns `tefBAD_LEDGER`.
     * 2. `adjustOwnerCount(view(), ownerSle, -1, j)` — decrements the owner's
     *    `sfOwnerCount`, releasing the base reserve locked at creation.
     * 3. `view().erase(slePd)` — removes the `PermissionedDomain` SLE.
     *
     * @return `tefBAD_LEDGER` if the owner-directory entry cannot be removed
     *         (ledger corruption; marked `LCOV_EXCL` as unreachable in
     *         production); `tesSUCCESS` otherwise.
     */
    TER
    doApply() override;

    /**
     * Per-entry invariant visitor — currently a no-op placeholder.
     *
     * Called once for each SLE modified by this transaction. No
     * domain-deletion-specific invariants are enforced at the entry level yet.
     *
     * @param isDelete  `true` if the entry is being deleted.
     * @param before    SLE state before the transaction (may be null for new
     *                  entries).
     * @param after     SLE state after the transaction (may be null for
     *                  deletions).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Post-transaction invariant check — currently a no-op placeholder.
     *
     * Called once after all `visitInvariantEntry` calls for this transaction.
     * No domain-deletion-specific invariants are enforced yet; always returns
     * `true`.
     *
     * @param tx     The transaction being applied.
     * @param result The TER result from `doApply`.
     * @param fee    The fee charged for this transaction.
     * @param view   Read-only view of the post-transaction ledger state.
     * @param j      Journal for diagnostic logging.
     * @return `true` unconditionally (no invariant violations possible yet).
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
