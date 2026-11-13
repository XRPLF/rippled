#include <xrpl/tx/transactors/delegate/DelegateSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/entries/DelegateEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
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
        return tecPSEUDO_ACCOUNT;

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
    auto const sleOwner = ctx_.view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& authAccount = ctx_.tx[sfAuthorize];
    DelegateEntryW entry(accountID_, authAccount, ctx_.view(), j_);

    if (entry.exists())
    {
        auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
        if (permissions.empty())
        {
            // if permissions array is empty, delete the ledger object.
            return entry.removeFromLedger(accountID_);
        }

        entry->setFieldArray(sfPermissions, permissions);
        entry.update();
        return tesSUCCESS;
    }

    auto const& permissions = ctx_.tx.getFieldArray(sfPermissions);
    if (permissions.empty())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(),
            sleOwner,
            preFeeBalance_,
            {.ownerCountDelta = 1},
            ctx_.journal);
        !isTesSuccess(ret))
        return ret;

    entry.newSLE();
    entry->setAccountID(sfAccount, accountID_);
    entry->setAccountID(sfAuthorize, authAccount);

    entry->setFieldArray(sfPermissions, permissions);

    // Add to delegating account's owner directory
    auto const page = ctx_.view().dirInsert(
        keylet::ownerDir(accountID_), entry.keylet(), describeOwnerDir(accountID_));

    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    (*entry)[sfOwnerNode] = *page;

    // Add to authorized account's owner directory so AccountDelete can find
    // and clean up inbound delegations when the authorized account is deleted.
    auto const destPage = ctx_.view().dirInsert(
        keylet::ownerDir(authAccount), entry.keylet(), describeOwnerDir(authAccount));

    if (!destPage)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    (*entry)[sfDestinationNode] = *destPage;

    entry.insert();
    increaseOwnerCount(ctx_.getApplyViewContext(), sleOwner, 1, ctx_.journal);
    addSponsorToLedgerEntry(ctx_.getApplyViewContext(), entry.mutableRawSle());

    return tesSUCCESS;
}

void
DelegateSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
