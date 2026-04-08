#include <xrpl/protocol/Permissions.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace xrpl {

Permission::Permission()
{
    txFeatureMap_ = {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegable, amendment, ...) {value, amendment},

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
    };

    delegableTx_ = {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegable, ...) {value, delegable},

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
    };

    granularPermissionMap_ = {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields) {#type, type},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    };

    granularNameMap_ = {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields) {type, #type},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    };

    granularTxTypeMap_ = {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields) {type, txType},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    };

    {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields) \
    granularPermittedFlags_[type] = (flags);

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    }

    {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields) \
    granularTemplates_.emplace(                                 \
        std::piecewise_construct,                               \
        std::forward_as_tuple(type),                            \
        std::forward_as_tuple(std::vector<SOElement> fields, TxFormats::getCommonFields()));

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    }

    XRPL_ASSERT(
        txFeatureMap_.size() == delegableTx_.size(),
        "xrpl::Permission : txFeatureMap_ and delegableTx_ must have same "
        "size");

    for ([[maybe_unused]] auto const& permission : granularPermissionMap_)
    {
        XRPL_ASSERT(
            permission.second > UINT16_MAX,
            "xrpl::Permission::granularPermissionMap_ : granular permission "
            "value must not exceed the maximum uint16_t value.");
    }

    for (auto const& [gp, txType] : granularTxTypeMap_)
        granularTxTypes_.insert(txType);
}

Permission const&
Permission::getInstance()
{
    static Permission const instance;
    return instance;
}

std::optional<std::string>
Permission::getPermissionName(std::uint32_t const value) const
{
    auto const permissionValue = static_cast<GranularPermissionType>(value);
    if (auto const granular = getGranularName(permissionValue))
        return granular;

    // not a granular permission, check if it maps to a transaction type
    auto const txType = permissionToTxType(value);
    if (auto const* item = TxFormats::getInstance().findByType(txType); item != nullptr)
        return item->getName();

    return std::nullopt;
}

std::optional<std::uint32_t>
Permission::getGranularValue(std::string const& name) const
{
    auto const it = granularPermissionMap_.find(name);
    if (it != granularPermissionMap_.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

std::optional<std::string>
Permission::getGranularName(GranularPermissionType const& value) const
{
    auto const it = granularNameMap_.find(value);
    if (it != granularNameMap_.end())
        return it->second;

    return std::nullopt;
}

std::optional<TxType>
Permission::getGranularTxType(GranularPermissionType const& gpType) const
{
    auto const it = granularTxTypeMap_.find(gpType);
    if (it != granularTxTypeMap_.end())
        return it->second;

    return std::nullopt;
}

bool
Permission::hasGranularPermissions(TxType txType) const
{
    return granularTxTypes_.contains(txType);
}

std::optional<std::reference_wrapper<uint256 const>>
Permission::getTxFeature(TxType txType) const
{
    auto const txFeaturesIt = txFeatureMap_.find(txType);
    XRPL_ASSERT(
        txFeaturesIt != txFeatureMap_.end(),
        "xrpl::Permissions::getTxFeature : tx exists in txFeatureMap_");

    if (txFeaturesIt->second == uint256{})
        return std::nullopt;
    return txFeaturesIt->second;
}

bool
Permission::isDelegable(std::uint32_t const& permissionValue, Rules const& rules) const
{
    auto const granularPermission =
        getGranularName(static_cast<GranularPermissionType>(permissionValue));
    if (granularPermission)
    {
        // granular permissions are always allowed to be delegated
        return true;
    }

    auto const txType = permissionToTxType(permissionValue);
    auto const it = delegableTx_.find(txType);

    if (it == delegableTx_.end())
        return false;

    auto const txFeaturesIt = txFeatureMap_.find(txType);
    XRPL_ASSERT(
        txFeaturesIt != txFeatureMap_.end(),
        "xrpl::Permissions::isDelegable : tx exists in txFeatureMap_");

    // Delegation is only allowed if the required amendment for the transaction
    // is enabled. For transactions that do not require an amendment, delegation
    // is always allowed.
    if (txFeaturesIt->second != uint256{} && !rules.enabled(txFeaturesIt->second))
        return false;

    if (it->second == Delegation::notDelegable)
        return false;

    return true;
}

uint32_t
Permission::txToPermissionType(TxType const& type)
{
    return static_cast<uint32_t>(type) + 1;
}

TxType
Permission::permissionToTxType(uint32_t const& value)
{
    return static_cast<TxType>(value - 1);
}

bool
Permission::checkGranularSandbox(
    STTx const& tx,
    std::unordered_set<GranularPermissionType> const& heldPermissions) const
{
    auto const txFlags = tx.getFlags();

    // Build union of flags and templates across all held permissions.
    std::uint32_t unionFlags = 0;
    for (auto const& gp : heldPermissions)
    {
        auto const it = granularPermittedFlags_.find(gp);
        if (it != granularPermittedFlags_.end())
            unionFlags |= it->second;
    }

    // Check if flags are permitted
    if ((txFlags & ~unionFlags) != 0)
        return false;

    // Check if fields are permitted. Every present field must appear in at least one held
    // permission's template. The common fields are included in the constructor.
    for (auto const& field : tx)
    {
        if (field.getSType() == STI_NOTPRESENT)
            continue;

        bool fieldAllowed = false;
        for (auto const& gp : heldPermissions)
        {
            auto const it = granularTemplates_.find(gp);
            if (it != granularTemplates_.end() && it->second.getIndex(field.getFName()) != -1)
            {
                fieldAllowed = true;
                break;
            }
        }
        if (!fieldAllowed)
            return false;
    }

    return true;
}

}  // namespace xrpl
