/** @file
 *  Implementation of the `OracleDelete` transactor (XLS-47d).
 *
 *  Removes a Price Oracle (`ltORACLE`) ledger object, reverting the owner
 *  reserve that was allocated when the oracle was created by `OracleSet`.
 *  The `deleteOracle` static helper is also the entry point used by
 *  `AccountDelete` when sweeping owned objects during account removal.
 */
#include <xrpl/tx/transactors/oracle/OracleDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/** Unconditionally succeeds.
 *
 *  A delete request has no stateless properties to validate — there are no
 *  field ranges, array size limits, or flag combinations that can be checked
 *  without ledger state. All meaningful validation is deferred to `preclaim`.
 */
NotTEC
OracleDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

/** Validates that the target oracle exists and is owned by the submitter.
 *
 *  The oracle keylet is derived from `(account, sfOracleDocumentID)`, so a
 *  successful read at that key is already proof of ownership; the explicit
 *  `sfOwner` comparison that follows is a defensive invariant and is excluded
 *  from coverage metrics (`LCOV_EXCL`).
 */
TER
OracleDelete::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfAccount))))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    auto const sle =
        ctx.view.read(keylet::oracle(ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID]));
    if (!sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }

    if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        // Unreachable: the oracle keylet embeds the account ID, so a
        // successful read above is sufficient proof of ownership.
        // LCOV_EXCL_START
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    return tesSUCCESS;
}

/** Remove the oracle SLE from the ledger and reclaim the owner reserve.
 *
 *  Deletion must follow a strict three-step order:
 *
 *  1. `dirRemove` — reads `sfOwnerNode` (the directory page index stored by
 *     `OracleSet::doApply` at creation time) from the live SLE. Erasing the
 *     SLE first would lose this pointer, making O(1) directory removal
 *     impossible. Failure here returns `tefBAD_LEDGER`, indicating ledger
 *     corruption rather than a user error.
 *
 *  2. `adjustOwnerCount` — decrements the owner reserve by `-2` if the oracle
 *     currently holds more than five `sfPriceDataSeries` entries, or by `-1`
 *     otherwise. This mirrors the creation logic in `OracleSet::doApply` and
 *     ensures the returned reserve equals the reserve originally taken.
 *     The threshold is read from the live SLE rather than the transaction to
 *     keep it immune to a caller passing a mismatched count.
 *
 *  3. `view.erase` — removes the SLE. After this call the SLE pointer is
 *     invalid; no further reads from it are safe.
 */
TER
OracleDelete::deleteOracle(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Oracle from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Mirror the +1/+2 increment in OracleSet::doApply: oracles with more than
    // 5 price data series occupy two reserve slots, all others occupy one.
    auto const count = sle->getFieldArray(sfPriceDataSeries).size() > 5 ? -2 : -1;

    adjustOwnerCount(view, sleOwner, count, j);

    view.erase(sle);

    return tesSUCCESS;
}

/** Fetches the oracle SLE and delegates to `deleteOracle`.
 *
 *  The `peek` returning null is unreachable under normal conditions because
 *  `preclaim` already confirmed the oracle exists. The `tecINTERNAL` sentinel
 *  is a fault-detection guard excluded from coverage metrics (`LCOV_EXCL`).
 */
TER
OracleDelete::doApply()
{
    if (auto sle = ctx_.view().peek(keylet::oracle(account_, ctx_.tx[sfOracleDocumentID])))
        return deleteOracle(ctx_.view(), sle, account_, j_);

    return tecINTERNAL;  // LCOV_EXCL_LINE
}

void
OracleDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
OracleDelete::finalizeInvariants(
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
