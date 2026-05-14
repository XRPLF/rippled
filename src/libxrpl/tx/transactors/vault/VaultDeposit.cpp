/** @file VaultDeposit.cpp
 *  Implementation of the `ttVAULT_DEPOSIT` transactor.
 *
 *  Deposits a fungible asset (XRP, IOU, or MPT) into a vault and mints
 *  vault-share MPTokens for the depositor. The share/asset exchange rate is
 *  determined by the vault's current `sfAssetsTotal` and the share issuance's
 *  `sfOutstandingAmount`; on a fresh vault both are zero and the initial rate
 *  is seeded via `sfScale`. See VaultHelpers.h for the arithmetic.
 */
#include <xrpl/tx/transactors/vault/VaultDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <stdexcept>

namespace xrpl {

NotTEC
VaultDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "VaultDeposit: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::kZERO)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** Read-only ledger validation for a vault deposit.
 *
 *  Checks are ordered from cheapest to most expensive and fail-fast on the
 *  first violation. The private-vault authorization path has two deliberate
 *  design choices worth noting:
 *
 *  - **`tecEXPIRED` suppression**: `credentials::validDomain` may return
 *    `tecEXPIRED` when the account's credential has expired. This code is
 *    suppressed here (the transaction proceeds) because deleting the expired
 *    credential is a ledger state mutation, which is only permitted in
 *    `doApply`. The `doApply` call to `enforceMPTokenAuthorization` handles
 *    the deletion.
 *
 *  - **No issuer-grant fallback**: the vault's share MPT issuance is managed
 *    by the vault pseudo-account, which cannot sign transactions and therefore
 *    cannot proactively grant MPT authorizations to individual holders. The
 *    only supported admission mechanism for private vaults is the domain-
 *    credential path via `sfDomainID` on the share issuance. Absence of
 *    `sfDomainID` on a private vault's issuance is an unconditional
 *    `tecNO_AUTH`.
 *
 *  The `lsfMPTLocked` and identical-asset-type checks are classified as
 *  `tefINTERNAL` and marked `LCOV_EXCL` because the `VaultCreate` transactor
 *  guarantees they cannot occur under correct ledger logic.
 */
TER
VaultDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto const& account = ctx.tx[sfAccount];
    auto const assets = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    if (assets.asset() != vaultAsset)
        return tecWRONG_ASSET;

    auto const& vaultAccount = vault->at(sfAccount);
    if (auto ter = canTransfer(ctx.view, vaultAsset, account, vaultAccount); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "VaultDeposit: vault assets are non-transferable.";
        return ter;
    }

    auto const mptIssuanceID = vault->at(sfShareMPTID);
    auto const vaultShare = MPTIssue(mptIssuanceID);
    if (vaultShare == assets.asset())
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDeposit: vault shares and assets cannot be same.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDeposit: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (sleIssuance->isFlag(lsfMPTLocked))
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDeposit: issuance of vault shares is locked.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (isFrozen(ctx.view, account, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;

    if (isFrozen(ctx.view, account, vaultShare))
        return tecLOCKED;

    if (vault->isFlag(lsfVaultPrivate) && account != vault->at(sfOwner))
    {
        auto const maybeDomainID = sleIssuance->at(~sfDomainID);
        if (maybeDomainID)
        {
            if (auto const err = credentials::validDomain(ctx.view, *maybeDomainID, account);
                !isTesSuccess(err) && err != tecEXPIRED)
                return err;
        }
        else
        {
            return tecNO_AUTH;
        }
    }

    if (auto const ter = requireAuth(ctx.view, vaultAsset, account); !isTesSuccess(ter))
        return ter;

    if (accountHolds(
            ctx.view,
            account,
            vaultAsset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j,
            SpendableHandling::FullBalance) < assets)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

/** Mutate the ledger to execute a vault deposit.
 *
 *  ### MPToken provisioning
 *
 *  Before any arithmetic, the depositor must hold an MPToken for vault shares.
 *  Two distinct code paths handle this:
 *
 *  - **Private vault, non-owner**: `enforceMPTokenAuthorization` is called.
 *    It deletes any expired credentials found in the ledger (the deferred
 *    side-effect suppressed in `preclaim`) and then creates the MPToken entry
 *    for the depositor if it does not yet exist.
 *
 *  - **Public vault, or vault owner**: `authorizeMPToken` is called only when
 *    the MPToken does not yet exist — no domain check is required. When the
 *    depositor is the vault owner of a *private* vault, a second
 *    `authorizeMPToken` call additionally provisions an MPToken on the vault's
 *    pseudo-account (`sleIssuance->at(sfIssuer)`) authorised for the owner.
 *    The pseudo-account cannot self-authorise, so this is the only mechanism
 *    by which it can hold shares for internal accounting.
 *
 *  ### Exchange arithmetic (two-step, depositor-protective)
 *
 *  1. `assetsToSharesDeposit(vault, sleIssuance, amount)` — converts the
 *     offered asset amount into shares, **truncating** (floor) to an integral
 *     MPT unit. On a fresh vault (`sfAssetsTotal == 0`) the initial rate is
 *     seeded using `sfScale`: `shares = floor(assets.mantissa * 10^(assets.exponent + scale))`.
 *     Returns `tecPRECISION_LOSS` when the floor rounds to zero — the deposit
 *     is too small relative to the current share price.
 *
 *  2. `sharesToAssetsDeposit(vault, sleIssuance, sharesCreated)` — inverts the
 *     truncated share count back to an exact asset amount, guaranteeing
 *     `assetsDeposited <= amount`. Any sub-share-unit fractional asset
 *     remainder stays with the depositor. An assertion verifies this invariant;
 *     violation indicates a bug in the arithmetic helpers.
 *
 *  `std::overflow_error` from either helper (reachable when `sfScale` is large
 *  and balances are high) is caught and converted to `tecPATH_DRY`. The log
 *  level is intentionally `debug`, not `error`, because this is a
 *  user-triggerable condition rather than a ledger-corruption sentinel.
 *
 *  ### Mutation ordering and cap enforcement
 *
 *  `sfAssetsTotal` and `sfAssetsAvailable` are incremented and the vault SLE
 *  flushed to the view *before* the cap check and before either `accountSend`.
 *  This ordering is deliberate: the `sfAssetsMaximum` guard must see the
 *  post-deposit total to correctly reject a deposit that would overflow the
 *  cap. If it would exceed the cap the transaction returns `tecLIMIT_EXCEEDED`
 *  and the framework discards all view mutations.
 *
 *  Both `accountSend` calls use `WaiveTransferFee::Yes` to prevent asset
 *  transfer fees from distorting `sfAssetsTotal` accounting and corrupting the
 *  exchange rate for future depositors.
 *
 *  `associateAsset` is the final call; per the `STTakesAsset` contract, it
 *  re-rounds stored numeric values to the asset's precision and must execute
 *  after all other writes to the vault SLE are complete.
 */
TER
VaultDeposit::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const vaultAsset = vault->at(sfAsset);

    auto const amount = ctx_.tx[sfAmount];
    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const& vaultAccount = vault->at(sfAccount);
    if (vault->isFlag(lsfVaultPrivate) && account_ != vault->at(sfOwner))
    {
        if (auto const err = enforceMPTokenAuthorization(
                ctx_.view(), mptIssuanceID, account_, preFeeBalance_, j_);
            !isTesSuccess(err))
            return err;
    }
    else
    {
        if (!view().exists(keylet::mptoken(mptIssuanceID, account_)))
        {
            if (auto const err = authorizeMPToken(
                    view(), preFeeBalance_, mptIssuanceID->value(), account_, ctx_.journal);
                !isTesSuccess(err))
                return err;
        }

        if (vault->isFlag(lsfVaultPrivate))
        {
            // This follows from the reverse of the outer enclosing if condition
            XRPL_ASSERT(
                account_ == vault->at(sfOwner), "xrpl::VaultDeposit::doApply : account is owner");
            if (auto const err = authorizeMPToken(
                    view(),
                    preFeeBalance_,             // priorBalance
                    mptIssuanceID->value(),     // mptIssuanceID
                    sleIssuance->at(sfIssuer),  // account
                    ctx_.journal,
                    {},       // flags
                    account_  // holderID
                );
                !isTesSuccess(err))
                return err;
        }
    }

    STAmount sharesCreated = {vault->at(sfShareMPTID)}, assetsDeposited;
    try
    {
        {
            auto const maybeShares = assetsToSharesDeposit(vault, sleIssuance, amount);
            if (!maybeShares)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            sharesCreated = *maybeShares;
        }
        if (sharesCreated == beast::kZERO)
            return tecPRECISION_LOSS;

        auto const maybeAssets = sharesToAssetsDeposit(vault, sleIssuance, sharesCreated);
        if (!maybeAssets)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
        if (*maybeAssets > amount)
        {
            // LCOV_EXCL_START
            JLOG(j_.error()) << "VaultDeposit: would take more than offered.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
        assetsDeposited = *maybeAssets;
    }
    catch (std::overflow_error const&)
    {
        JLOG(j_.debug())  //
            << "VaultDeposit: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount) << ", amount=" << amount;
        return tecPATH_DRY;
    }

    XRPL_ASSERT(
        sharesCreated.asset() != assetsDeposited.asset(),
        "xrpl::VaultDeposit::doApply : assets are not shares");

    vault->at(sfAssetsTotal) += assetsDeposited;
    vault->at(sfAssetsAvailable) += assetsDeposited;
    view().update(vault);

    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    if (auto const ter =
            accountSend(view(), account_, vaultAccount, assetsDeposited, j_, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    if (accountHolds(
            view(),
            account_,
            assetsDeposited.asset(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j_) < beast::kZERO)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: negative balance of account assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (auto const ter =
            accountSend(view(), vaultAccount, account_, sharesCreated, j_, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    associateAsset(*vault, vaultAsset);

    return tesSUCCESS;
}

void
VaultDeposit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultDeposit::finalizeInvariants(
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
