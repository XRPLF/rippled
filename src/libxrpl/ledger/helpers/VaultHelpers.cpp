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

    // Forward to the dust-aware overlay for eligible Vaults (cash-basis +
    // IOU asset). Every other Vault runs the base body verbatim below,
    // byte-identical to a call with no overlay in the tree.
    if (vault_dust::useVaultDust(view, vault))
        return vault_dust::addVaultAssets(view, vault, sender, amount, valueDelta, j);

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

    if (vault_dust::useVaultDust(view, vault))
        return vault_dust::clawbackVaultAssets(view, vault, recipient, amount, j);

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

    if (vault_dust::useVaultDust(ctx.view, vault))
        return vault_dust::removeVaultAssets(
            ctx, vault, senderAcct, dstAcct, priorBalance, amount, j, finalRemoval);

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

    if (vault_dust::useVaultDust(view, vault))
        return vault_dust::moveVaultAssets(view, vault, recipients, valueDelta, j);

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

// Build a sender-leg Override DustSplit targeting the Vault's current
// (post-mutation) sfAssetsTotal scale. Shared by clawback, non-terminal
// withdraw, and multi-recipient move — every sender-leg dust-aware call
// path in vault_dust uses the same shape.
DustSplit
makeSenderOverride(SLE::const_ref vault, Asset const& asset)
{
    DustSplit split;
    split.sender = DustSplit::LegPolicy{
        .mode = DustSplit::LegPolicy::Mode::Override,
        .overrideScale = scale(Number{vault->at(sfAssetsTotal)}, asset)};
    return split;
}

// Reconcile Vault fields against a sender-leg dust report. `dustDelta` is
// the change in the custody line's sfDust (sender-positive: positive when
// dust was newly deferred, negative when previously-deferred dust was
// promoted into sfBalance). Both Vault fields shift by that delta so the
// receivable (sfAssetsTotal - sfAssetsAvailable) stays aligned with the
// line's newDust exactly. No-op when no sender-leg policy ran.
void
reconcileSenderDust(ApplyView& view, SLE::ref vault, DustSplit const& split)
{
    if (!split.sender)
        return;
    vault->at(sfAssetsAvailable) -= split.sender->dustDelta;
    vault->at(sfAssetsTotal) -= split.sender->dustDelta;
    view.update(vault);
}

}  // namespace

[[nodiscard]] bool
useVaultDust(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::vault_dust::useVaultDust : valid Vault sle");
    // Amendment gate is defense-in-depth; see directSendNoFeeIOU
    // (TokenHelpers.cpp) for the canonical rationale.
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return false;
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
        useVaultDust(view, vault), "xrpl::vault_dust::addVaultAssets : useVaultDust precondition");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        amount.asset() == asset, "xrpl::vault_dust::addVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        valueDelta.asset() == asset,
        "xrpl::vault_dust::addVaultAssets : valueDelta matches vault asset");
    XRPL_ASSERT(
        amount >= beast::kZero, "xrpl::vault_dust::addVaultAssets : amount is non-negative");

    // Route the credit through a DustSplit's receiver-leg policy
    // targeting the Vault's posterior scale (the scale implied by
    // sfAssetsTotal + valueDelta): any sub-quantum remainder on the
    // Vault's custody line lands in sfDust rather than being lost. The
    // sender's line is written by the pre-credit debit leg and does not
    // participate in the split — a depositor's own trust line is
    // dust-unaware.
    DustSplit split;
    split.receiver = DustSplit::LegPolicy{
        .mode = DustSplit::LegPolicy::Mode::Override,
        .overrideScale = posteriorScale(vault, Number{valueDelta})};
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
    // receiver-leg deltas are receiver-positive, so we ADD them
    // directly to the Vault's fields (the Vault IS the receiver here).
    //
    // Any dust stranded on the custody line by a scale-refining prior
    // operation is renormalised by the credit-path re-split itself
    // (`directSendNoFeeIOU` truncates the extended balance +
    // credit at the Override target scale, so a decade-boundary crossing
    // automatically promotes freed whole-quanta into sfBalance). No
    // separate renormaliseStrandedDust pass is needed here.
    vault->at(sfAssetsAvailable) += split.receiver->balanceDelta;
    vault->at(sfAssetsTotal) += Number{valueDelta} - split.receiver->dustDelta;
    view.update(vault);

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
        useVaultDust(view, vault),
        "xrpl::vault_dust::clawbackVaultAssets : useVaultDust precondition");

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

    // A clawback shrinks sfAssetsTotal, refining the Vault's scale, so
    // any stranded whole quanta on the custody line's sfDust need to
    // be promoted back into sfBalance. Drive this through a sender-leg
    // Override policy targeting the Vault's posterior scale; the trust
    // -line layer re-splits (sfBalance, sfDust) at that scale and
    // reports any dust promotion or newly-deferred residual.
    DustSplit split = makeSenderOverride(vault, asset);

    if (auto const ter = accountSend(
            view,
            vault->at(sfAccount),
            recipient,
            amount,
            j,
            {},
            WaiveTransferFee::Yes,
            AllowMPTOverflow::No,
            &split);
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

    reconcileSenderDust(view, vault, split);

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
        useVaultDust(ctx.view, vault),
        "xrpl::vault_dust::removeVaultAssets : useVaultDust precondition");

    Asset const asset = vault->at(sfAsset);
    XRPL_ASSERT(
        amount.asset() == asset,
        "xrpl::vault_dust::removeVaultAssets : amount matches vault asset");
    XRPL_ASSERT(
        amount >= beast::kZero, "xrpl::vault_dust::removeVaultAssets : amount is non-negative");

    if (finalRemoval == FinalRemoval::Yes)
    {
        // Terminal branch: drive the drain through a sender-leg Drain
        // policy on the outgoing withdrawal. The trust-line layer folds
        // sfDust into the sender's sfBalance in place, inflates the
        // outgoing amount so the destination receives `amount + dust`,
        // and zeroes sfDust — leaving the custody line ready for
        // downstream deletion guards.
        AccountID const vaultAccount = vault->at(sfAccount);

        DustSplit split;
        split.sender =
            DustSplit::LegPolicy{.mode = DustSplit::LegPolicy::Mode::Drain, .overrideScale = 0};

        // Hard-reset both accounting fields to exactly zero — the same
        // contract as the base helper's FinalRemoval::Yes branch.
        vault->at(sfAssetsTotal) = 0;
        vault->at(sfAssetsAvailable) = 0;
        ctx.view.update(vault);

        // Short-circuit only when both the whole-quanta amount and the
        // sub-quantum reservoir on the custody line are zero. The
        // dust-inclusive read helper collapses sfBalance + sfDust; when
        // amount==0 the sfBalance component is also zero, so a zero
        // extended balance means "nothing to drain".
        if (amount == beast::kZero &&
            creditBalanceExact(ctx.view, vaultAccount, asset.get<Issue>()) == beast::kZero)
            return tesSUCCESS;

        return doWithdraw(ctx, senderAcct, dstAcct, vaultAccount, priorBalance, amount, j, &split);
    }

    // Non-terminal: same field mutation as the base helper (both fields
    // drop by `amount`), then a dust-aware doWithdraw driven by a
    // sender-leg Override policy targeting the Vault's posterior
    // scale. The trust-line layer re-splits (sfBalance, sfDust) on the
    // custody line at the new scale and reports back any promoted /
    // newly-deferred sub-quantum residual so the Vault's fields stay
    // aligned.
    vault->at(sfAssetsTotal) -= amount;
    vault->at(sfAssetsAvailable) -= amount;
    ctx.view.update(vault);

    if (amount != beast::kZero)
    {
        DustSplit split = makeSenderOverride(vault, asset);

        if (auto const ter = doWithdraw(
                ctx, senderAcct, dstAcct, vault->at(sfAccount), priorBalance, amount, j, &split);
            !isTesSuccess(ter))
            return ter;

        reconcileSenderDust(ctx.view, vault, split);
    }

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
        useVaultDust(view, vault), "xrpl::vault_dust::moveVaultAssets : useVaultDust precondition");
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

    // Field mutations first (same as the base helper), then the multi-
    // send with a sender-leg Override policy so any dust freed by the
    // scale refinement (from valueDelta / amount) surfaces via the
    // trust-line layer. accountSendMulti applies the sender-leg
    // policy on the single shared sender-line debit; the multiple
    // receiver-line credits are dust-unaware (the trust-line layer
    // has no receiver-leg policy plumbed through the multi path).
    vault->at(sfAssetsTotal) += valueDelta;
    vault->at(sfAssetsAvailable) -= amount;
    view.update(vault);

    if (amount != beast::kZero)
    {
        DustSplit split = makeSenderOverride(vault, asset);

        if (auto const ter = accountSendMulti(
                view, vault->at(sfAccount), asset, recipients, j, WaiveTransferFee::Yes, &split);
            !isTesSuccess(ter))
            return ter;

        reconcileSenderDust(view, vault, split);
    }

    return tesSUCCESS;
}

}  // namespace vault_dust

}  // namespace xrpl
