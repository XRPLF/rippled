#include <xrpl/tx/transactors/vault/VaultManagerClaim.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

namespace {

struct ManagerFees
{
    STAmount fee;
    Number performance;
    Number nav;
    std::int64_t shares = 0;
};

// The management fee on elapsed time plus the performance fee on share-price
// gain over the high-water mark, both rounded down.
ManagerFees
computeFees(ReadView const& view, SLE::const_ref vault, SLE const& manager)
{
    Asset const asset = vault->at(sfAsset);
    auto const issuance = view.read(keylet::mptokenIssuance(vault->at(sfShareMPTID)));
    std::int64_t const shares =
        issuance ? static_cast<std::int64_t>(issuance->at(sfOutstandingAmount)) : 0;
    Number const nav = vaultNetAssetValue(view, vault);
    auto const now = view.parentCloseTime().time_since_epoch().count();
    std::uint32_t const since = manager[~sfLastFeeTime].value_or(0);

    NumberRoundModeGuard const guard(Number::RoundingMode::Downward);
    Number management;
    if (since != 0 && now > since)
    {
        management = tenthBipsOfValue(nav, TenthBips32{manager[~sfManagementFeeRate].value_or(0)}) *
            Number{now - since} / Number{kSecondsInYear};
    }

    Number performance;
    Number const mark = manager[~sfHighWaterMark].value_or(Number{});
    if (shares > 0 && mark > Number{})
    {
        Number const price = nav / Number{shares};
        if (price > mark)
        {
            performance = tenthBipsOfValue(
                (price - mark) * Number{shares},
                TenthBips32{manager[~sfPerformanceFeeRate].value_or(0)});
        }
    }

    // Rounded down at the scale of the vault's assets, so the debit is exact.
    Number const fee = roundToAsset(
        asset,
        management + performance,
        scale(vault->at(sfAssetsTotal), asset),
        Number::RoundingMode::Downward);
    return {.fee = STAmount{asset, fee}, .performance = performance, .nav = nav, .shares = shares};
}

}  // namespace

NotTEC
VaultManagerClaim::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
VaultManagerClaim::preclaim(PreclaimContext const& ctx)
{
    auto const vaultID = ctx.tx[sfVaultID];
    auto const vault = ctx.view.read(keylet::vault(vaultID));
    auto const manager = ctx.view.read(keylet::vaultManager(vaultID));
    if (!vault || !manager)
        return tecNO_ENTRY;

    AccountID const account = ctx.tx[sfAccount];
    if (account != effectiveVaultRoles(ctx.view, manager).manager)
        return tecNO_PERMISSION;

    // A fee on NAV needs a price.
    if (auto const ter = checkVaultMarks(ctx.view, vault); !isTesSuccess(ter))
        return ter;

    Asset const asset = vault->at(sfAsset);
    AccountID const pseudo = vault->at(sfAccount);
    if (auto const ter = requireAuth(ctx.view, asset, account, AuthType::WeakAuth))
        return ter;
    if (!holdingExists(ctx.view, account, asset))
    {
        if (auto const ter = canAddHolding(ctx.view, asset); !isTesSuccess(ter))
            return ter;
    }
    if (auto const ter = checkWithdrawFreeze(ctx.view, pseudo, account, account, asset))
        return ter;
    return tesSUCCESS;
}

TER
VaultManagerClaim::doApply()
{
    auto const vaultID = ctx_.tx[sfVaultID];
    auto vault = view().peek(keylet::vault(vaultID));
    auto manager = view().peek(keylet::vaultManager(vaultID));
    if (!vault || !manager)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (auto const ter = applyDuePendingUpdate(view(), manager, vault, j_); !isTesSuccess(ter))
        return ter;

    auto const fees = computeFees(view(), vault, *manager);
    // Elapsed time is kept when cash is short, so waiting loses nothing.
    if (Number{fees.fee} > vault->at(sfAssetsAvailable))
        return tecINSUFFICIENT_FUNDS;

    Asset const asset = vault->at(sfAsset);
    manager->at(sfLastFeeTime) = view().parentCloseTime().time_since_epoch().count();
    // The mark moves to the price after the fee when a performance fee is
    // charged, or is set by the first claim that finds shares.
    if (fees.shares > 0 &&
        (fees.performance > Number{} || Number{manager->at(sfHighWaterMark)} == Number{}))
        manager->at(sfHighWaterMark) = (fees.nav - Number{fees.fee}) / Number{fees.shares};
    view().update(manager);
    associateAsset(*manager, asset);

    if (fees.fee == beast::kZero)
        return tesSUCCESS;

    vault->at(sfAssetsTotal) -= Number{fees.fee};
    vault->at(sfAssetsAvailable) -= Number{fees.fee};
    view().update(vault);
    associateAsset(*vault, asset);
    return doWithdraw(
        ctx_.getApplyViewContext(),
        accountID_,
        accountID_,
        vault->at(sfAccount),
        preFeeBalance_,
        fees.fee,
        j_);
}

void
VaultManagerClaim::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerClaim::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
