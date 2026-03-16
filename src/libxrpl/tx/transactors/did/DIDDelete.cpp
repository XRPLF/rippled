#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/transactors/did/DIDDelete.h>

namespace xrpl {

NotTEC
DIDDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
DIDDelete::deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner)
{
    auto const sle = ctx.view().peek(sleKeylet);
    if (!sle)
        return tecNO_ENTRY;

    return DIDDelete::deleteSLE(ctx.view(), sle, owner, ctx.journal);
}

TER
DIDDelete::deleteSLE(
    ApplyView& view,
    std::shared_ptr<SLE> sle,
    AccountID const owner,
    beast::Journal j)
{
    // Remove object from owner directory
    if (!view.dirRemove(keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete DID Token from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    adjustOwnerCount(view, sleOwner, -1, j);
    view.update(sleOwner);

    // Remove object from ledger
    view.erase(sle);
    return tesSUCCESS;
}

TER
DIDDelete::doApply()
{
    return deleteSLE(ctx_, keylet::did(account_), account_);
}

}  // namespace xrpl
