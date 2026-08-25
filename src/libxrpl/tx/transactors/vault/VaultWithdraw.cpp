#include <xrpl/tx/transactors/vault/VaultWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
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

#include <stdexcept>

namespace xrpl {

bool
VaultWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) ||
        (ctx.rules.enabled(featureCredentials) && ctx.rules.enabled(fixCleanup3_4_0));
}

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

    if (auto const err = credentials::checkFields(ctx.tx, ctx.rules, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
VaultWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const fix313Enabled = ctx.view.rules().enabled(fixCleanup3_1_3);
    auto const fix320Enabled = ctx.view.rules().enabled(fixCleanup3_2_0);
    auto const fix330Enabled = ctx.view.rules().enabled(fixCleanup3_3_0);
    auto const fix340Enabled = ctx.view.rules().enabled(fixCleanup3_4_0);

    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    if (ctx.view.rules().enabled(featureLendingProtocolV1_1))
    {
        if (getVaultPhase(ctx.view, vault) == VaultPhase::Investment)
        {
            JLOG(ctx.j.debug())
                << "VaultWithdraw: vault withdrawal is not allowed in the investment phase.";
            return tecTOO_SOON;
        }
    }

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
    auto const waive = fix320Enabled ? WaiveMPTCanTransfer::Yes : WaiveMPTCanTransfer::No;
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

    // Validate credentials (if any) before canWithdraw, since canWithdraw may
    // call credentials::authorizedDepositPreauth which assumes credentials
    // already exist.
    if (auto const err = credentials::valid(ctx.tx, ctx.view, account, ctx.j); !isTesSuccess(err))
        return err;

    // A pseudo-account belongs to a ledger object rather than to a person and
    // must never receive funds from a user-initiated transaction. Deposit
    // authorization, which every pseudo-account carries, already refuses the
    // payout, but it reports only that the destination declines deposits and
    // leaves the real reason unsaid.
    if (fix340Enabled && isPseudoAccount(ctx.view, dstAcct))
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: cannot withdraw into a pseudo-account.";
        return tecPSEUDO_ACCOUNT;
    }

    if (fix313Enabled && amount.asset() == vaultShare)
    {
        // Post-fixCleanup3_1_3: if the user specified shares, convert
        // to the equivalent asset amount before checking withdrawal
        // limits. Pre-amendment the limit check was skipped for
        // share-denominated withdrawals.
        auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(vaultShare));
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
                    ctx.tx.isFieldPresent(sfDestinationTag),
                    ctx.tx[~sfCredentialIDs]))
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

    // The checks above only establish that an account may hold the asset. A
    // private vault additionally restricts who may take part in it, so paying
    // its asset out to a third party requires both ends of that payout to be
    // inside the vault's permissioned domain. VaultDeposit applies the same
    // domain check on the way in.
    //
    // Two cases deliberately skip the check. Withdrawing to self is never
    // restricted: losing vault access must not strand funds already deposited.
    // The asset issuer is always allowed to receive, which keeps the return
    // path for frozen assets open even for a submitter who lost access.
    if (fix340Enabled && vault->isFlag(lsfVaultPrivate) && dstAcct != account &&
        dstAcct != vaultAsset.getIssuer())
    {
        auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(vaultShare));
        if (!sleIssuance)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error()) << "VaultWithdraw: missing issuance of vault shares.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }

        // Unlike VaultDeposit we do not suppress tecEXPIRED: there is no
        // doApply step here that would clean up the expired credential.
        if (auto const ter = checkVaultDomain(ctx.view, sleIssuance, account, SuppressExpired::No);
            !isTesSuccess(ter))
            return ter;

        if (auto const ter = checkVaultDomain(ctx.view, sleIssuance, dstAcct, SuppressExpired::No);
            !isTesSuccess(ter))
            return ter;
    }

    if (fix330Enabled)
    {
        // checkWithdrawFreeze checks the underlying asset on the source
        // (vault pseudo-account), the submitter, and the destination.
        // A separate share-level freeze check is unnecessary: vault shares
        // are issued by the vault pseudo-account, which cannot submit
        // MPTokenIssuanceSet to individually lock a holder's MPToken.
        // The only way shares become locked is transitively via the
        // underlying asset, which checkWithdrawFreeze covers.
        if (auto const ret =
                checkWithdrawFreeze(ctx.view, vaultAccount, account, dstAcct, vaultAsset))
            return ret;
    }
    else
    {
        // Cannot withdraw from a Vault an Asset frozen for the destination account
        if (auto const ret = checkFrozen(ctx.view, dstAcct, vaultAsset))
            return ret;

        // Cannot return shares to the vault, if the underlying asset was frozen for
        // the submitter
        if (auto const ret = checkFrozen(ctx.view, account, Asset{vaultShare}))
            return ret;
    }
    return tesSUCCESS;
}

TER
VaultWithdraw::doApply()
{
    bool const fix340Enabled = view().rules().enabled(fixCleanup3_4_0);
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    auto applyViewContext = ctx_.getApplyViewContext();
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = *((*vault)[sfShareMPTID]);
    auto const sleIssuance = view().read(keylet::mptokenIssuance(mptIssuanceID));
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
    // to withdraw it to yourself. Sending the proceeds to somebody else is a
    // different matter, and preclaim checks such a withdrawal against the
    // vault's permissioned domain.

    auto const amount = ctx_.tx[sfAmount];
    Asset const vaultAsset = vault->at(sfAsset);

    MPTIssue const share{mptIssuanceID};
    STAmount sharesRedeemed = {share};
    STAmount assetsWithdrawn;

    // When the user is the sole shareholder they own both the available and future value.
    // We waive the unrealized-loss subtraction in this case to avoid user withdrawing all of their
    // shares but keeping future value in the vault.
    auto const waiveUnrealizedLoss = shouldWaiveWithdrawal(view(), accountID_, sleIssuance);
    // Number arithmetic can throw overflow_error when Scale and totals are large. Caught below.
    try
    {
        if (amount.asset() == vaultAsset)
        {
            // Fixed assets, variable shares.
            //
            // Pre-fixCleanup3_4_0: shares were rounded to nearest, so the
            // round-trip back to assets could exceed the requested amount.
            // That over-delivers to the depositor and can bypass the
            // preclaim canWithdraw check on the destination, which was
            // validated against the requested amount only.
            // Post-amendment: truncate shares so assetsWithdrawn <=
            // requested amount by construction. If truncation yields zero
            // shares, the tecPRECISION_LOSS guard below fires.
            auto const truncate =
                view().rules().enabled(fixCleanup3_4_0) ? TruncateShares::Yes : TruncateShares::No;
            {
                auto const maybeShares = assetsToSharesWithdraw(
                    vault, sleIssuance, amount, truncate, waiveUnrealizedLoss);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            // Shares are MPT (integer). Small requested amounts truncate to zero; refuse rather
            // than burn nothing while paying out assets.
            if (sharesRedeemed == beast::kZero)
                return tecPRECISION_LOSS;
            // Convert shares back to assets so the payout matches the shares actually burned, not
            // the requested amount. The extra would otherwise be paid from the vault for free.
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed, waiveUnrealizedLoss);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            // Fixed shares, variable assets. No round-trip: the share count is exactly what the
            // caller specified; only the payout amount is derived.
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
        // Overflow means this transaction cannot apply, but ledger state is still consistent.
        // Return tecPATH_DRY rather than a hard internal error.
        return tecPATH_DRY;
    }

    // The "final withdrawal" rule below handles its own zero-value case using
    // sfAssetsAvailable directly, so it is exempt from the checks below.
    bool const isFinalWithdrawal =
        sharesRedeemed == STAmount{share, sleIssuance->at(sfOutstandingAmount)};

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    auto const lossUnrealized = vault->at(sfLossUnrealized);

    if (fix340Enabled && !isFinalWithdrawal)
    {
        // Fixed-shares path: a small share count can round to zero assets even though the vault
        // still has backing value. Reject rather than burn shares for a zero payout. The
        // fixed-assets branch above has already rejected zero via the sharesRedeemed check.
        if (amount.asset() == share && assetsWithdrawn == beast::kZero &&
            assetsTotalForWithdrawal(vault, waiveUnrealizedLoss) != beast::kZero)
        {
            JLOG(j_.debug()) << "VaultWithdraw: fixed-share withdrawal rounds to zero assets";
            return tecPRECISION_LOSS;
        }

        // Number arithmetic can throw overflow_error when Scale and totals are large. Caught
        // below. debitIsNonZeroDust converts assetsTotal/assetsAvailable to STAmount, which is
        // exactly what a sufficiently abused sfScale can push out of STAmount's representable
        // range.
        try
        {
            // Even a non-zero withdrawal can be too small to change the stored sfAssetsTotal or
            // sfAssetsAvailable at STAmount's precision. Shares would still move, so ValidVault
            // would fail after apply; reject here instead.
            if (debitIsNonZeroDust(vaultAsset, assetsTotal, assetsWithdrawn) ||
                debitIsNonZeroDust(vaultAsset, assetsAvailable, assetsWithdrawn))
            {
                JLOG(j_.debug()) << "VaultWithdraw: withdrawal amount too small to change stored"
                                    " vault balance";
                return tecPRECISION_LOSS;
            }
        }
        // LCOV_EXCL_START
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
            // Overflow means this transaction cannot apply, but ledger state is still consistent.
            // Return tecPATH_DRY rather than a hard internal error.
            return tecPATH_DRY;
        }
        // LCOV_EXCL_STOP
    }

    // Post-fixCleanup3_3_0: preclaim already validated all freeze conditions
    // (checkWithdrawFreeze), so IgnoreFreeze avoids a redundant check that
    // would incorrectly return zero for vault pseudo-accounts whose shares
    // are frozen via a transitively frozen underlying asset.
    auto const freezeHandling = view().rules().enabled(fixCleanup3_3_0)
        ? FreezeHandling::IgnoreFreeze
        : FreezeHandling::ZeroIfFrozen;
    if (accountHolds(view(), accountID_, share, freezeHandling, AuthHandling::IgnoreAuth, j_) <
        sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    // Post-fixCleanup3_4_0: round the payout to the sfAssetsTotal scale so all three rails
    // (trust line / MPT, sfAssetsAvailable, sfAssetsTotal) change by the same representable delta.
    // Skip when assetsWithdrawn is already zero: the earlier fix340 guard above deliberately
    // permits fixed-share zero-asset withdrawals in a fully-impaired vault (where
    // assetsTotalForWithdrawal == 0), and clamping-then-rejecting would undo that. Also skip on
    // the final-withdrawal path, which overwrites assetsWithdrawn with sfAssetsAvailable below.
    if (fix340Enabled && !isFinalWithdrawal && assetsWithdrawn > beast::kZero)
    {
        // Number arithmetic can throw overflow_error when Scale and totals are large. The
        // debitIsNonZeroDust check above already performs the same STAmount conversion of
        // assetsTotal/assetsAvailable under the ambient rounding mode and would have thrown
        // (and been caught) first for any value that overflows under that mode. Only reachable
        // if RoundingMode::Upward -- forced below to keep the clamp conservative -- carries a
        // value that was in range under the ambient mode just past the max representable
        // exponent. Kept for defense in depth; not realistically triggerable from a test.
        try
        {
            // Round Upward on the negative delta: the stored total is decremented by no more than
            // it can represent, so the payout is trimmed downward and the vault never pays out
            // more than it can account for.
            assetsWithdrawn =
                clampToAssetsTotalScale(vault, -assetsWithdrawn, Number::RoundingMode::Upward);
            // Unreachable: debitIsNonZeroDust above already rejected amounts too small to
            // move sfAssetsTotal, so snapping to that same grid cannot collapse the payout
            // to zero. Kept as defense in depth.
            if (assetsWithdrawn <= beast::kZero)
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::VaultWithdraw::doApply : clamped withdrawal rounds to zero");
                return tecPRECISION_LOSS;
                // LCOV_EXCL_STOP
            }
        }
        // LCOV_EXCL_START
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
            // Overflow means this transaction cannot apply, but ledger state is still consistent.
            // Return tecPATH_DRY rather than a hard internal error.
            return tecPATH_DRY;
        }
        // LCOV_EXCL_STOP
    }

    // The vault must have enough assets on hand.
    if (*assetsAvailable < assetsWithdrawn)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    // Post-fixCleanup3_2_0: burning every outstanding share is only allowed when the vault has no
    // unrealized loss. Otherwise the resulting (shares == 0, assetsTotal > 0) state would violate
    // the zero-sized-vault invariant.
    //
    // The payout is set to the remaining sfAssetsAvailable. The helper result should already
    // equal that value in a clean vault; any mismatch is a rounding artifact and is logged.
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
            // LCOV_EXCL_STOP
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
        // Debit both rails by the same delta so sfAssetsTotal and sfAssetsAvailable stay in step,
        // as required by the ValidVault invariant.
        assetsTotal -= assetsWithdrawn;
        assetsAvailable -= assetsWithdrawn;
    }
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);

    // Transfer shares from depositor to vault.
    if (auto const ter = accountSend(
            view(), accountID_, vaultAccount, sharesRedeemed, j_, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the account balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (accountID_ != vault->at(sfOwner))
    {
        if (auto const ter =
                removeEmptyHolding(applyViewContext, accountID_, sharesRedeemed.asset(), j_);
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

    associateAsset(*vault, vaultAsset);

    auto const dstAcct = ctx_.tx[~sfDestination].value_or(accountID_);
    return doWithdraw(
        applyViewContext, accountID_, dstAcct, vaultAccount, preFeeBalance_, assetsWithdrawn, j_);
}

void
VaultWithdraw::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
