/** @file
 *  Implements `CheckCancel`, the teardown transactor for XRPL Check objects.
 *
 *  Removes a Check SLE from the ledger without transferring value, releasing
 *  the source account's owner reserve. See `CheckCancel.h` for the full
 *  public interface and permission model.
 */

#include <xrpl/tx/transactors/check/CheckCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/** Returns `tesSUCCESS` unconditionally.
 *
 *  Every meaningful validation for a cancel — whether the Check exists,
 *  whether it has expired, and whether the submitter is authorized — depends
 *  on ledger state that is only available in `preclaim`. There are no
 *  purely structural properties of the `CheckCancel` transaction itself that
 *  can be rejected without a ledger read, so this phase is intentionally
 *  empty. Contrast with `CheckCreate::preflight`, which validates self-sends
 *  and amount sanity without any ledger access.
 */
NotTEC
CheckCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

/** Validates cancellation eligibility against the current ledger state.
 *
 *  Two authorization regimes apply:
 *  - **Unexpired check**: only the original creator (`sfAccount` on the Check
 *    SLE) or the designated destination may cancel.
 *  - **Expired check**: any account may cancel, allowing third-party sweeping
 *    of stale obligations from the ledger.
 *
 *  Expiration is compared against the **parent** ledger's close time, not the
 *  ledger currently being constructed. The closing time of the in-progress
 *  ledger is not finalized at apply time, so using it would produce divergent
 *  results across validators. The parent ledger's close time is
 *  consensus-agreed and immutable, making this check fully deterministic.
 *
 *  @note Unauthorized cancellation returns `tecNO_PERMISSION` — the fee is
 *      still charged even though the check is not removed.
 */
TER
CheckCancel::preclaim(PreclaimContext const& ctx)
{
    auto const sleCheck = ctx.view.read(keylet::check(ctx.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(ctx.j.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    if (!hasExpired(ctx.view, (*sleCheck)[~sfExpiration]))
    {
        AccountID const acctId{ctx.tx[sfAccount]};
        if (acctId != (*sleCheck)[sfAccount] && acctId != (*sleCheck)[sfDestination])
        {
            JLOG(ctx.j.warn()) << "Check is not expired and canceler is "
                                  "neither check source nor destination.";
            return tecNO_PERMISSION;
        }
    }
    return tesSUCCESS;
}

/** Removes the Check SLE and all associated ledger state.
 *
 *  Performs the inverse of `CheckCreate::doApply` in three sequential steps:
 *
 *  1. **Directory removal** — uses the cached `sfDestinationNode` and
 *     `sfOwnerNode` page indices written by `CheckCreate` for O(1) removal
 *     from both owner directories. The `srcId != dstId` guard skips the
 *     destination directory for self-checks; `CheckCreate` rejects
 *     self-sends (`temREDUNDANT`), so this branch is a defensive invariant
 *     guard rather than a live path.
 *  2. **Reserve release** — decrements the source account's `sfOwnerCount`
 *     by 1 via `adjustOwnerCount`, returning the reserve locked at creation.
 *     Only the source account bears a reserve obligation; the destination
 *     holds a directory reference only.
 *  3. **SLE erasure** — removes the Check object from the ledger.
 *
 *  The `dirRemove` failure branches (`tefBAD_LEDGER`) are unreachable in a
 *  well-formed ledger and are excluded from coverage requirements
 *  (`LCOV_EXCL`). The `sleCheck` existence guard is a defensive backstop
 *  against any theoretical divergence between the `preclaim` read-only view
 *  and the mutable `doApply` view; the framework prevents such divergence
 *  in practice.
 */
TER
CheckCancel::doApply()
{
    auto const sleCheck = view().peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(j_.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    AccountID const dstId{sleCheck->getAccountID(sfDestination)};
    auto viewJ = ctx_.registry.get().getJournal("View");

    if (srcId != dstId)
    {
        std::uint64_t const page{(*sleCheck)[sfDestinationNode]};
        if (!view().dirRemove(keylet::ownerDir(dstId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete check from destination.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }
    {
        std::uint64_t const page{(*sleCheck)[sfOwnerNode]};
        if (!view().dirRemove(keylet::ownerDir(srcId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete check from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    auto const sleSrc = view().peek(keylet::account(srcId));
    adjustOwnerCount(view(), sleSrc, -1, viewJ);

    view().erase(sleCheck);
    return tesSUCCESS;
}

void
CheckCancel::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
CheckCancel::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
