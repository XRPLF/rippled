#include <xrpl/tx/transactors/vault/VaultWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
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

static WaiveUnrealizedLoss
shouldWaiveWithdrawal(ReadView const& view, AccountID const& account, SLE::const_ref issuance)
{
    XRPL_ASSERT(
        issuance && issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::shouldWaiveWithdrawal : valid issuance sle");

    return view.rules().enabled(fixCleanup3_2_0) && isSoleShareholder(view, account, issuance)
        ? WaiveUnrealizedLoss::Yes
        : WaiveUnrealizedLoss::No;
}

NotTEC
VaultWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::kZero)
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::kZero)
        {
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
VaultWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto const amount = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    auto const vaultShare = vault->at(sfShareMPTID);
    if (amount.asset() != vaultAsset && amount.asset() != vaultShare)
        return tecWRONG_ASSET;

    auto const& vaultAccount = vault->at(sfAccount);
    auto const& account = ctx.tx[sfAccount];
    auto const& dstAcct = ctx.tx[~sfDestination].value_or(account);
    // Post-fixCleanup3_2_0: withdraw is a recovery path that bypasses the
    // lsfMPTCanTransfer flag check, so an issuer cannot trap depositor funds.
    // Other transferability checks (IOU NoRipple, freeze, requireAuth) still
    // apply.
    auto const waive = ctx.view.rules().enabled(fixCleanup3_2_0) ? WaiveMPTCanTransfer::Yes
                                                                 : WaiveMPTCanTransfer::No;
    if (auto ter = canTransfer(ctx.view, vaultAsset, vaultAccount, dstAcct, waive);
        !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: vault assets are non-transferable.";
        return ter;
    }

    // Enforce valid withdrawal policy
    if (vault->at(sfWithdrawalPolicy) != kVaultStrategyFirstComeFirstServe)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultWithdraw: invalid withdrawal policy.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (ctx.view.rules().enabled(fixCleanup3_1_3) && amount.asset() == vaultShare)
    {
        // Post-fixCleanup3_1_3: if the user specified shares, convert
        // to the equivalent asset amount before checking withdrawal
        // limits. Pre-amendment the limit check was skipped for
        // share-denominated withdrawals.
        auto const sleIssuance = ctx.view.read(keylet::mptIssuance(vaultShare));
        if (!sleIssuance)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error()) << "VaultWithdraw: missing issuance of vault shares.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }

        // When the user is the sole shareholder they own both the available and future value.
        // We waive the unrealized-loss subtraction in this case to avoid user withdrawing all of
        // their shares but keeping future value in the vault.
        auto const waiveUnrealizedLoss = shouldWaiveWithdrawal(ctx.view, account, sleIssuance);
        try
        {
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, amount, waiveUnrealizedLoss);
            if (!maybeAssets)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            if (auto const ret = canWithdraw(
                    ctx.view,
                    account,
                    dstAcct,
                    *maybeAssets,
                    ctx.tx.isFieldPresent(sfDestinationTag)))
                return ret;
        }
        catch (std::overflow_error const&)
        {
            // It's easy to hit this exception from Number with large enough Scale
            // so we avoid spamming the log and only use debug here.
            JLOG(ctx.j.debug())  //
                << "VaultWithdraw: overflow error with"
                << " scale=" << (int)vault->at(sfScale)  //
                << ", assetsTotal=" << vault->at(sfAssetsTotal)
                << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
                << ", amount=" << amount.value();
            return tecPATH_DRY;
        }
    }
    else
    {
        if (auto const ret = canWithdraw(ctx.view, ctx.tx))
            return ret;
    }

    // If sending to Account (i.e. not a transfer), we will also create (only
    // if authorized) a trust line or MPToken as needed, in doApply().
    // Destination MPToken or trust line must exist if _not_ sending to Account.
    AuthType const authType = account == dstAcct ? AuthType::WeakAuth : AuthType::StrongAuth;
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType); !isTesSuccess(ter))
        return ter;

    // Cannot withdraw from a Vault an Asset frozen for the destination account
    if (auto const ret = checkFrozen(ctx.view, dstAcct, vaultAsset))
        return ret;

    // Cannot return shares to the vault, if the underlying asset was frozen for
    // the submitter
    if (auto const ret = checkFrozen(ctx.view, account, Asset{vaultShare}))
        return ret;

    return tesSUCCESS;
}

TER
VaultWithdraw::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = *((*vault)[sfShareMPTID]);
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultWithdraw: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Note, we intentionally do not check lsfVaultPrivate flag on the Vault. If
    // you have a share in the vault, it means you were at some point authorized
    // to deposit into it, and this means you are also indefinitely authorized
    // to withdraw from it.

    auto const amount = ctx_.tx[sfAmount];
    Asset const vaultAsset = vault->at(sfAsset);

    MPTIssue const share{mptIssuanceID};
    STAmount sharesRedeemed = {share};
    STAmount assetsWithdrawn;

    // When the user is the sole shareholder they own both the available and future value.
    // We waive the unrealized-loss subtraction in this case to avoid user withdrawing all of their
    // shares but keeping future value in the vault.
    auto const waiveUnrealizedLoss = shouldWaiveWithdrawal(view(), accountID_, sleIssuance);
    try
    {
        if (amount.asset() == vaultAsset)
        {
            // Fixed assets, variable shares.
            {
                auto const maybeShares = assetsToSharesWithdraw(
                    vault, sleIssuance, amount, TruncateShares::No, waiveUnrealizedLoss);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            if (sharesRedeemed == beast::kZero)
                return tecPRECISION_LOSS;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed, waiveUnrealizedLoss);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            // Fixed shares, variable assets.
            sharesRedeemed = amount;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed, waiveUnrealizedLoss);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else
        {
            return tefINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough Scale
        // so we avoid spamming the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultWithdraw: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
            << ", amount=" << amount.value();
        return tecPATH_DRY;
    }

    if (accountHolds(
            view(), accountID_, share, FreezeHandling::ZeroIfFrozen, AuthHandling::IgnoreAuth, j_) <
        sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "xrpl::VaultWithdraw::doApply : loss and assets do balance");

    // The vault must have enough assets on hand.
    if (*assetsAvailable < assetsWithdrawn)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    // Post-fixCleanup3_2_0 "final withdrawal" rule:
    // a transaction that would burn every outstanding share is only permitted when the vault is in
    // a clean state — no outstanding receivables and no unrealized loss. Otherwise the resulting
    // (shares == 0, assetsTotal > 0) state would violate the zero-sized-vault invariant.
    //
    // When the rule applies, the payout is the remaining sfAssetsAvailable; in a clean vault
    // the helper result should already equal that value, and any mismatch is a rounding artifact
    // worth logging.
    bool const isFinalWithdrawal =
        sharesRedeemed == STAmount{share, sleIssuance->at(sfOutstandingAmount)};
    if (view().rules().enabled(fixCleanup3_2_0) && isFinalWithdrawal)
    {
        // Unreachable: a final withdrawal with lossUnrealized > 0 has
        // assetsWithdrawn == assetsTotal > assetsAvailable, which the
        // insufficient-funds guard above already rejected.
        if (*lossUnrealized != beast::kZero)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::VaultWithdraw::doApply : final withdrawal with non-zero unrealized loss");
            JLOG(j_.fatal())
                << "VaultWithdraw: "  //
                   "Cannot burn all outstanding shares while unrealized loss is non-zero";
            return tefINTERNAL;
            // LCOV_EXCL_END
        }

        STAmount const allAvailable{vaultAsset, *assetsAvailable};
        if (assetsWithdrawn != allAvailable)
        {
            JLOG(j_.error())  //
                << "VaultWithdraw: final withdrawal share-value mismatch;"
                << " computed=" << assetsWithdrawn.getText()
                << " assetsAvailable=" << allAvailable.getText();
        }
        assetsWithdrawn = allAvailable;

        // Do not let dust accumulate in the Vault.
        assetsTotal = 0;
        assetsAvailable = 0;
    }
    else
    {
        assetsTotal -= assetsWithdrawn;
        assetsAvailable -= assetsWithdrawn;
    }
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from depositor to vault.
    if (auto const ter = accountSend(
            view(), accountID_, vaultAccount, sharesRedeemed, j_, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the account balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (accountID_ != vault->at(sfOwner))
    {
        if (auto const ter = removeEmptyHolding(view(), accountID_, sharesRedeemed.asset(), j_);
            isTesSuccess(ter))
        {
            JLOG(j_.debug())  //
                << "VaultWithdraw: removed empty MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(accountID_);
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultWithdraw: failed to remove MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(accountID_)    //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
        // else quietly ignore, account balance is not zero
    }

    auto const dstAcct = ctx_.tx[~sfDestination].value_or(accountID_);

    associateAsset(*vault, vaultAsset);

    return doWithdraw(
        view(), ctx_.tx, accountID_, dstAcct, vaultAccount, preFeeBalance_, assetsWithdrawn, j_);
}

void
VaultWithdraw::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultWithdraw::finalizeInvariants(
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
