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

NotTEC
PermissionedDomainDelete::preflight(PreflightContext const& ctx)
{
    auto const domain = ctx.tx.getFieldH256(sfDomainID);
    if (domain == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

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

/** Attempt to delete the Permissioned Domain. */
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

void
PermissionedDomainDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

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
