#include <xrpl/tx/transactors/vault/VaultWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
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
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

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
VaultWithdraw::visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    data_.visitEntry(isDelete, before, after);
}

bool
VaultWithdraw::finalizeInvariants(
    STTx const& tx,
    TER result,
    XRPAmount fee,
    ReadView const& view,
    beast::Journal const& j)
{
    static constexpr Number kZero{};

    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(result))
        return true;

    auto const& afterVaults = data_.afterVaults();
    if (afterVaults.empty())
        return true;

    auto const& afterVault = afterVaults[0];
    auto const& vaultAsset = afterVault.asset;

    XRPL_ASSERT(
        !data_.beforeVaults().empty(),
        "xrpl::VaultWithdraw::finalizeInvariants : withdrawal updated a vault");
    auto const& beforeVault = data_.beforeVaults()[0];

    auto const maybeVaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
    if (!maybeVaultDeltaAssets)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault balance";
        XRPL_ASSERT(enforce, "xrpl::VaultWithdraw::finalizeInvariants : withdrawal changed vault");
        return !enforce;
    }

    // Get the posterior scale to round calculations to
    auto const minScale = data_.computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

    auto const vaultPseudoDeltaAssets =
        roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);

    bool result_ = true;

    if (vaultPseudoDeltaAssets >= kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal must decrease vault balance";
        result_ = false;
    }

    // Any payments (including withdrawal) going to the issuer
    // do not change their balance, but destroy funds instead.
    bool const issuerWithdrawal = [&]() -> bool {
        if (vaultAsset.native())
            return false;
        auto const destination = tx[~sfDestination].value_or(tx[sfAccount]);
        return destination == vaultAsset.getIssuer();
    }();

    if (!issuerWithdrawal)
    {
        auto const maybeAccDelta = data_.deltaAssetsTxAccount(tx, fee);
        auto const maybeOtherAccDelta = [&]() -> std::optional<VaultInvariantData::DeltaInfo> {
            if (auto const destination = tx[~sfDestination];
                destination && *destination != tx[sfAccount])
            {
                return data_.deltaAssets(*destination);
            }
            return std::nullopt;
        }();

        if (maybeAccDelta.has_value() == maybeOtherAccDelta.has_value())
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: withdrawal must change one destination balance";
            XRPL_ASSERT(
                enforce,
                "xrpl::VaultWithdraw::finalizeInvariants : withdrawal changed one destination");
            return !enforce;
        }

        auto const destinationDelta =  //
            maybeAccDelta ? *maybeAccDelta : *maybeOtherAccDelta;

        // the scale of destinationDelta can be coarser than
        // minScale, so we take that into account when rounding
        auto const destinationScale = VaultInvariantData::computeCoarsestScale({destinationDelta});
        auto const localMinScale = std::max(minScale, destinationScale);

        auto const roundedDestinationDelta =
            roundToAsset(vaultAsset, destinationDelta.delta, localMinScale);

        // Post-fixCleanup3_2_0: Tolerate zero-rounded destination deltas for IOUs only.
        // If the receiver's trust line sits at a coarser scale, the inflow may
        // safely round down to zero.
        //
        // XRP and MPT remain strict. Because they are integer-exact, a zero
        // destination delta indicates a true accounting bug, not a rounding artifact.
        bool const tolerateZeroDelta =
            view.rules().enabled(fixCleanup3_2_0) && !vaultAsset.integral();
        auto const invalidBalanceChange =
            tolerateZeroDelta ? roundedDestinationDelta < kZero : roundedDestinationDelta <= kZero;
        if (invalidBalanceChange)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: withdrawal must increase destination balance";
            result_ = false;
        }

        auto const localPseudoDeltaAssets =
            roundToAsset(vaultAsset, vaultPseudoDeltaAssets, localMinScale);
        // For IOU assets near a precision boundary the destination's STAmount
        // exponent can shift, making part of the sent value unrepresentable at the
        // receiver's new scale — that portion is irreversibly absorbed by the IOU
        // rail.  Tolerate the mismatch only when the destroyed amount (vault outflow
        // minus destination inflow, in Number space) is itself sub-ULP at the
        // destination's scale.  Floor rounding is used so that values exactly at the
        // step boundary are not mistakenly dismissed.  Any representable discrepancy
        // indicates a real accounting bug and must be caught.
        auto const destroyedIsSubUlp = tolerateZeroDelta &&
            roundToAsset(
                vaultAsset,
                maybeVaultDeltaAssets->delta * -1 - destinationDelta.delta,
                destinationScale,
                Number::RoundingMode::Downward) == kZero;
        if (!destroyedIsSubUlp && localPseudoDeltaAssets * -1 != roundedDestinationDelta)
        {
            JLOG(j.fatal()) << "Invariant failed: " <<  //
                "withdrawal must change vault and destination balance by equal amount";
            result_ = false;
        }
    }

    // We don't round shares, they are integral MPT
    auto const accountDeltaShares = data_.deltaShares(tx[sfAccount]);
    if (!accountDeltaShares)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal must change depositor shares";
        XRPL_ASSERT(
            enforce,
            "xrpl::VaultWithdraw::finalizeInvariants : withdrawal changed depositor shares");
        return !enforce;
    }

    if (accountDeltaShares->delta >= kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal must decrease depositor shares";
        result_ = false;
    }

    // We don't round shares, they are integral MPT
    auto const vaultDeltaShares = data_.deltaShares(afterVault.pseudoId);
    if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault shares";
        XRPL_ASSERT(
            enforce, "xrpl::VaultWithdraw::finalizeInvariants : withdrawal changed vault shares");
        return !enforce;
    }

    if (vaultDeltaShares->delta * -1 != accountDeltaShares->delta)
    {
        JLOG(j.fatal()) << "Invariant failed: " <<  //
            "withdrawal must change depositor and vault shares by equal amount";
        result_ = false;
    }

    auto const assetTotalDelta =
        roundToAsset(vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
    // Note, vaultPseudoDeltaAssets is negative (see check above)
    if (assetTotalDelta != vaultPseudoDeltaAssets)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal and assets outstanding must add up";
        result_ = false;
    }

    auto const assetAvailableDelta = roundToAsset(
        vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);

    if (assetAvailableDelta != vaultPseudoDeltaAssets)
    {
        JLOG(j.fatal()) << "Invariant failed: withdrawal and assets available must add up";
        result_ = false;
    }

    if (!result_)
    {
        XRPL_ASSERT(enforce, "xrpl::VaultWithdraw::finalizeInvariants : vault invariants");
        return !enforce;
    }

    return true;
}

}  // namespace xrpl
