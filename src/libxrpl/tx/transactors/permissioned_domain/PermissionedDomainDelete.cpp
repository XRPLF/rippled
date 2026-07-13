#include <xrpl/tx/transactors/permissioned_domain/PermissionedDomainDelete.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

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
    PermissionedDomainEntry<ReadView> const sleDomain{keylet::permissionedDomain(domain), ctx.view};

    if (!sleDomain)
        return tecNO_ENTRY;

    XRPL_ASSERT(
        sleDomain->isFieldPresent(sfOwner) && ctx.tx.isFieldPresent(sfAccount),
        "xrpl::PermissionedDomainDelete::preclaim : required fields present");
    if (sleDomain->getAccountID(sfOwner) != ctx.tx.getAccountID(sfAccount))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/**
 * Attempt to delete the Permissioned Domain.
 */
TER
PermissionedDomainDelete::doApply()
{
    XRPL_ASSERT(
        ctx_.tx.isFieldPresent(sfDomainID),
        "xrpl::PermissionedDomainDelete::doApply : required field present");

    PermissionedDomainEntry<ApplyView> slePd{
        keylet::permissionedDomain(ctx_.tx.at(sfDomainID)), view()};

    return slePd.destroy();
}

void
PermissionedDomainDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
