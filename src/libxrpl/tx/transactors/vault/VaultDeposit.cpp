#include <xrpl/tx/transactors/vault/VaultDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <optional>
#include <stdexcept>

namespace xrpl {

[[nodiscard]]
static STAmount
roundToVaultScale(STAmount const& amount, SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::roundToVaultScale : valid vault sle");
    XRPL_ASSERT(
        amount.asset() == vault->at(sfAsset), "xrpl::roundToVaultScale : valid vault asset");

    if (amount.integral())
        return amount;

    int const postScale = [&]() {
        NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
        return scale(vault->at(sfAssetsTotal) + amount, vault->at(sfAsset));
    }();
    return roundToScale(amount, postScale, Number::RoundingMode::Downward);
}

// True if debiting `assets` would leave the depositor's balance where it started, so the deposit
// would mint shares against a transfer that never happened. Asking the balance directly whether it
// notices the debit avoids having to infer the rounding step: it has to be the stored balance that
// answers, because that magnitude is what governs the rounding, and it is not the same as the
// spendable amount, which also counts what the counterparty's limit allows.
[[nodiscard]]
static bool
roundsToZeroForDepositor(
    ReadView const& view,
    AccountID const& account,
    STAmount const& assets,
    beast::Journal j)
{
    if (assets.integral())
        return false;

    auto const balance = accountHolds(
        view,
        account,
        assets.asset(),
        FreezeHandling::ZeroIfFrozen,
        AuthHandling::ZeroIfUnauthorized,
        j,
        SpendableHandling::SimpleBalance);

    if (balance - assets != balance)
        return false;

    JLOG(j.warn()) << "VaultDeposit: amount " << assets.getFullText()
                   << " leaves the depositor's balance " << balance.getFullText() << " unchanged";
    return true;
}

NotTEC
VaultDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "VaultDeposit: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
VaultDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const fix320Enabled = ctx.view.rules().enabled(fixCleanup3_2_0);
    auto const fix330Enabled = ctx.view.rules().enabled(fixCleanup3_3_0);

    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    if (ctx.view.rules().enabled(featureLendingProtocolV1_1))
    {
        auto const phase = getVaultPhase(ctx.view, vault);
        if (phase == VaultPhase::Investment || phase == VaultPhase::Redemption)
        {
            JLOG(ctx.j.debug()) << "VaultDeposit: vault deposit is not allowed in the investment "
                                   "or redemption phase.";
            return tecEXPIRED;
        }
    }

    auto const& account = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    if (amount.asset() != vaultAsset)
        return tecWRONG_ASSET;

    auto const& vaultAccount = vault->at(sfAccount);
    if (auto ter = canTransfer(ctx.view, vaultAsset, account, vaultAccount); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "VaultDeposit: vault assets are non-transferable.";
        return ter;
    }

    auto const mptIssuanceID = vault->at(sfShareMPTID);
    auto const vaultShare = MPTIssue(mptIssuanceID);
    if (vaultShare == amount.asset())
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultDeposit: vault shares and assets cannot be same.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
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

    if (fix330Enabled)
    {
        if (auto const ret = checkDepositFreeze(ctx.view, account, vaultAccount, vaultAsset))
            return ret;
    }
    else
    {
        // Cannot deposit inside Vault an Asset frozen for the depositor
        if (isFrozen(ctx.view, account, vaultAsset))
            return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;

        // Cannot deposit if the shares of the vault are frozen
        if (isFrozen(ctx.view, account, vaultShare))
            return tecLOCKED;
    }

    // The vault owner is authorized to deposit unconditionally. An expired
    // credential is tolerated here because doApply deletes it.
    if (vault->isFlag(lsfVaultPrivate) && account != vault->at(sfOwner))
    {
        if (auto const err = checkVaultDomain(ctx.view, sleIssuance, account, SuppressExpired::Yes);
            !isTesSuccess(err))
            return err;
    }

    // Source MPToken must exist (if asset is an MPT)
    if (auto const ter = requireAuth(ctx.view, vaultAsset, account); !isTesSuccess(ter))
        return ter;

    auto const roundedAmount = fix320Enabled ? roundToVaultScale(amount, vault) : amount;

    if (fix320Enabled && roundedAmount == beast::kZero)
    {
        JLOG(ctx.j.warn()) << "VaultDeposit: deposit amount: " << ctx.tx[sfAmount]
                           << " is zero at vault scale";
        return tecPRECISION_LOSS;
    }

    auto const accountBalance = accountHolds(
        ctx.view,
        account,
        vaultAsset,
        FreezeHandling::ZeroIfFrozen,
        AuthHandling::ZeroIfUnauthorized,
        ctx.j,
        SpendableHandling::FullBalance);

    if (accountBalance < roundedAmount)
        return tecINSUFFICIENT_FUNDS;

    // IOU precision checks
    if (fix320Enabled && !roundedAmount.integral())
    {
        // reject deposits that would canonicalize to a no-op at the depositor's trustline scale.
        // Skipped for issuer-as-depositor: accountHolds returns (kMaxValue @ kMaxOffset) which
        // would always trip the predicate.
        if (account != amount.getIssuer() &&
            amount.isZeroAtScale(scale(accountBalance, vaultAsset)))
        {
            JLOG(ctx.j.warn()) << "VaultDeposit: amount " << amount.getFullText()
                               << " rounds to zero at counterparty trust-line scale";
            return tecPRECISION_LOSS;
        }
    }

    return tesSUCCESS;
}

TER
VaultDeposit::doApply()
{
    bool const fix320Enabled = view().rules().enabled(fixCleanup3_2_0);
    bool const fix340Enabled = view().rules().enabled(fixCleanup3_4_0);
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    auto applyViewContext = ctx_.getApplyViewContext();
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const vaultAsset = vault->at(sfAsset);

    // Post-amendment IOU only: round Downward to the AssetsTotal precision so
    // a sub-ULP tail can't be silently absorbed by one rail and not the other.
    auto const amount =
        fix320Enabled ? roundToVaultScale(ctx_.tx[sfAmount], vault) : ctx_.tx[sfAmount];

    // We validated zero-amount in preclaim, if we ended up with zero now, fail hard.
    if (amount == beast::kZero)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: deposit amount: " << ctx_.tx[sfAmount] << " is zero";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Make sure the depositor can hold shares.
    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const& vaultAccount = vault->at(sfAccount);
    // Note, vault owner is always authorized
    if (vault->isFlag(lsfVaultPrivate) && accountID_ != vault->at(sfOwner))
    {
        if (auto const err = enforceMPTokenAuthorization(
                applyViewContext, mptIssuanceID, accountID_, preFeeBalance_, j_);
            !isTesSuccess(err))
            return err;
    }
    else  // !vault->isFlag(lsfVaultPrivate) || accountID_ == vault->at(sfOwner)
    {
        // No authorization needed, but must ensure there is MPToken
        if (!view().exists(keylet::mptoken(mptIssuanceID, accountID_)))
        {
            if (auto const err = authorizeMPToken(
                    applyViewContext,
                    preFeeBalance_,
                    mptIssuanceID->value(),
                    accountID_,
                    ctx_.journal);
                !isTesSuccess(err))
                return err;
        }

        // If the vault is private, set the authorized flag for the vault owner
        if (vault->isFlag(lsfVaultPrivate))
        {
            // This follows from the reverse of the outer enclosing if condition
            XRPL_ASSERT(
                accountID_ == vault->at(sfOwner), "xrpl::VaultDeposit::doApply : account is owner");
            if (auto const err = authorizeMPToken(
                    applyViewContext,
                    preFeeBalance_,             // priorBalance
                    mptIssuanceID->value(),     // mptIssuanceID
                    sleIssuance->at(sfIssuer),  // account
                    ctx_.journal,
                    {},         // flags
                    accountID_  // holderID
                );
                !isTesSuccess(err))
                return err;
        }
    }

    STAmount sharesCreated = {vault->at(sfShareMPTID)}, assetsDeposited;

    // Number arithmetic can throw overflow_error when Scale and totals are large. Caught below.
    try
    {
        // Compute exchange before transferring any amounts.
        {
            auto const maybeShares = assetsToSharesDeposit(vault, sleIssuance, amount);
            if (!maybeShares)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            sharesCreated = *maybeShares;
        }

        if (sharesCreated == beast::kZero)
            return tecPRECISION_LOSS;

        // Convert shares back to assets so the depositor is debited for the amount actually minted.
        // The truncated share count is worth <= amount; without this the difference would be
        // credited to the vault for free.
        auto const maybeAssets = sharesToAssetsDeposit(vault, sleIssuance, sharesCreated);
        if (!maybeAssets)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
        // The round-trip must never return more than the original amount. If it does, a conversion
        // helper is broken. Reject rather than overcharge the depositor.
        if (*maybeAssets > amount)
        {
            // LCOV_EXCL_START
            JLOG(j_.error()) << "VaultDeposit: would take more than offered.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
        assetsDeposited = *maybeAssets;

        // Post-fixCleanup3_4_0: round the deposit to the sfAssetsTotal scale so all accounting
        // fields (trust line / MPT, sfAssetsAvailable, sfAssetsTotal) change by the same
        // representable delta.
        if (fix340Enabled)
        {
            // Round down at the posterior sfAssetsTotal scale so the vault is credited by no more
            // than the depositor paid. Keep the share count from the first round trip: the clamp
            // only drops a last digit of the new total. Converting the clamped amount back to
            // shares would mint fewer shares while still charging the N-share debit.
            auto const maybeClamped = clampToAssetsTotalScale(vault, assetsDeposited);
            if (!maybeClamped)
                return maybeClamped.error();
            assetsDeposited = *maybeClamped;

            // The actual deposit amount is truncated to whole shares, converted back to assets,
            // and clamped to the sfAssetsTotal scale (post-fixCleanup3_4_0). Check the depositor's
            // balance here—after clamping—before making any state changes.
            if (roundsToZeroForDepositor(view(), accountID_, assetsDeposited, j_))
                return tecPRECISION_LOSS;
        }
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough Scale
        // so we avoid spamming the log and only use debug here.
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

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    // Transfer assets from depositor to vault.
    if (auto const ter = accountSend(
            view(), accountID_, vaultAccount, assetsDeposited, j_, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // This check is wrong. Disable it with fixCleanup3_2_0.
    // For XRP and MPT the predicate is structurally unsatisfiable: xrpLiquid clamps at zero, and
    // MPT balances are unsigned. For IOUs it only fires when the deposit drove the depositor's
    // trust line into debt the exact case preclaim authorizes via SpendableHandling::FullBalance.
    // The check thus converts a preclaim- authorized deposit into tefINTERNAL after the asset
    // transfer.
    if (!fix320Enabled)
    {
        // Sanity check
        if (accountHolds(
                view(),
                accountID_,
                assetsDeposited.asset(),
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                j_) < beast::kZero)
        {
            JLOG(j_.error()) << "VaultDeposit: negative balance of account assets.";
            return tefINTERNAL;
        }
    }

    // Transfer shares from vault to depositor.
    if (auto const ter = accountSend(
            view(), vaultAccount, accountID_, sharesCreated, j_, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    associateAsset(*vault, vaultAsset);

    return tesSUCCESS;
}

void
VaultDeposit::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
