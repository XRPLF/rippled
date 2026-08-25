#include <xrpl/tx/transactors/vault/VaultDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
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
        return {temMALFORMED, "VaultDelete: zero/empty vault ID."};
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
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    if (vault->at(sfOwner) != ctx.tx[sfAccount])
    {
        return {tecNO_PERMISSION, "VaultDelete: account is not an owner."};
    }

    if (vault->at(sfAssetsAvailable) != 0)
    {
        return {tecHAS_OBLIGATIONS, "VaultDelete: nonzero assets available."};
    }

    if (vault->at(sfAssetsTotal) != 0)
    {
        return {tecHAS_OBLIGATIONS, "VaultDelete: nonzero assets total."};
    }

    // Verify we can destroy MPTokenIssuance
    auto const sleMPT = ctx.view.read(keylet::mptokenIssuance(vault->at(sfShareMPTID)));

    if (!sleMPT)
    {
        // LCOV_EXCL_START
        return {tecOBJECT_NOT_FOUND, "VaultDelete: missing issuance of vault shares."};
        // LCOV_EXCL_STOP
    }

    if (sleMPT->at(sfIssuer) != vault->getAccountID(sfAccount))
    {
        // LCOV_EXCL_START
        return {tecNO_PERMISSION, "VaultDelete: invalid owner of vault shares."};
        // LCOV_EXCL_STOP
    }

    if (sleMPT->at(sfOutstandingAmount) != 0)
    {
        return {tecHAS_OBLIGATIONS, "VaultDelete: nonzero outstanding shares."};
    }

    return tesSUCCESS;
}

TER
VaultDelete::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    auto applyViewContext = ctx_.getApplyViewContext();
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Remove any credentials pinned to the vault pseudo-account before anything
    // else. They would otherwise keep its owner directory alive and block
    // deletion with tecHAS_OBLIGATIONS. Doing it first means a bounded,
    // tecINCOMPLETE cleanup can be resumed by a later transaction without having
    // already torn down the vault.
    if (view().rules().enabled(fixCleanup3_4_0))
    {
        if (auto const ter = credentials::deletePseudoAccountCredentials(
                view(), vault->at(sfAccount), kMaxDeletablePseudoAccountCredentials, j_);
            !isTesSuccess(ter))
            return ter;
    }

    // Destroy the asset holding.
    auto asset = vault->at(sfAsset);

    if (auto ter = removeEmptyHolding(applyViewContext, vault->at(sfAccount), asset, j_);
        !isTesSuccess(ter))
        return ter;

    auto const& pseudoID = vault->at(sfAccount);
    auto const pseudoAcct = view().peek(keylet::account(pseudoID));
    if (!pseudoAcct)
    {
        // LCOV_EXCL_START
        return {tefBAD_LEDGER, "VaultDelete: missing vault pseudo-account."};
        // LCOV_EXCL_STOP
    }

    // Destroy the share issuance. Do not use MPTokenIssuanceDestroy for this,
    // no special logic needed. First run few checks, duplicated from preclaim.
    auto const shareMPTID = *vault->at(sfShareMPTID);
    auto const mpt = view().peek(keylet::mptokenIssuance(shareMPTID));
    if (!mpt)
    {
        // LCOV_EXCL_START
        return {tefINTERNAL, "VaultDelete: missing issuance of vault shares."};
        // LCOV_EXCL_STOP
    }

    // Try to remove MPToken for vault shares for the vault owner if it exists.
    if (auto const mptoken = view().peek(keylet::mptoken(shareMPTID, accountID_)))
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
        return {tefBAD_LEDGER, "VaultDelete: failed to delete issuance object."};
        // LCOV_EXCL_STOP
    }
    decreaseOwnerCountForObject(view(), pseudoAcct, mpt, 1, j_);

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
        return {tecHAS_OBLIGATIONS, "VaultDelete: pseudo-account has a balance"};
        // LCOV_EXCL_STOP
    }
    if (vaultPseudoSLE->at(sfOwnerCount) != 0)
    {
        // LCOV_EXCL_START
        return {tecHAS_OBLIGATIONS, "VaultDelete: pseudo-account still owns objects"};
        // LCOV_EXCL_STOP
    }
    if (view().exists(keylet::ownerDir(pseudoID)))
    {
        // LCOV_EXCL_START
        return {tecHAS_OBLIGATIONS, "VaultDelete: pseudo-account has a directory"};
        // LCOV_EXCL_STOP
    }

    view().erase(vaultPseudoSLE);

    // Remove the vault from its owner's directory.
    auto const ownerID = vault->at(sfOwner);
    if (!view().dirRemove(keylet::ownerDir(ownerID), vault->at(sfOwnerNode), vault->key(), false))
    {
        // LCOV_EXCL_START
        return {tefBAD_LEDGER, "VaultDelete: failed to delete vault object."};
        // LCOV_EXCL_STOP
    }

    auto const owner = view().peek(keylet::account(ownerID));
    if (!owner)
    {
        // LCOV_EXCL_START
        return {tefBAD_LEDGER, "VaultDelete: missing vault owner account."};
        // LCOV_EXCL_STOP
    }

    // We are destroying Vault and PseudoAccount, hence decrease by 2
    decreaseOwnerCountForObject(view(), owner, vault, 2, j_);

    // Destroy the vault.
    view().erase(vault);

    return tesSUCCESS;
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
