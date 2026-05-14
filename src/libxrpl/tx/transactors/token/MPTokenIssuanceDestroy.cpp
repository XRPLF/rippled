#include <xrpl/tx/transactors/token/MPTokenIssuanceDestroy.h>

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

/** Trivial preflight: unconditionally succeeds.
 *
 *  A destroy transaction carries only `sfMPTokenIssuanceID` — no flags,
 *  amounts, or complex parameters that can be validated without ledger
 *  state.  The framework's `preflight0`/`preflight1` layers have already
 *  checked fee, sequence, and signature format by the time this runs.
 *  All substantive validation is deferred to `preclaim`, where a read-only
 *  ledger view is available.
 *
 *  @param ctx  Preflight context (unused beyond framework consumption).
 *  @return `tesSUCCESS` always.
 */
NotTEC
MPTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

/** Validate destruction preconditions against a read-only ledger view.
 *
 *  Performs three ordered checks against the `MPTokenIssuance` SLE:
 *
 *  1. **Existence** — looks up the SLE via `keylet::mptIssuance(sfMPTokenIssuanceID)`.
 *     An absent entry returns `tecOBJECT_NOT_FOUND`; the keylet lookup also
 *     implicitly validates the ID's format.
 *
 *  2. **Ownership** — `sfIssuer` on the SLE must match the submitting account.
 *     No other account may destroy an issuance, regardless of administrative
 *     authority.  Returns `tecNO_PERMISSION` on mismatch.
 *
 *  3. **Zero supply** — both `sfOutstandingAmount` (circulating tokens) and the
 *     optional `sfLockedAmount` (tokens held in escrow or protocol positions)
 *     must be exactly zero, checked separately because they represent distinct
 *     accounting concepts.  Either non-zero returns `tecHAS_OBLIGATIONS`.
 *     Destroying while holders exist would orphan their `MPToken` SLEs,
 *     corrupting the ledger.  The `sfLockedAmount` branch is `LCOV_EXCL_LINE`
 *     because reaching it with zero `sfOutstandingAmount` would indicate
 *     already-corrupted ledger state not reachable through normal flow.
 *
 *  @param ctx  Preclaim context providing the read-only ledger view.
 *  @return `tesSUCCESS` if all guards pass; `tecOBJECT_NOT_FOUND`,
 *      `tecNO_PERMISSION`, or `tecHAS_OBLIGATIONS` otherwise.
 */
TER
MPTokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    auto const sleMPT = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMPT)
        return tecOBJECT_NOT_FOUND;

    if ((*sleMPT)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if ((*sleMPT)[sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    if ((*sleMPT)[~sfLockedAmount].value_or(0) != 0)
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

/** Apply the destruction: remove the issuance SLE and unwind its accounting.
 *
 *  Executes the symmetric inverse of `MPTokenIssuanceCreate::create()` in
 *  four steps:
 *
 *  1. **Defensive re-verification** — re-checks that `account_` equals the
 *     SLE's `sfIssuer`.  This guard is redundant with `preclaim` by design:
 *     `doApply` runs against a mutable view that is logically distinct from
 *     the read-only snapshot used by `preclaim`, and the framework could, in
 *     theory, present a different ledger state here.  `tecINTERNAL` on
 *     mismatch signals a framework-level bug, not user error; the branch is
 *     `LCOV_EXCL_LINE` because it cannot be reached through correct
 *     transaction processing.
 *
 *  2. **Owner-directory removal** — calls `view().dirRemove(keylet::ownerDir(account_),
 *     (*mpt)[sfOwnerNode], mpt->key(), false)`.  The `sfOwnerNode` back-pointer
 *     stored in the SLE at creation time gives the exact directory page, making
 *     this an O(1) lookup rather than a linear scan.  The `false` argument
 *     preserves the owner directory itself even if it becomes empty.  Failure
 *     means the directory entry was never recorded or has already been removed —
 *     ledger corruption — so `tefBAD_LEDGER` is returned (also `LCOV_EXCL_LINE`).
 *
 *  3. **SLE erasure** — `view().erase(mpt)` removes the `MPTokenIssuance`
 *     object from the ledger.  This is safe only after the directory entry is
 *     gone; erasing first would lose the `sfOwnerNode` page index needed for
 *     step 2.
 *
 *  4. **Owner-count adjustment** — `adjustOwnerCount(..., -1, j_)` decrements
 *     the issuer's reserve-tracked owner count, freeing the one reserve slot
 *     that `MPTokenIssuanceCreate` claimed.
 *
 *  All four mutations are applied to the `ApplyContext` view and are committed
 *  atomically if `tesSUCCESS` is returned, or discarded entirely on any non-`tes`
 *  result per the standard transactor rollback contract.
 *
 *  @return `tesSUCCESS` on success; `tecINTERNAL` if the issuer mismatch guard
 *      fires (framework bug); `tefBAD_LEDGER` if the owner-directory entry is
 *      missing (ledger corruption).
 */
TER
MPTokenIssuanceDestroy::doApply()
{
    auto const mpt = view().peek(keylet::mptIssuance(ctx_.tx[sfMPTokenIssuanceID]));
    if (account_ != mpt->getAccountID(sfIssuer))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(keylet::ownerDir(account_), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view().erase(mpt);

    adjustOwnerCount(view(), view().peek(keylet::account(account_)), -1, j_);

    return tesSUCCESS;
}

/** No-op stub satisfying the invariant-visitor interface.
 *
 *  No transaction-specific per-entry invariants are defined for
 *  `MPTokenIssuanceDestroy` yet.  Global invariant checkers (e.g.,
 *  `ValidMPTIssuance`, `NoZeroEscrow`) cover the relevant ledger
 *  state transitions without needing a per-transactor hook here.
 *  This method is the designated extension point for any future
 *  per-transaction invariants.
 */
void
MPTokenIssuanceDestroy::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** No-op stub satisfying the invariant-finalizer interface.
 *
 *  Always returns `true` because no transaction-specific finalization
 *  invariants exist for `MPTokenIssuanceDestroy`.  The global invariant
 *  framework (e.g., `ValidMPTIssuance`, `XRPNotCreated`) handles all
 *  post-apply consistency checks for this transaction type.  Reserved
 *  for future per-transaction invariants.
 *
 *  @return `true` always — no invariants to fail.
 */
bool
MPTokenIssuanceDestroy::finalizeInvariants(
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
