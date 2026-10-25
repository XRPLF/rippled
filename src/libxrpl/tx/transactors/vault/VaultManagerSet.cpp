#include <xrpl/tx/transactors/vault/VaultManagerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <set>

namespace xrpl {

namespace {

// Fields a VaultManagerSet without a flag may carry besides sfVaultID.
constexpr std::array<SField const*, 13> kConfigFields{
    &sfManager,
    &sfValuer,
    &sfGuardian,
    &sfMandate,
    &sfDeployedMaximum,
    &sfCoverRateMinimum,
    &sfCoverRateLiquidation,
    &sfManagementFeeRate,
    &sfPerformanceFeeRate,
    &sfMarkMoveLimit,
    &sfMarkHaltPeriod,
    &sfMarkStaleAfter,
    &sfTimelock};

// Fields a VaultManagerSet must carry when it creates the entry.
constexpr std::array<SField const*, 9> kCreateFields{
    &sfManager,
    &sfValuer,
    &sfGuardian,
    &sfMandate,
    &sfDeployedMaximum,
    &sfMarkMoveLimit,
    &sfMarkHaltPeriod,
    &sfMarkStaleAfter,
    &sfTimelock};

NotTEC
checkMandate(STArray const& mandate)
{
    if (mandate.empty() || mandate.size() > kMaxMandateEntries)
        return temMALFORMED;

    std::set<Asset> seen;
    for (auto const& entry : mandate)
    {
        if (entry.getFName() != sfMandateEntry || !entry.isFieldPresent(sfAsset))
            return temMALFORMED;
        if (!seen.insert(entry[sfAsset]).second)
            return temMALFORMED;
        if (entry[~sfMarkSource].value_or(0) != 0)
            return temMALFORMED;
        if (entry[~sfPositionMaximum].value_or(Number{}) < Number{})
            return temMALFORMED;
        if (auto const domain = entry[~sfDomainID]; domain && *domain == beast::kZero)
            return temMALFORMED;
    }
    return tesSUCCESS;
}

}  // namespace

std::uint32_t
VaultManagerSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultManagerSetMask;
}

NotTEC
VaultManagerSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfVaultID] == beast::kZero)
        return temMALFORMED;

    auto const flags = tx.getFlags() & tfUniversalMask;
    if ((flags & (flags - 1)) != 0)
        return temINVALID_FLAG;

    if (flags != 0)
    {
        for (auto const* field : kConfigFields)
        {
            if (tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        return tesSUCCESS;
    }

    for (auto const* field : {&sfManager, &sfValuer, &sfGuardian})
    {
        if (auto const id = tx[~*field]; id && *id == beast::kZero)
            return temMALFORMED;
    }

    if (tx.isFieldPresent(sfMandate))
    {
        if (auto const ter = checkMandate(tx.getFieldArray(sfMandate)); !isTesSuccess(ter))
            return ter;
    }

    if (tx[~sfDeployedMaximum].value_or(Number{}) < Number{})
        return temMALFORMED;
    if (auto const v = tx[~sfMarkMoveLimit]; v && (*v == 0 || *v > kTenthBipsPerUnity.value()))
        return temMALFORMED;
    for (auto const* field : {&sfMarkHaltPeriod, &sfMarkStaleAfter, &sfTimelock})
    {
        if (auto const v = tx[~*field]; v && *v == 0)
            return temMALFORMED;
    }
    if (tx[~sfManagementFeeRate].value_or(0) > lending::kMaxManagementFeeRate.value())
        return temMALFORMED;
    if (tx[~sfPerformanceFeeRate].value_or(0) > kTenthBipsPerUnity.value())
        return temMALFORMED;
    if (tx[~sfCoverRateMinimum].value_or(0) > lending::kMaxCoverRate.value())
        return temMALFORMED;
    if (tx[~sfCoverRateLiquidation].value_or(0) > lending::kMaxCoverRate.value())
        return temMALFORMED;

    return tesSUCCESS;
}

namespace {

// Ledger checks of a proposed mandate: no entry is the vault asset, and every
// entry's issuer or issuance exists.
TER
checkMandateAssets(ReadView const& view, STArray const& mandate, Asset const& vaultAsset)
{
    for (auto const& entry : mandate)
    {
        Asset const asset = entry[sfAsset];
        if (asset == vaultAsset)
            return tecWRONG_ASSET;
        if (asset.native())
            continue;
        if (asset.holds<Issue>())
        {
            if (!view.exists(keylet::account(asset.getIssuer())))
                return tecNO_ISSUER;
        }
        else if (!view.exists(keylet::mptokenIssuance(asset.get<MPTIssue>().getMptID())))
        {
            return tecOBJECT_NOT_FOUND;
        }
    }
    return tesSUCCESS;
}

}  // namespace

TER
VaultManagerSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const vaultID = tx[sfVaultID];
    auto const vault = ctx.view.read(keylet::vault(vaultID));
    if (!vault)
        return tecNO_ENTRY;

    auto const manager = ctx.view.read(keylet::vaultManager(vaultID));
    if (auto const flags = tx.getFlags() & tfUniversalMask; flags != 0)
    {
        if (!manager)
            return tecNO_ENTRY;
        auto const roles = effectiveVaultRoles(ctx.view, manager);
        AccountID const allowed =
            (flags & tfCancelPending) != 0 ? AccountID{vault->at(sfOwner)} : roles.guardian;
        if (tx[sfAccount] != allowed)
            return tecNO_PERMISSION;
        if ((flags & (tfVetoPending | tfCancelPending)) != 0)
        {
            // A pending update at or past sfPendingUntil has taken effect.
            bool const waiting = manager->isFieldPresent(sfPendingUpdate) &&
                ctx.view.parentCloseTime().time_since_epoch().count() < manager->at(sfPendingUntil);
            if (!waiting)
                return tecNO_ENTRY;
        }
        return tesSUCCESS;
    }

    if (tx[sfAccount] != vault->at(sfOwner))
        return tecNO_PERMISSION;

    auto const kind = getVaultKind(vault);
    if (kind != VaultKind::ClosedEnded && kind != VaultKind::Rolling)
        return tecNO_PERMISSION;

    Asset const vaultAsset = vault->at(sfAsset);
    if (manager)
    {
        if (tx.isFieldPresent(sfMandate))
        {
            auto const& proposed = tx.getFieldArray(sfMandate);
            if (auto const ter = checkMandateAssets(ctx.view, proposed, vaultAsset);
                !isTesSuccess(ter))
                return ter;
            // A position leaves only by a trade, so its entry stays until then.
            for (auto const& entry : manager->getFieldArray(sfMandate))
            {
                Asset const asset = entry[sfAsset];
                bool const kept = std::ranges::any_of(
                    proposed, [&](STObject const& e) { return e[sfAsset] == asset; });
                if (!kept && ctx.view.exists(keylet::vaultPosition(vaultID, asset)))
                    return tecHAS_OBLIGATIONS;
            }
        }
        if (tx.isFieldPresent(sfManager) || tx.isFieldPresent(sfValuer) ||
            tx.isFieldPresent(sfGuardian))
        {
            auto roles = effectiveVaultRoles(ctx.view, manager);
            roles.manager = tx[~sfManager].value_or(roles.manager);
            roles.valuer = tx[~sfValuer].value_or(roles.valuer);
            roles.guardian = tx[~sfGuardian].value_or(roles.guardian);
            if (auto const ter = checkVaultRoles(ctx.view, vault, roles); !isTesSuccess(ter))
                return ter;
        }
        return tesSUCCESS;
    }

    for (auto const* field : kCreateFields)
    {
        if (!tx.isFieldPresent(*field))
            return temMALFORMED;
    }

    if (auto const ter = checkMandateAssets(ctx.view, tx.getFieldArray(sfMandate), vaultAsset);
        !isTesSuccess(ter))
        return ter;

    if (auto const ter = checkVaultRoles(
            ctx.view,
            vault,
            {.manager = tx[sfManager], .valuer = tx[sfValuer], .guardian = tx[sfGuardian]});
        !isTesSuccess(ter))
        return ter;

    return canAddHolding(ctx.view, vaultAsset);
}

TER
VaultManagerSet::applyFlag(std::uint32_t flags, SLE::ref sle, SLE::ref vault)
{
    auto& view = ctx_.view();
    if (auto const ter = applyDuePendingUpdate(view, sle, vault, j_); !isTesSuccess(ter))
        return ter;

    if ((flags & tfHalt) != 0)
    {
        // A halt only ever moves forward.
        std::uint32_t const until =
            view.parentCloseTime().time_since_epoch().count() + sle->at(sfMarkHaltPeriod);
        if (until > vault->at(sfMarkHaltUntil))
        {
            vault->at(sfMarkHaltUntil) = until;
            view.update(vault);
        }
        return tesSUCCESS;
    }

    sle->makeFieldAbsent(sfPendingUpdate);
    sle->at(sfPendingUntil) = 0;
    view.update(sle);
    return tesSUCCESS;
}

TER
VaultManagerSet::update(SLE::ref sle, SLE::ref vault)
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    if (auto const ter = applyDuePendingUpdate(view, sle, vault, j_); !isTesSuccess(ter))
        return ter;

    // A tightening applies now; a loosening or role change goes to one new
    // pending update that replaces any earlier one.
    STObject pending = STObject::makeInnerObject(sfPendingUpdate);
    bool anyPending = false;

    for (auto const* role : {&sfManager, &sfValuer, &sfGuardian})
    {
        if (auto const id = tx[~*role]; id && *id != sle->at(*role))
        {
            pending[*role] = *id;
            anyPending = true;
        }
    }

    auto const scalar = [&]<class T>(TypedField<T> const& field) {
        if (!tx.isFieldPresent(field))
            return;
        if (isLooseningField(field, *sle, tx))
        {
            pending[field] = tx[field];
            anyPending = true;
        }
        else
        {
            sle->at(field) = tx[field];
        }
    };
    scalar(sfDeployedMaximum);
    scalar(sfCoverRateMinimum);
    scalar(sfCoverRateLiquidation);
    scalar(sfManagementFeeRate);
    scalar(sfPerformanceFeeRate);
    scalar(sfMarkMoveLimit);
    scalar(sfMarkHaltPeriod);
    scalar(sfMarkStaleAfter);
    scalar(sfTimelock);

    if (tx.isFieldPresent(sfMandate))
    {
        auto const& proposed = tx.getFieldArray(sfMandate);
        if (isLooseningMandate(sle->getFieldArray(sfMandate), proposed))
        {
            pending.setFieldArray(sfMandate, proposed);
            anyPending = true;
        }
        else
        {
            sle->setFieldArray(sfMandate, proposed);
            // The latest full mandate supersedes a waiting one.
            if (sle->isFieldPresent(sfPendingUpdate))
            {
                auto& waiting = sle->peekFieldObject(sfPendingUpdate);
                waiting.makeFieldAbsent(sfMandate);
                if (waiting.empty())
                {
                    sle->makeFieldAbsent(sfPendingUpdate);
                    sle->at(sfPendingUntil) = 0;
                }
            }
        }
    }

    if (anyPending)
    {
        sle->setFieldObject(sfPendingUpdate, pending);
        sle->at(sfPendingUntil) =
            view.parentCloseTime().time_since_epoch().count() + sle->at(sfTimelock);
    }

    view.update(sle);
    associateAsset(*sle, vault->at(sfAsset));
    return tesSUCCESS;
}

TER
VaultManagerSet::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();
    auto const vaultID = tx[sfVaultID];

    if (auto sle = view.peek(keylet::vaultManager(vaultID)))
    {
        auto vaultSle = view.peek(keylet::vault(vaultID));
        if (!vaultSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        if (auto const flags = tx.getFlags() & tfUniversalMask; flags != 0)
            return applyFlag(flags, sle, vaultSle);
        return update(sle, vaultSle);
    }

    auto const vault = view.read(keylet::vault(vaultID));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto owner = view.peek(keylet::account(accountID_));
    if (!owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const vaultAsset = vault->at(sfAsset);
    auto sle = std::make_shared<SLE>(keylet::vaultManager(vaultID));
    if (auto const ter = dirLink(view, accountID_, sle))
        return ter;  // LCOV_EXCL_LINE

    // One increment for the entry and one for its pseudo-account.
    increaseOwnerCount(view, owner, {}, 2, j_);
    if (preFeeBalance_ < accountReserve(view, owner, j_))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view, sle->key(), sfVaultManagerID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE
    AccountID const pseudoId = (*maybePseudo)->at(sfAccount);

    if (auto const ter =
            addEmptyHolding(ctx_.getApplyViewContext(), pseudoId, preFeeBalance_, vaultAsset, j_))
        return ter;

    sle->at(sfVaultID) = vaultID;
    sle->at(sfAccount) = pseudoId;
    sle->at(sfManager) = tx[sfManager];
    sle->at(sfValuer) = tx[sfValuer];
    sle->at(sfGuardian) = tx[sfGuardian];
    sle->setFieldArray(sfMandate, tx.getFieldArray(sfMandate));
    sle->at(sfDeployedMaximum) = tx[sfDeployedMaximum];
    sle->at(sfMarkMoveLimit) = tx[sfMarkMoveLimit];
    sle->at(sfMarkHaltPeriod) = tx[sfMarkHaltPeriod];
    sle->at(sfMarkStaleAfter) = tx[sfMarkStaleAfter];
    sle->at(sfTimelock) = tx[sfTimelock];
    if (auto const v = tx[~sfCoverRateMinimum])
        sle->at(sfCoverRateMinimum) = *v;
    if (auto const v = tx[~sfCoverRateLiquidation])
        sle->at(sfCoverRateLiquidation) = *v;
    if (auto const v = tx[~sfManagementFeeRate])
        sle->at(sfManagementFeeRate) = *v;
    if (auto const v = tx[~sfPerformanceFeeRate])
        sle->at(sfPerformanceFeeRate) = *v;
    sle->at(sfLastFeeTime) = view.parentCloseTime().time_since_epoch().count();

    view.insert(sle);
    associateAsset(*sle, vaultAsset);
    return tesSUCCESS;
}

void
VaultManagerSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
