#include <xrpld/app/tx/detail/VaultWithdraw.h>

#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::zero)
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination];
        destination.has_value())
    {
        if (*destination == beast::zero)
        {
            JLOG(ctx.j.debug())
                << "VaultWithdraw: zero/empty destination account.";
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

    auto const assets = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    auto const vaultShare = vault->at(sfShareMPTID);
    if (assets.asset() != vaultAsset && assets.asset() != vaultShare)
        return tecWRONG_ASSET;

    if (vaultAsset.native())
        ;  // No special checks for XRP
    else if (vaultAsset.holds<MPTIssue>())
    {
        auto mptID = vaultAsset.get<MPTIssue>().getMptID();
        auto issuance = ctx.view.read(keylet::mptIssuance(mptID));
        if (!issuance)
            return tecOBJECT_NOT_FOUND;
        if (!issuance->isFlag(lsfMPTCanTransfer))
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error())
                << "VaultWithdraw: vault assets are non-transferable.";
            return tecNO_AUTH;
            // LCOV_EXCL_STOP
        }
    }
    else if (vaultAsset.holds<Issue>())
    {
        auto const issuer =
            ctx.view.read(keylet::account(vaultAsset.getIssuer()));
        if (!issuer)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error())
                << "VaultWithdraw: missing issuer of vault assets.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    // Enforce valid withdrawal policy
    if (vault->at(sfWithdrawalPolicy) != vaultStrategyFirstComeFirstServe)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultWithdraw: invalid withdrawal policy.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const account = ctx.tx[sfAccount];
    auto const dstAcct = ctx.tx[~sfDestination].value_or(account);
    auto const sleDst = ctx.view.read(keylet::account(dstAcct));
    if (sleDst == nullptr)
        return account == dstAcct ? tecINTERNAL : tecNO_DST;

    if (sleDst->isFlag(lsfRequireDestTag) &&
        !ctx.tx.isFieldPresent(sfDestinationTag))
        return tecDST_TAG_NEEDED;  // Cannot send without a tag

    // Withdrawal to a 3rd party destination account is essentially a transfer,
    // via shares in the vault. Enforce all the usual asset transfer checks.
    if (account != dstAcct && sleDst->isFlag(lsfDepositAuth))
    {
        if (!ctx.view.exists(keylet::depositPreauth(dstAcct, account)))
            return tecNO_PERMISSION;
    }

    // If sending to Account (i.e. not a transfer), we will also create (only
    // if authorized) a trust line or MPToken as needed, in doApply().
    // Destination MPToken or trust line must exist if _not_ sending to Account.
    AuthType const authType =
        account == dstAcct ? AuthType::WeakAuth : AuthType::StrongAuth;
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType);
        !isTesSuccess(ter))
        return ter;

    // Cannot withdraw from a Vault an Asset frozen for the destination account
    if (auto const ret = checkFrozen(ctx.view, dstAcct, vaultAsset))
        return ret;

    if (auto const ret = checkFrozen(ctx.view, account, vaultShare))
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
    try
    {
        if (amount.asset() == vaultAsset)
        {
            // Fixed assets, variable shares.
            {
                auto const maybeShares =
                    assetsToSharesWithdraw(vault, sleIssuance, amount);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            if (sharesRedeemed == beast::zero)
                return tecPRECISION_LOSS;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            // Fixed shares, variable assets.
            sharesRedeemed = amount;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else
            return tefINTERNAL;  // LCOV_EXCL_LINE
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
            view(),
            account_,
            share,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahIGNORE_AUTH,
            j_) < sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "ripple::VaultWithdraw::doApply : loss and assets do balance");

    // The vault must have enough assets on hand. The vault may hold assets
    // that it has already pledged. That is why we look at AssetAvailable
    // instead of the pseudo-account balance.
    if (*assetsAvailable < assetsWithdrawn)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    assetsTotal -= assetsWithdrawn;
    assetsAvailable -= assetsWithdrawn;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from depositor to vault.
    if (auto const ter = accountSend(
            view(),
            account_,
            vaultAccount,
            sharesRedeemed,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the account balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (account_ != vault->at(sfOwner))
    {
        if (auto const ter = removeEmptyHolding(
                view(), account_, sharesRedeemed.asset(), j_);
            isTesSuccess(ter))
        {
            JLOG(j_.debug())  //
                << "VaultWithdraw: removed empty MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(account_);
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultWithdraw: failed to remove MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(account_)      //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
        // else quietly ignore, account balance is not zero
    }

    auto const dstAcct = ctx_.tx[~sfDestination].value_or(account_);
    if (!vaultAsset.native() &&               //
        dstAcct != vaultAsset.getIssuer() &&  //
        dstAcct == account_)
    {
        if (auto const ter = addEmptyHolding(
                view(), account_, mPriorBalance, vaultAsset, j_);
            !isTesSuccess(ter) && ter != tecDUPLICATE)
            return ter;
    }

    // Transfer assets from vault to depositor or destination account.
    if (auto const ter = accountSend(
            view(),
            vaultAccount,
            dstAcct,
            assetsWithdrawn,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Sanity check
    if (accountHolds(
            view(),
            vaultAccount,
            assetsWithdrawn.asset(),
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_) < beast::zero)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultWithdraw: negative balance of vault assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
}

}  // namespace ripple
