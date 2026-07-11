#include <xrpl/tx/transactors/delegate/DelegateSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

namespace xrpl {

NotTEC
DelegateSet::preflight(PreflightContext const& ctx)
{
    auto const& permissions = ctx.tx.getFieldArray(sfPermissions);
    if (permissions.size() > kPermissionMaxSize)
        return temARRAY_TOO_LARGE;

    // can not authorize self
    if (ctx.tx[sfAccount] == ctx.tx[sfAuthorize])
        return temMALFORMED;

    std::unordered_set<std::uint32_t> permissionSet;

    for (auto const& permission : permissions)
    {
        if (!permissionSet.insert(permission[sfPermissionValue]).second)
            return temMALFORMED;

        if (!Permission::getInstance().isDelegable(permission[sfPermissionValue], ctx.rules))
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
DelegateSet::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx[sfAccount])))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    auto const sleAuthorize = ctx.view.read(keylet::account(ctx.tx[sfAuthorize]));
    if (!sleAuthorize)
        return tecNO_TARGET;

    if (isPseudoAccount(sleAuthorize))
        return tecNO_PERMISSION;

    // Deleting the delegate object is invalid if it doesn’t exist.
    if (ctx.tx.getFieldArray(sfPermissions).empty() &&
        !ctx.view.exists(keylet::delegate(ctx.tx[sfAccount], ctx.tx[sfAuthorize])))
    {
        return tecNO_ENTRY;
    }

    return tesSUCCESS;
}

TER
DelegateSet::doApply()
{
    AccountRootEntry<ApplyView> sleOwner{keylet::account(accountID_), ctx_.view()};
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& authAccount = ctx_.tx[sfAuthorize];
    auto const delegateKey = keylet::delegate(accountID_, authAccount);

    // Build with the ApplyViewContext so create() honors reserve sponsorship.
    DelegateEntry<ApplyView> sle{delegateKey, ctx_.getApplyViewContext()};
    if (sle)
    {
        auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
        if (permissions.empty())
        {
            // if permissions array is empty, delete the ledger object.
            return deleteDelegate(view(), sle.mutableSle(), j_);
        }

        sle->setFieldArray(sfPermissions, permissions);
        sle.update();
        return tesSUCCESS;
    }

    auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
    if (permissions.empty())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    sle.newSLE();
    sle->setAccountID(sfAccount, accountID_);
    sle->setAccountID(sfAuthorize, authAccount);
    sle->setFieldArray(sfPermissions, permissions);

    // Reserve check (delegator's pre-fee balance, honoring any reserve sponsor)
    // + link into the delegator's owner directory and the authorized account's
    // directory + bump the delegator's OwnerCount + stamp the reserve sponsor +
    // insert. See DelegateEntry::ownerDirs() and SLEBase::create().
    return sle.create(preFeeBalance_);
}

TER
DelegateSet::deleteDelegate(ApplyView& view, SLE::ref sle, beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Unlink from the delegator's owner directory and the authorized account's
    // directory, decrement the delegator's OwnerCount (refunding a reserve
    // sponsor if present), and erase. Only the delegator's count was bumped on
    // creation. See DelegateEntry::ownerDirs().
    DelegateEntry<ApplyView> delegate{sle, view, j};
    return delegate.destroy();
}

void
DelegateSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
DelegateSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
