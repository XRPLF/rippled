#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {
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

[[nodiscard]] VaultKind
decodeVaultKind(std::optional<std::uint8_t> vaultKind)
{
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    return VaultKind::OpenEnded;
}

}  // namespace

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

    return accountSend(view, sender, vault->at(sfAccount), amount, j, {}, WaiveTransferFee::Yes);
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
    // On a final removal both fields are hard-reset to zero, so the amount
    // being withdrawn must equal the Vault's pre-mutation sfAssetsAvailable
    // — otherwise the caller would leave residual funds on the Vault's
    // pseudo-account after the Vault-level bookkeeping says the position
    // is fully unwound. VaultWithdraw enforces this by pinning
    // `assetsWithdrawn = allAvailable` immediately before setting
    // FinalRemoval::Yes. The equality is checked via STAmount so both
    // sides go through the same asset-precision normalization used by
    // VaultWithdraw when it built `allAvailable`.
    // Constructed outside the assert macro because unprotected commas
    // inside `{}` initializers are parsed as extra macro arguments.
    [[maybe_unused]] STAmount const availableSnapshot{asset, Number(vault->at(sfAssetsAvailable))};
    XRPL_ASSERT(
        finalRemoval == FinalRemoval::No || amount == availableSnapshot,
        "xrpl::removeVaultAssets : final removal amount equals sfAssetsAvailable");

    applyRemoveVaultAssets(ctx.view, vault, amount, finalRemoval);

    // The amount==0 short-circuit is defensive: every in-tree caller
    // (VaultWithdraw) computes `assetsWithdrawn` from a positive
    // sharesRedeemed and errors out earlier on a zero-share withdrawal, so
    // production traffic cannot reach this path with amount==0.
    // doWithdraw would otherwise still succeed for a zero amount, but
    // short-circuiting here makes the invariant explicit at the single
    // mutation point.
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

[[nodiscard]] VaultKind
getVaultKind(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultKind : valid Vault sle");
    return decodeVaultKind(vault->at(~sfVaultKind));
}

[[nodiscard]] VaultKind
getVaultKind(STTx const& tx)
{
    return decodeVaultKind(tx[~sfVaultKind]);
}

[[nodiscard]] bool
isValidVaultKind(STTx const& tx)
{
    auto const kindField = tx[~sfVaultKind];
    if (!kindField)
        return true;
    return *kindField == std::to_underlying(VaultKind::OpenEnded) ||
        *kindField == std::to_underlying(VaultKind::ClosedEnded);
}

[[nodiscard]] bool
isValidClosedEndedGap(std::uint32_t sub, std::uint32_t red)
{
    auto const s = static_cast<std::int64_t>(sub);
    auto const r = static_cast<std::int64_t>(red);
    return r >= s + kMinInvestmentPeriod && r < s + kMaxInvestmentPeriod;
}

[[nodiscard]] VaultPhase
getVaultPhase(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultPhase : valid Vault sle");
    return getVaultPhase(
        view, (*vault)[~sfVaultKind], (*vault)[~sfSubscriptionDate], (*vault)[~sfRedemptionDate]);
}

[[nodiscard]] VaultPhase
getVaultPhase(
    ReadView const& view,
    std::optional<std::uint8_t> vaultKind,
    std::optional<std::uint32_t> subscriptionDate,
    std::optional<std::uint32_t> redemptionDate)
{
    if (!vaultKind || *vaultKind != std::to_underlying(VaultKind::ClosedEnded))
        return VaultPhase::NoPhase;

    // Subscription includes now == SubscriptionDate; Investment starts
    // strictly after SubscriptionDate.
    if (!hasExpired(view, subscriptionDate, ExpiryComparison::Exclusive))
        return VaultPhase::Subscription;
    if (!hasExpired(view, redemptionDate))
        return VaultPhase::Investment;
    return VaultPhase::Redemption;
}

}  // namespace xrpl
