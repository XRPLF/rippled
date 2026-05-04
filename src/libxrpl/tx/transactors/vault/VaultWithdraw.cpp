#include <xrpl/tx/transactors/vault/VaultWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace xrpl {

NotTEC
VaultWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::kZERO)
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::kZERO)
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
    if (auto ter = canTransfer(ctx.view, vaultAsset, vaultAccount, dstAcct); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: vault assets are non-transferable.";
        return ter;
    }

    // Enforce valid withdrawal policy
    if (vault->at(sfWithdrawalPolicy) != kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultWithdraw: invalid withdrawal policy.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (ctx.view.rules().enabled(fixSecurity3_1_3) && amount.asset() == vaultShare)
    {
        // Post-fixSecurity3_1_3: if the user specified shares, convert
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

        try
        {
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, amount);
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

    // Post-fixCleanup3_2_0: reject withdrawals whose amount is sub-ULP at
    // the vault's coarsest accounting rail. Same rationale as the
    // symmetric check in VaultDeposit::preclaim — surface the precision
    // loss here as tecPRECISION_LOSS instead of letting VaultInvariant
    // catch it at finalize as tecINVARIANT_FAILED. Only applied when the
    // amount is in vault assets; for share-denominated withdrawals the
    // assets-equivalent is computed in doApply and that path is checked
    // by the helper.
    if (ctx.view.rules().enabled(fixCleanup3_2_0) && amount.asset() == vaultAsset)
    {
        int const coarsestScale = std::max(
            scale(vault->at(sfAssetsTotal), vaultAsset),
            scale(vault->at(sfAssetsAvailable), vaultAsset));
        if (roundToAsset(vaultAsset, amount, coarsestScale).signum() == 0)
        {
            JLOG(ctx.j.warn()) << "VaultWithdraw: amount " << amount.getFullText()
                               << " is sub-ULP at vault coarsest scale " << coarsestScale;
            return tecPRECISION_LOSS;
        }
    }

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
    try
    {
        if (amount.asset() == vaultAsset)
        {
            // Fixed assets, variable shares.
            {
                auto const maybeShares = assetsToSharesWithdraw(vault, sleIssuance, amount);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            if (sharesRedeemed == beast::kZERO)
                return tecPRECISION_LOSS;
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            // Fixed shares, variable assets.
            sharesRedeemed = amount;
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
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
        // It's easy to hit this exception from Number with large enough Scale  so we avoid spamming
        // the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultWithdraw: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
            << ", amount=" << amount.value();
        return tecPATH_DRY;
    }

    if (accountHolds(
            view(), account_, share, FreezeHandling::ZeroIfFrozen, AuthHandling::IgnoreAuth, j_) <
        sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    auto const dstAcct = ctx_.tx[~sfDestination].value_or(account_);

    // When the destination's IOU trust-line balance sits in a coarser exponent band than the
    // requested amount (e.g. balance >= 10^16 with grid step 10, withdraw of 1 USD), the
    // destination's +amount inflow rounds back to its pre-state and the receiver effectively gains
    // nothing.
    // Pre-fixCleanup3_2_0, this fired VaultInvariant's "withdrawal must increase
    // destination balance" at finalize as tecINVARIANT_FAILED the same shape would also strand a
    // sole depositor whose own trust line is past the edge, since the only path out is via the
    // vault.
    // Post-amendment, the IOU-only carve-out in VaultInvariant tolerates a zero-rounded
    // destination delta paired with a vault loss that also rounds to zero at the comparison scale.
    // The vault still drains, the depositor's shares burn. The "by equal amount" check
    // remains the safety net: vault loss greater than 1 ULP at the comparison scale paired with a
    // zero destination delta is still a genuine value-transfer mismatch and still fires.
    //
    // XRP and MPT are not affected — those asset types are integer-exact, so a zero-rounded
    // destination delta on a non-zero withdrawal is unreachable under any rounding mode and
    // continues to fire the strict invariant. The carve-out is gated to IOU at the invariant
    // level (`vaultAsset.holds<Issue>()`).
    return withdrawFromVault(
        view(),
        ctx_.tx,
        vault,
        account_,
        dstAcct,
        preFeeBalance_,
        assetsWithdrawn,
        sharesRedeemed,
        j_);
}

void
VaultWithdraw::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
VaultWithdraw::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
