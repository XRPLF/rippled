#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
    {
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    shares = ((shareTotal * assets) / assetTotal).truncate();
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
    {
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    WaiveUnrealizedLoss waive)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= vault->at(sfLossUnrealized);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return shares;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / assetTotal;
    if (truncate == TruncateShares::Yes)
        result = result.truncate();
    shares = result;
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= vault->at(sfLossUnrealized);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] bool
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::const_ref issuance)
{
    XRPL_ASSERT(
        issuance && issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::isSoleShareholder : valid issuance SLE");

    std::uint64_t const outstanding = issuance->at(sfOutstandingAmount);
    if (outstanding == 0)
        return false;

    auto const shareMPTID =
        makeMptID(issuance->getFieldU32(sfSequence), issuance->getAccountID(sfIssuer));
    auto const sleToken = view.read(keylet::mptoken(shareMPTID, account));
    if (!sleToken)
        return false;  // LCOV_EXCL_LINE

    return sleToken->getFieldU64(sfMPTAmount) == outstanding;
}

[[nodiscard]] VaultVersion
getVaultVersion(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultVersion : valid Vault sle");
    if (!vault->isFieldPresent(sfLEVersion))
        return VaultVersion::Legacy;

    auto const version = vault->at(sfLEVersion);
    if (version > std::to_underlying(VaultVersion::CashBasis))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::getVaultVersion : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(version);
}

[[nodiscard]] int
getVaultScale(SLE::const_ref vault)
{
    if (!vault)
        return Number::kMinExponent - 1;  // LCOV_EXCL_LINE
    return scale(vault->at(sfAssetsTotal), vault->at(sfAsset));
}

[[nodiscard]] TER
addVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& sender,
    STAmount const& amount,
    STAmount const& valueDelta,
    beast::Journal j)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::addVaultAssets : valid Vault sle");

    [[maybe_unused]] Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(amount.asset() == asset, "xrpl::addVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        valueDelta.asset() == asset, "xrpl::addVaultAssets : valueDelta matches vault asset");

    XRPL_ASSERT(amount >= beast::kZero, "xrpl::addVaultAssets : amount is non-negative");

    // Callers are responsible for rounding amount/valueDelta to whatever
    // scale their own accounting requires; this helper does not re-round.
    // valueDelta and amount are independent (e.g. a loan default written off
    // entirely by the vault, with no first-loss capital cover, has a nonzero
    // (and possibly negative) valueDelta but a zero amount; late/regular loan
    // payments can also carry a small negative valueDelta from untracked
    // interest rounding corrections), so both fields are always updated even
    // when there is nothing to transfer.
    vault->at(sfAssetsTotal) += valueDelta;
    vault->at(sfAssetsAvailable) += amount;
    view.update(vault);

    if (auto const ter =
            accountSend(view, sender, vault->at(sfAccount), amount, j, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

namespace {

// Applies a full-removal mutation to the Vault's ledger fields: both
// callers (clawbackVaultAssets and removeVaultAssets) apply `amount` to
// sfAssetsTotal and sfAssetsAvailable equally (unlike
// addVaultAssets/moveVaultAssets, a full removal always shrinks both fields
// by the same amount). On a final removal, both fields are hard-reset to
// exactly zero rather than computed via subtraction: see FinalRemoval's
// doc comment for why an arithmetic subtraction cannot be trusted to land
// on exactly zero here.
void
applyRemoveVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    STAmount const& amount,
    FinalRemoval finalRemoval)
{
    if (finalRemoval == FinalRemoval::Yes)
    {
        vault->at(sfAssetsTotal) = 0;
        vault->at(sfAssetsAvailable) = 0;
    }
    else
    {
        vault->at(sfAssetsTotal) -= amount;
        vault->at(sfAssetsAvailable) -= amount;
    }
    view.update(vault);
}

}  // namespace

[[nodiscard]] TER
clawbackVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& recipient,
    STAmount const& amount,
    beast::Journal j)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::clawbackVaultAssets : valid Vault sle");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(amount.asset() == asset, "xrpl::clawbackVaultAssets : amount matches vault asset");
    XRPL_ASSERT(amount > beast::kZero, "xrpl::clawbackVaultAssets : amount is positive");

    if (amount > *vault->at(sfAssetsAvailable))
        return tefINTERNAL;

    applyRemoveVaultAssets(view, vault, amount, FinalRemoval::No);

    if (auto const ter = accountSend(
            view, vault->at(sfAccount), recipient, amount, j, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Sanity check
    if (accountHolds(
            view,
            vault->at(sfAccount),
            asset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j) < beast::kZero)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "clawbackVaultAssets: negative balance of vault assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
removeVaultAssets(
    ApplyViewContext ctx,
    SLE::ref vault,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j,
    FinalRemoval finalRemoval)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::removeVaultAssets : valid Vault sle");

    [[maybe_unused]] Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(amount.asset() == asset, "xrpl::removeVaultAssets : amount matches vault asset");
    XRPL_ASSERT(amount >= beast::kZero, "xrpl::removeVaultAssets : amount is non-negative");

    applyRemoveVaultAssets(ctx.view, vault, amount, finalRemoval);

    if (amount == beast::kZero)
        return tesSUCCESS;  // LCOV_EXCL_LINE

    return doWithdraw(ctx, senderAcct, dstAcct, vault->at(sfAccount), priorBalance, amount, j);
}

[[nodiscard]] TER
moveVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    MultiplePaymentDestinations const& recipients,
    STAmount const& valueDelta,
    beast::Journal j)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::moveVaultAssets : valid Vault sle");
    XRPL_ASSERT(recipients.size() > 1, "xrpl::moveVaultAssets : multiple recipients provided");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        valueDelta.asset() == asset, "xrpl::moveVaultAssets : valueDelta matches vault asset");
    XRPL_ASSERT(
        valueDelta == beast::kZero || getVaultVersion(vault) == VaultVersion::Legacy,
        "xrpl::moveVaultAssets : nonzero valueDelta requires Legacy vault version");

    Number amountTotal{};
    for (auto const& [recipient, recipientAmount] : recipients)
    {
        XRPL_ASSERT(
            recipientAmount >= beast::kZero,
            "xrpl::moveVaultAssets : recipientAmount is non-negative");
        amountTotal += recipientAmount;
    }
    STAmount const amount{asset, amountTotal};

    // valueDelta follows addVaultAssets's convention (added to sfAssetsTotal):
    // disbursing a loan typically increases sfAssetsTotal via accrued
    // interest even as cash leaves sfAssetsAvailable.

    vault->at(sfAssetsTotal) += valueDelta;
    vault->at(sfAssetsAvailable) -= amount;
    view.update(vault);

    if (amount == beast::kZero)
        return tesSUCCESS;

    return accountSendMulti(
        view, vault->at(sfAccount), asset, recipients, j, WaiveTransferFee::Yes);
}

namespace vault_dust {

namespace {

// Compute the exponent (scale) sfBalance on the Vault's custody line must
// remain representable at after this operation. This is the scale of
// sfAssetsTotal *after* applying `deltaToAssetsTotal`, using nearest-
// rounding so a same-magnitude jump does not oscillate the scale
// arbitrarily by a single ulp.
int
posteriorScale(SLE::const_ref vault, Number const& deltaToAssetsTotal)
{
    NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
    Number const posterior = Number{vault->at(sfAssetsTotal)} + deltaToAssetsTotal;
    return scale(posterior, vault->at(sfAsset));
}

// Promote whole quanta of dust stranded on the Vault's custody line back
// into sfBalance after a scale-refining accounting update, and move both
// sfAssetsAvailable and sfAssetsTotal by the same amount (receivable-
// preserving — this recognises deferred cash and creates no new
// receivable).
//
// A no-op when the custody line does not exist, when sfDust is already
// zero, or when the whole-quanta portion is zero at the Vault's current
// scale.
void
renormaliseStrandedDust(ApplyView& view, SLE::ref vault)
{
    Asset const asset = vault->at(sfAsset);
    AccountID const vaultAccount = vault->at(sfAccount);
    auto const line = view.peek(keylet::trustLine(vaultAccount, asset.get<Issue>()));
    if (!line || Number{line->at(sfDust)} == beast::kZero)
        return;

    bool const vaultIsHigh = vaultAccount > asset.getIssuer();
    // sfDust is stored in the line's low/high convention (same as
    // sfBalance); flip its sign so the truncation math below runs in the
    // Vault's terms.
    Number const dustInVaultTerms =
        vaultIsHigh ? -Number{line->at(sfDust)} : Number{line->at(sfDust)};

    int const targetScale = scale(Number{vault->at(sfAssetsTotal)}, asset);
    Number const movable =
        roundToAsset(asset, dustInVaultTerms, targetScale, Number::RoundingMode::Downward);
    if (movable == beast::kZero)
        return;

    Number const movableLineTerms = vaultIsHigh ? -movable : movable;
    STAmount const newBalance = line->getFieldAmount(sfBalance) + STAmount{asset, movableLineTerms};
    line->setFieldAmount(sfBalance, newBalance);
    line->at(sfDust) = Number{line->at(sfDust)} - movableLineTerms;
    view.update(line);

    vault->at(sfAssetsAvailable) += movable;
    vault->at(sfAssetsTotal) += movable;
    view.update(vault);
}

}  // namespace

[[nodiscard]] bool
useVaultDust(SLE::const_ref vault)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::vault_dust::useVaultDust : valid Vault sle");
    Asset const asset = vault->at(sfAsset);
    return getVaultVersion(vault) == VaultVersion::CashBasis && !asset.integral();
}

[[nodiscard]] TER
addVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& sender,
    STAmount const& amount,
    STAmount const& valueDelta,
    beast::Journal j)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::vault_dust::addVaultAssets : valid Vault sle");
    XRPL_ASSERT(
        useVaultDust(vault), "xrpl::vault_dust::addVaultAssets : useVaultDust precondition");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        amount.asset() == asset, "xrpl::vault_dust::addVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        valueDelta.asset() == asset,
        "xrpl::vault_dust::addVaultAssets : valueDelta matches vault asset");
    XRPL_ASSERT(
        amount >= beast::kZero, "xrpl::vault_dust::addVaultAssets : amount is non-negative");

    // Route the credit through a DustSplit targeting the Vault's
    // posterior scale (the scale implied by sfAssetsTotal + valueDelta):
    // any sub-quantum remainder on the custody line lands in sfDust
    // rather than being lost.
    DustSplit split(posteriorScale(vault, Number{valueDelta}));
    if (auto const ter = accountSend(
            view,
            sender,
            vault->at(sfAccount),
            amount,
            j,
            {},
            WaiveTransferFee::Yes,
            AllowMPTOverflow::No,
            &split);
        !isTesSuccess(ter))
        return ter;

    // Apply the accounting correction so the receivable
    // (sfAssetsTotal - sfAssetsAvailable) matches what a dust-unaware
    // call would produce: sfAssetsAvailable moves by the aligned
    // balanceDelta, and sfAssetsTotal absorbs the sub-quantum residual so
    // (valueDelta - dustDelta) - balanceDelta == valueDelta - amount.
    vault->at(sfAssetsAvailable) += split.balanceDelta;
    vault->at(sfAssetsTotal) += Number{valueDelta} - split.dustDelta;
    view.update(vault);

    // A negative valueDelta could refine the Vault's scale and free
    // stranded whole quanta on the custody line; promote them so the
    // dust reservoir never lingers above one quantum.
    renormaliseStrandedDust(view, vault);

    return tesSUCCESS;
}

[[nodiscard]] TER
clawbackVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& recipient,
    STAmount const& amount,
    beast::Journal j)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT,
        "xrpl::vault_dust::clawbackVaultAssets : valid Vault sle");
    XRPL_ASSERT(
        useVaultDust(vault), "xrpl::vault_dust::clawbackVaultAssets : useVaultDust precondition");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        amount.asset() == asset,
        "xrpl::vault_dust::clawbackVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        amount > beast::kZero, "xrpl::vault_dust::clawbackVaultAssets : amount is positive");

    if (amount > *vault->at(sfAssetsAvailable))
        return tefINTERNAL;

    // Same field mutation as the base clawback: a full removal shrinks
    // sfAssetsTotal and sfAssetsAvailable equally.
    vault->at(sfAssetsTotal) -= amount;
    vault->at(sfAssetsAvailable) -= amount;
    view.update(vault);

    if (auto const ter = accountSend(
            view, vault->at(sfAccount), recipient, amount, j, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    if (accountHolds(
            view,
            vault->at(sfAccount),
            asset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j) < beast::kZero)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "vault_dust::clawbackVaultAssets: negative balance of vault assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // A clawback shrinks sfAssetsTotal, refining the Vault's scale;
    // promote any dust that is now representable.
    renormaliseStrandedDust(view, vault);

    return tesSUCCESS;
}

[[nodiscard]] TER
removeVaultAssets(
    ApplyViewContext ctx,
    SLE::ref vault,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j,
    FinalRemoval finalRemoval)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT,
        "xrpl::vault_dust::removeVaultAssets : valid Vault sle");
    XRPL_ASSERT(
        useVaultDust(vault), "xrpl::vault_dust::removeVaultAssets : useVaultDust precondition");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        amount.asset() == asset,
        "xrpl::vault_dust::removeVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        amount >= beast::kZero, "xrpl::vault_dust::removeVaultAssets : amount is non-negative");

    if (finalRemoval == FinalRemoval::Yes)
    {
        // Terminal branch: drain any residual sfDust on the custody line
        // into the outgoing transfer so the line ends with
        // sfBalance == 0 and sfDust == 0 — a precondition for the
        // deletion guards downstream to permit cleanup. There is no other
        // shareholder left to divide the reservoir with, so promote ALL
        // of sfDust (not just whole quanta) into sfBalance and send it
        // out along with `amount`. The custody line's own scale can
        // absorb the promoted value; STAmount rounding of the extended
        // credit stays within a fraction of one quantum, well below the
        // magnitudes that VaultRounding tests exercise.
        AccountID const vaultAccount = vault->at(sfAccount);
        STAmount effectiveAmount = amount;
        if (auto const line = ctx.view.peek(keylet::trustLine(vaultAccount, asset.get<Issue>()));
            line && Number{line->at(sfDust)} != beast::kZero)
        {
            bool const vaultIsHigh = vaultAccount > asset.getIssuer();
            Number const dustInVaultTerms =
                vaultIsHigh ? -Number{line->at(sfDust)} : Number{line->at(sfDust)};
            Number const dustLineTerms = vaultIsHigh ? -dustInVaultTerms : dustInVaultTerms;
            STAmount const newBalance =
                line->getFieldAmount(sfBalance) + STAmount{asset, dustLineTerms};
            line->setFieldAmount(sfBalance, newBalance);
            line->at(sfDust) = Number{0};
            ctx.view.update(line);

            effectiveAmount = amount + STAmount{asset, dustInVaultTerms};
        }

        // Hard-reset both accounting fields to exactly zero — the same
        // contract as the base helper's FinalRemoval::Yes branch.
        vault->at(sfAssetsTotal) = 0;
        vault->at(sfAssetsAvailable) = 0;
        ctx.view.update(vault);

        if (effectiveAmount == beast::kZero)
            return tesSUCCESS;

        return doWithdraw(ctx, senderAcct, dstAcct, vaultAccount, priorBalance, effectiveAmount, j);
    }

    // Non-terminal: same field mutation as the base helper (both fields
    // drop by `amount`), then a plain doWithdraw. No DustSplit on the
    // transfer itself: the split machinery re-splits the receiver's
    // line, and the sub-quantum remainder that matters here is on the
    // Vault's line (the sender), which the split does not touch.
    // Renormalisation afterwards catches any dust promoted by the scale
    // refinement.
    vault->at(sfAssetsTotal) -= amount;
    vault->at(sfAssetsAvailable) -= amount;
    ctx.view.update(vault);

    if (amount != beast::kZero)
    {
        if (auto const ter =
                doWithdraw(ctx, senderAcct, dstAcct, vault->at(sfAccount), priorBalance, amount, j);
            !isTesSuccess(ter))
            return ter;
    }

    renormaliseStrandedDust(ctx.view, vault);

    return tesSUCCESS;
}

[[nodiscard]] TER
moveVaultAssets(
    ApplyView& view,
    SLE::ref vault,
    MultiplePaymentDestinations const& recipients,
    STAmount const& valueDelta,
    beast::Journal j)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT,
        "xrpl::vault_dust::moveVaultAssets : valid Vault sle");
    XRPL_ASSERT(
        useVaultDust(vault), "xrpl::vault_dust::moveVaultAssets : useVaultDust precondition");
    XRPL_ASSERT(
        recipients.size() > 1, "xrpl::vault_dust::moveVaultAssets : multiple recipients provided");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        valueDelta.asset() == asset,
        "xrpl::vault_dust::moveVaultAssets : valueDelta matches vault asset");
    XRPL_ASSERT(
        valueDelta == beast::kZero || getVaultVersion(vault) == VaultVersion::Legacy,
        "xrpl::vault_dust::moveVaultAssets : nonzero valueDelta requires Legacy vault version");

    Number amountTotal{};
    for (auto const& [recipient, recipientAmount] : recipients)
    {
        XRPL_ASSERT(
            recipientAmount >= beast::kZero,
            "xrpl::vault_dust::moveVaultAssets : recipientAmount is non-negative");
        amountTotal += recipientAmount;
    }
    STAmount const amount{asset, amountTotal};

    // Same field mutation and transfer as the base helper: the Vault is
    // the sender in this multi-payment, so DustSplit (which re-splits
    // the receiver's line) does not apply here. Renormalisation
    // afterwards picks up any dust freed by the scale refinement.
    vault->at(sfAssetsTotal) += valueDelta;
    vault->at(sfAssetsAvailable) -= amount;
    view.update(vault);

    if (amount != beast::kZero)
    {
        if (auto const ter = accountSendMulti(
                view, vault->at(sfAccount), asset, recipients, j, WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
    }

    renormaliseStrandedDust(view, vault);

    return tesSUCCESS;
}

}  // namespace vault_dust

}  // namespace xrpl
