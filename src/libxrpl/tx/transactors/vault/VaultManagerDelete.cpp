#include <xrpl/tx/transactors/vault/VaultManagerDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
VaultManagerDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
VaultManagerDelete::preclaim(PreclaimContext const& ctx)
{
    auto const vaultID = ctx.tx[sfVaultID];
    auto const manager = ctx.view.read(keylet::vaultManager(vaultID));
    if (!manager)
        return tecNO_ENTRY;
    auto const vault = ctx.view.read(keylet::vault(vaultID));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (ctx.tx[sfAccount] != vault->at(sfOwner))
        return tecNO_PERMISSION;

    if (vault->at(sfManagedPositionCount) != 0)
    {
        JLOG(ctx.j.debug()) << "VaultManagerDelete: positions remain open.";
        return tecHAS_OBLIGATIONS;
    }

    if (manager->at(sfCoverAvailable) != beast::kZero)
    {
        JLOG(ctx.j.debug()) << "VaultManagerDelete: first-loss cover remains.";
        return tecHAS_OBLIGATIONS;
    }

    return tesSUCCESS;
}

TER
VaultManagerDelete::doApply()
{
    auto const vaultID = ctx_.tx[sfVaultID];
    auto manager = view().peek(keylet::vaultManager(vaultID));
    auto const vault = view().read(keylet::vault(vaultID));
    if (!manager || !vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    Asset const vaultAsset = vault->at(sfAsset);
    AccountID const pseudoId = manager->at(sfAccount);

    if (!view().dirRemove(
            keylet::ownerDir(accountID_), manager->at(sfOwnerNode), manager->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (auto const ter = removeEmptyHolding(ctx_.getApplyViewContext(), pseudoId, vaultAsset, j_))
        return ter;

    auto pseudo = view().peek(keylet::account(pseudoId));
    if (!pseudo)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    // Removing the empty holding leaves the cover pseudo-account with nothing.
    if (*pseudo->at(sfBalance) || pseudo->at(sfOwnerCount) != 0 ||
        view().read(keylet::ownerDir(pseudoId)))
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE
    view().erase(pseudo);

    auto owner = view().peek(keylet::account(accountID_));
    if (!owner)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    // One increment for the entry and one for its pseudo-account.
    decreaseOwnerCountForObject(view(), owner, manager, 2, j_);
    view().erase(manager);
    return tesSUCCESS;
}

void
VaultManagerDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
