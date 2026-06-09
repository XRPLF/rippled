#include <xrpl/tx/transactors/vault/VaultDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
VaultDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "VaultDelete: zero/empty vault ID.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
VaultDelete::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    if (vault->at(sfOwner) != ctx.tx[sfAccount])
    {
        JLOG(ctx.j.debug()) << "VaultDelete: account is not an owner.";
        return tecNO_PERMISSION;
    }

    if (vault->at(sfAssetsAvailable) != 0)
    {
        JLOG(ctx.j.debug()) << "VaultDelete: nonzero assets available.";
        return tecHAS_OBLIGATIONS;
    }

    if (vault->at(sfAssetsTotal) != 0)
    {
        JLOG(ctx.j.debug()) << "VaultDelete: nonzero assets total.";
        return tecHAS_OBLIGATIONS;
    }

    // Verify we can destroy MPTokenIssuance
    auto const sleMPT = ctx.view.read(keylet::mptIssuance(vault->at(sfShareMPTID)));

    if (!sleMPT)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDelete: missing issuance of vault shares.";
        return tecOBJECT_NOT_FOUND;
        // LCOV_EXCL_STOP
    }

    if (sleMPT->at(sfIssuer) != vault->getAccountID(sfAccount))
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDelete: invalid owner of vault shares.";
        return tecNO_PERMISSION;
        // LCOV_EXCL_STOP
    }

    if (sleMPT->at(sfOutstandingAmount) != 0)
    {
        JLOG(ctx.j.debug()) << "VaultDelete: nonzero outstanding shares.";
        return tecHAS_OBLIGATIONS;
    }

    return tesSUCCESS;
}

TER
VaultDelete::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Destroy the asset holding.
    auto asset = vault->at(sfAsset);

    if (auto ter = removeEmptyHolding(view(), vault->at(sfAccount), asset, j_); !isTesSuccess(ter))
        return ter;

    auto const& pseudoID = vault->at(sfAccount);
    auto const pseudoAcct = view().peek(keylet::account(pseudoID));
    if (!pseudoAcct)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: missing vault pseudo-account.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Destroy the share issuance. Do not use MPTokenIssuanceDestroy for this,
    // no special logic needed. First run few checks, duplicated from preclaim.
    auto const shareMPTID = *vault->at(sfShareMPTID);
    auto const mpt = view().peek(keylet::mptIssuance(shareMPTID));
    if (!mpt)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Try to remove MPToken for vault shares for the vault owner if it exists.
    if (auto const mptoken = view().peek(keylet::mptoken(shareMPTID, accountID_)))
    {
        if (auto const ter = removeEmptyHolding(view(), accountID_, MPTIssue(shareMPTID), j_);
            !isTesSuccess(ter))
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultDelete: failed to remove vault owner's MPToken"
                << " MPTID=" << to_string(shareMPTID)   //
                << " account=" << toBase58(accountID_)  //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
    }

    if (!view().dirRemove(keylet::ownerDir(pseudoID), (*mpt)[sfOwnerNode], mpt->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: failed to delete issuance object.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    adjustOwnerCount(view(), pseudoAcct, -1, j_);

    view().erase(mpt);

    // The pseudo-account's directory should have been deleted already.
    if (view().peek(keylet::ownerDir(pseudoID)))
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    // Destroy the pseudo-account.
    auto vaultPseudoSLE = view().peek(keylet::account(pseudoID));
    if (!vaultPseudoSLE || vaultPseudoSLE->at(~sfVaultID) != vault->key())
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Making the payment and removing the empty holding should have deleted any
    // obligations associated with the vault or vault pseudo-account.
    if (*vaultPseudoSLE->at(sfBalance))
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: pseudo-account has a balance";
        return tecHAS_OBLIGATIONS;
        // LCOV_EXCL_STOP
    }
    if (vaultPseudoSLE->at(sfOwnerCount) != 0)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: pseudo-account still owns objects";
        return tecHAS_OBLIGATIONS;
        // LCOV_EXCL_STOP
    }
    if (view().exists(keylet::ownerDir(pseudoID)))
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: pseudo-account has a directory";
        return tecHAS_OBLIGATIONS;
        // LCOV_EXCL_STOP
    }

    view().erase(vaultPseudoSLE);

    // Remove the vault from its owner's directory.
    auto const ownerID = vault->at(sfOwner);
    if (!view().dirRemove(keylet::ownerDir(ownerID), vault->at(sfOwnerNode), vault->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: failed to delete vault object.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const owner = view().peek(keylet::account(ownerID));
    if (!owner)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: missing vault owner account.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // We are destroying Vault and PseudoAccount, hence decrease by 2
    adjustOwnerCount(view(), owner, -2, j_);

    // Destroy the vault.
    view().erase(vault);

    return tesSUCCESS;
}

void
VaultDelete::visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    data_.visitEntry(isDelete, before, after);
}

bool
VaultDelete::finalizeInvariants(
    STTx const&,
    TER result,
    XRPAmount,
    ReadView const& view,
    beast::Journal const& j)
{
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(result))
        return true;

    static constexpr Number kZero{};

    // VaultDelete must have deleted the vault; if it still exists, something
    // went wrong.
    if (!data_.afterVaults().empty())
    {
        JLOG(j.fatal()) << "Invariant failed: vault not deleted by VaultDelete";
        XRPL_ASSERT(enforce, "xrpl::VaultDelete::finalizeInvariants : vault not deleted invariant");
        return !enforce;
    }

    // Nothing to check if the vault was never in the before-set (e.g. a
    // test that omits the vault entry entirely).
    if (data_.beforeVaults().empty())
        return true;

    auto const& beforeVault = data_.beforeVaults()[0];

    auto const deletedShares = data_.findDeletedShares(beforeVault.shareMPTID);
    if (!deletedShares)
    {
        JLOG(j.fatal()) << "Invariant failed: deleted vault must also delete shares";
        XRPL_ASSERT(enforce, "xrpl::VaultDelete::finalizeInvariants : shares deletion invariant");
        return !enforce;
    }

    bool result2 = true;

    if (deletedShares->sharesTotal != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: deleted vault must have no shares outstanding";
        result2 = false;
    }

    if (beforeVault.assetsTotal != kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: deleted vault must have no assets outstanding";
        result2 = false;
    }

    if (beforeVault.assetsAvailable != kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: deleted vault must have no assets available";
        result2 = false;
    }

    if (!result2)
    {
        XRPL_ASSERT(enforce, "xrpl::VaultDelete::finalizeInvariants : vault delete invariants");
        return !enforce;
    }

    return true;
}

}  // namespace xrpl
