#include <xrpl/tx/transactors/vault/VaultDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
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

    if (ctx.tx.isFieldPresent(sfMemoData) && !ctx.rules.enabled(featureLendingProtocolV1_1))
        return temDISABLED;

    if (!validDataLength(ctx.tx[~sfMemoData], kMaxDataPayloadLength))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
VaultDelete::preclaim(PreclaimContext const& ctx)
{
    VaultEntry<ReadView> const vault{keylet::vault(ctx.tx[sfVaultID]), ctx.view};
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
    MPTokenIssuanceEntry<ReadView> const sleMPT{
        keylet::mptokenIssuance(vault->at(sfShareMPTID)), ctx.view};

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
    VaultEntry<ApplyView> vault{keylet::vault(ctx_.tx[sfVaultID]), view()};
    auto applyViewContext = ctx_.getApplyViewContext();
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Destroy the asset holding.
    auto asset = vault->at(sfAsset);

    if (auto ter = removeEmptyHolding(applyViewContext, vault->at(sfAccount), asset, j_);
        !isTesSuccess(ter))
        return ter;

    auto const& pseudoID = vault->at(sfAccount);
    AccountRootEntry<ApplyView> const pseudoAcct{keylet::account(pseudoID), view()};
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
    MPTokenIssuanceEntry<ApplyView> mpt{keylet::mptokenIssuance(shareMPTID), view()};
    if (!mpt)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDelete: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Try to remove MPToken for vault shares for the vault owner if it exists.
    if (MPTokenEntry<ApplyView> const mptoken{keylet::mptoken(shareMPTID, accountID_), view()})
    {
        if (auto const ter =
                removeEmptyHolding(applyViewContext, accountID_, MPTIssue(shareMPTID), j_);
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
    decreaseOwnerCountForObject(view(), pseudoAcct.mutableSle(), mpt.mutableSle(), 1, j_);

    mpt.erase();

    // The pseudo-account's directory should have been deleted already.
    if (DirectoryNodeEntry<ApplyView>{keylet::ownerDir(pseudoID), view()})
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    // Destroy the pseudo-account.
    AccountRootEntry<ApplyView> vaultPseudoSLE{keylet::account(pseudoID), view()};
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

    vaultPseudoSLE.erase();

    // Unlink the vault from its owner's directory, decrement the owner's
    // OwnerCount by 2 (the vault and its now-destroyed pseudo-account, refunding
    // any reserve sponsor), and erase it. See VaultEntry.
    return vault.destroy();
}

void
VaultDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultDelete::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
