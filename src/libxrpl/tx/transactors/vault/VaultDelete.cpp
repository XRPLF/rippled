#include <xrpl/tx/transactors/vault/VaultDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

NotTEC
VaultDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "VaultDelete: zero/empty vault ID.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

/** Enforces pre-deletion invariants against the live ledger.
 *
 *  `sfAssetsAvailable` and `sfAssetsTotal` are checked independently because
 *  a vault carrying unrealized losses (e.g. defaulted loans) may have
 *  `sfAssetsTotal` > `sfAssetsAvailable`; both must reach zero before the
 *  vault can be destroyed.  Checking only one would allow deletion while the
 *  other still records outstanding obligations.
 *
 *  The two `MPTokenIssuance` guards — existence of the share issuance SLE and
 *  the issuer-match check — are wrapped in `LCOV_EXCL_START` because they
 *  defend against ledger state that cannot arise from valid transaction
 *  sequences.  They are reachable only if earlier transactions have already
 *  corrupted the ledger; their presence prevents silent destruction of a
 *  partially dismantled vault cluster.
 *
 *  All user-correctable failures (`tecNO_PERMISSION`, `tecHAS_OBLIGATIONS`,
 *  `tecNO_ENTRY`) consume the transaction fee, signalling that the submitter
 *  must resolve the issue before resubmitting.
 */
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

/** Dismantles the vault cluster in strict dependency order.
 *
 *  The five-step sequence must not be reordered: each step removes an object
 *  that a later step depends on having already cleaned up.
 *
 *  **Step 1 — Asset holding removal.**
 *  `removeEmptyHolding` erases the trust line (`RippleState`) or `MPToken`
 *  that the pseudo-account used to hold the underlying asset.  The holding is
 *  guaranteed empty by `preclaim`'s `sfAssetsTotal == 0` check; the call also
 *  removes the object from the pseudo-account's owner directory and
 *  decrements its owner count.
 *
 *  **Step 2 — Vault owner's share MPToken removal.**
 *  If the vault creator holds an `MPToken` for the share issuance
 *  (`keylet::mptoken(shareMPTID, account_)`), a second `removeEmptyHolding`
 *  call cleans it up.  This is conditioned on the token's existence; the
 *  vault owner is not required to hold shares.  The `LCOV_EXCL` guard covers
 *  the failure branch, which is unreachable in valid ledger state because
 *  `preclaim` has already verified `sfOutstandingAmount == 0`.
 *
 *  **Step 3 — Share issuance removal.**
 *  The `MPTokenIssuance` SLE is removed directly rather than via
 *  `MPTokenIssuanceDestroy` because that transactor carries fee and
 *  amendment logic irrelevant here.  `dirRemove` uses the cached
 *  `sfOwnerNode` from the issuance SLE for O(1) directory removal, then
 *  `adjustOwnerCount(view(), pseudoAcct, -1, j_)` decrements the
 *  pseudo-account's count before the SLE is erased.
 *
 *  **Step 4 — Pseudo-account cleanup verification and erasure.**
 *  After Steps 1–3, the pseudo-account's owner directory must be empty.
 *  The `view().peek(keylet::ownerDir(pseudoID))` guard is the one
 *  `tec` code emitted from `doApply` (vs. `tef` codes for true corruption),
 *  marked `LCOV_EXCL_LINE` because it is a forward-safety valve: a future
 *  ledger feature could attach additional objects to the pseudo-account's
 *  directory, and this guard prevents silently destroying a pseudo-account
 *  that still owns unhandled objects.  The subsequent balance and owner-count
 *  checks are `LCOV_EXCL`-guarded corruption sentinels; if reached, they
 *  return `tecHAS_OBLIGATIONS` rather than `tef` to let the invariant checker
 *  log diagnostics before fee collection.
 *
 *  **Step 5 — Vault SLE removal and owner-count adjustment.**
 *  The vault is removed from the real owner's `ownerDir` via `dirRemove`,
 *  then `adjustOwnerCount(view(), owner, -2, j_)` fires.  The `-2` is the
 *  exact inverse of `VaultCreate`'s `+2`, accounting for both the vault SLE
 *  and the pseudo-account destroyed in Step 4.  The vault SLE is erased last.
 *
 *  Errors indicating impossible ledger state (missing pseudo-account,
 *  mismatched issuance, failed directory removal) return `tefBAD_LEDGER` or
 *  `tefINTERNAL` rather than fee-claiming `tec` codes, signalling internal
 *  inconsistency rather than a user-correctable condition.
 */
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
    if (auto const mptoken = view().peek(keylet::mptoken(shareMPTID, account_)))
    {
        if (auto const ter = removeEmptyHolding(view(), account_, MPTIssue(shareMPTID), j_);
            !isTesSuccess(ter))
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultDelete: failed to remove vault owner's MPToken"
                << " MPTID=" << to_string(shareMPTID)  //
                << " account=" << toBase58(account_)   //
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
VaultDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
