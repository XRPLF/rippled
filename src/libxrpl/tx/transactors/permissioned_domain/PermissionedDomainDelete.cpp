/** @file
 *  Implements the `PermissionedDomainDelete` transactor, which removes a
 *  permissioned domain SLE from the XRPL ledger and recovers the owner
 *  reserve locked at creation.
 *
 *  The three-phase pipeline is: `preflight` (structural check on `sfDomainID`)
 *  → `preclaim` (existence + ownership, read-only) → `doApply` (directory
 *  removal, owner-count decrement, SLE erasure).
 */
#include <xrpl/tx/transactors/permissioned_domain/PermissionedDomainDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/** Reject structurally invalid transactions before consulting ledger state.
 *
 *  A zero-valued `sfDomainID` is analogous to a null pointer — it cannot
 *  reference any real domain object. All ledger-state checks (existence,
 *  ownership) are deferred to `preclaim` so that they can influence
 *  fee-claiming behaviour.
 *
 *  @param ctx  The preflight context containing the raw transaction.
 *  @return `temMALFORMED` if `sfDomainID` is zero; `tesSUCCESS` otherwise.
 */
NotTEC
PermissionedDomainDelete::preflight(PreflightContext const& ctx)
{
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    if (domain == beast::kZERO)
        return temMALFORMED;

    return tesSUCCESS;
}

/** Verify domain existence and submitter ownership against live ledger state.
 *
 *  Resolves `sfDomainID` to a `PermissionedDomain` SLE via
 *  `keylet::permissionedDomain`. Only the account that created the domain
 *  (`sfOwner`) may delete it — there is no admin override or co-ownership
 *  model. Placing these checks here (rather than in `doApply`) ensures that
 *  an unauthorized or nonexistent-domain attempt still charges the fee.
 *
 *  @param ctx  The preclaim context with read-only ledger view.
 *  @return `tecNO_ENTRY` if the domain SLE does not exist in the current
 *      ledger; `tecNO_PERMISSION` if the submitter is not the domain owner;
 *      `tesSUCCESS` otherwise.
 */
TER
PermissionedDomainDelete::preclaim(PreclaimContext const& ctx)
{
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    auto const sleDomain = ctx.view.read(keylet::permissionedDomain(domain));

    if (!sleDomain)
        return tecNO_ENTRY;

    XRPL_ASSERT(
        sleDomain->isFieldPresent(sfOwner) && ctx.tx.isFieldPresent(sfAccount),
        "xrpl::PermissionedDomainDelete::preclaim : required fields present");
    if (sleDomain->getAccountID(sfOwner) != ctx.tx.getAccountID(sfAccount))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/** Remove the permissioned domain object and release the owner reserve.
 *
 *  Performs three mutations in strict order:
 *  1. `view().dirRemove()` — removes the SLE's back-reference from the
 *     account's owner directory using the page index stored in `sfOwnerNode`.
 *     Passing `true` also cleans up the directory page if it becomes empty.
 *     Failure is unreachable for a well-formed ledger and is marked
 *     `LCOV_EXCL`; it returns `tefBAD_LEDGER` if somehow triggered.
 *  2. `adjustOwnerCount(..., -1, j)` — decrements `sfOwnerCount`, recovering
 *     the base reserve that was locked when the domain was created.
 *  3. `view().erase(slePd)` — removes the `PermissionedDomain` SLE from the
 *     ledger view. This is the final, irreversible step.
 *
 *  @return `tefBAD_LEDGER` on owner-directory corruption (unreachable under
 *      normal operation); `tesSUCCESS` otherwise.
 */
TER
PermissionedDomainDelete::doApply()
{
    XRPL_ASSERT(
        ctx_.tx.isFieldPresent(sfDomainID),
        "xrpl::PermissionedDomainDelete::doApply : required field present");

    auto const slePd = view().peek(keylet::permissionedDomain(ctx_.tx.at(sfDomainID)));
    auto const page = (*slePd)[sfOwnerNode];

    if (!view().dirRemove(keylet::ownerDir(account_), page, slePd->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete permissioned domain directory entry.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const ownerSle = view().peek(keylet::account(account_));
    XRPL_ASSERT(
        ownerSle && ownerSle->getFieldU32(sfOwnerCount) > 0,
        "xrpl::PermissionedDomainDelete::doApply : nonzero owner count");
    adjustOwnerCount(view(), ownerSle, -1, ctx_.journal);
    view().erase(slePd);

    return tesSUCCESS;
}

/** Per-entry invariant visitor — no domain-deletion-specific checks yet.
 *
 *  @note This is a placeholder for future per-entry invariant enforcement.
 */
void
PermissionedDomainDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** Post-transaction invariant finalization — always passes.
 *
 *  @note This is a placeholder for future transaction-level invariant checks;
 *      no domain-deletion-specific invariants are enforced yet.
 *  @return `true` unconditionally.
 */
bool
PermissionedDomainDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
