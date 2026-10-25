#include <xrpl/ledger/helpers/VaultManagerHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <optional>
#include <utility>

namespace xrpl {

TER
checkVaultRoles(ReadView const& view, SLE::const_ref vault, VaultRoles const& roles)
{
    if (roles.manager == roles.valuer || roles.manager == roles.guardian ||
        roles.valuer == roles.guardian)
        return tecROLE_CONFLICT;

    AccountID const owner = vault->at(sfOwner);
    if (roles.valuer == owner || roles.guardian == owner)
        return tecROLE_CONFLICT;

    if (roles.manager == owner && vaultHasLoanBroker(view, vault))
        return tecROLE_CONFLICT;

    return tesSUCCESS;
}

bool
vaultHasLoanBroker(ReadView const& view, SLE::const_ref vault)
{
    auto const root = keylet::ownerDir(vault->at(sfAccount)).key;
    SLE::const_pointer page;
    unsigned int index = 0;
    uint256 entry;
    for (bool more = cdirFirst(view, root, page, index, entry); more;
         more = cdirNext(view, root, page, index, entry))
    {
        if (auto const sle = view.read(keylet::unchecked(entry));
            sle && sle->getType() == ltLOAN_BROKER)
            return true;
    }
    return false;
}

namespace {

bool
pendingIsDue(ReadView const& view, SLE::const_ref manager)
{
    if (!manager->isFieldPresent(sfPendingUpdate))
        return false;
    return view.parentCloseTime().time_since_epoch().count() >= manager->at(sfPendingUntil);
}

template <class T>
void
copyIfPresent(STObject& dst, STObject const& src, TypedField<T> const& field)
{
    if (src.isFieldPresent(field))
        dst[field] = src[field];
}

STObject const*
findEntry(STArray const& mandate, Asset const& asset)
{
    for (auto const& entry : mandate)
    {
        if (entry[sfAsset] == asset)
            return &entry;
    }
    return nullptr;
}

}  // namespace

VaultRoles
effectiveVaultRoles(ReadView const& view, SLE::const_ref manager)
{
    VaultRoles roles{
        .manager = manager->at(sfManager),
        .valuer = manager->at(sfValuer),
        .guardian = manager->at(sfGuardian)};
    if (pendingIsDue(view, manager))
    {
        auto const& pending = manager->getFieldObject(sfPendingUpdate);
        roles.manager = pending[~sfManager].value_or(roles.manager);
        roles.valuer = pending[~sfValuer].value_or(roles.valuer);
        roles.guardian = pending[~sfGuardian].value_or(roles.guardian);
    }
    return roles;
}

TER
applyDuePendingUpdate(ApplyView& view, SLE::ref manager, SLE::ref vault, beast::Journal j)
{
    if (!pendingIsDue(view, manager))
        return tesSUCCESS;

    STObject const pending = manager->getFieldObject(sfPendingUpdate);
    copyIfPresent(*manager, pending, sfManager);
    copyIfPresent(*manager, pending, sfValuer);
    copyIfPresent(*manager, pending, sfGuardian);
    copyIfPresent(*manager, pending, sfDeployedMaximum);
    copyIfPresent(*manager, pending, sfCoverRateMinimum);
    copyIfPresent(*manager, pending, sfCoverRateLiquidation);
    copyIfPresent(*manager, pending, sfManagementFeeRate);
    copyIfPresent(*manager, pending, sfPerformanceFeeRate);
    copyIfPresent(*manager, pending, sfMarkMoveLimit);
    copyIfPresent(*manager, pending, sfMarkHaltPeriod);
    copyIfPresent(*manager, pending, sfMarkStaleAfter);
    copyIfPresent(*manager, pending, sfTimelock);
    if (pending.isFieldPresent(sfMandate))
        manager->setFieldArray(sfMandate, pending.getFieldArray(sfMandate));

    manager->makeFieldAbsent(sfPendingUpdate);
    manager->at(sfPendingUntil) = 0;
    view.update(manager);
    return tesSUCCESS;
}

bool
isLooseningMandate(STArray const& current, STArray const& proposed)
{
    for (auto const& entry : proposed)
    {
        auto const* old = findEntry(current, entry[sfAsset]);
        if (old == nullptr)
            return true;

        Number const oldMax = (*old)[~sfPositionMaximum].value_or(Number{});
        Number const newMax = entry[~sfPositionMaximum].value_or(Number{});
        if (oldMax != Number{} && (newMax == Number{} || newMax > oldMax))
            return true;

        auto const oldDomain = (*old)[~sfDomainID];
        if (oldDomain && entry[~sfDomainID] != oldDomain)
            return true;
    }
    return false;
}

bool
isLooseningField(SField const& field, STObject const& current, STObject const& proposed)
{
    if (field == sfDeployedMaximum)
        return proposed[sfDeployedMaximum] > current[~sfDeployedMaximum].value_or(Number{});
    if (field == sfManagementFeeRate)
        return proposed[sfManagementFeeRate] > current[~sfManagementFeeRate].value_or(0);

    auto const u32 = [&](SF_UINT32 const& f) {
        return std::pair{proposed[f], current[~f].value_or(0)};
    };
    for (auto const* f : {&sfMarkStaleAfter, &sfMarkMoveLimit, &sfPerformanceFeeRate})
    {
        if (field == *f)
        {
            auto const [next, now] = u32(*f);
            return next > now;
        }
    }
    for (auto const* f :
         {&sfMarkHaltPeriod, &sfCoverRateMinimum, &sfCoverRateLiquidation, &sfTimelock})
    {
        if (field == *f)
        {
            auto const [next, now] = u32(*f);
            return next < now;
        }
    }
    return false;
}

STAmount
positionHolding(ReadView const& view, SLE::const_ref vault, Asset const& asset)
{
    return accountHolds(
        view,
        vault->at(sfAccount),
        asset,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        beast::Journal{beast::Journal::getNullSink()});
}

Number
positionValue(ReadView const& view, SLE::const_ref vault, SLE::const_ref position)
{
    Asset const vaultAsset = vault->at(sfAsset);
    Number const holding = positionHolding(view, vault, position->at(sfAsset));
    NumberRoundModeGuard const guard(Number::RoundingMode::Downward);
    return STAmount{vaultAsset, holding * position->at(sfMarkPrice)};
}

Number
vaultPositionsValue(ReadView const& view, SLE::const_ref vault)
{
    if (!view.rules().enabled(featureManagedVault))
        return Number{};
    auto const manager = view.read(keylet::vaultManager(vault->key()));
    if (!manager)
        return Number{};

    Number total;
    for (auto const& entry : manager->getFieldArray(sfMandate))
    {
        if (auto const position = view.read(keylet::vaultPosition(vault->key(), entry[sfAsset])))
            total += positionValue(view, vault, position);
    }
    return total;
}

Number
minimumManagerCover(Number const& positionsValue, STObject const& manager)
{
    NumberRoundModeGuard const guard(Number::RoundingMode::Upward);
    return tenthBipsOfValue(positionsValue, TenthBips32{manager[~sfCoverRateMinimum].value_or(0)});
}

}  // namespace xrpl
