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
#pragma push_macro("PERMISSION")
#pragma push_macro("GRANULAR_TEMPLATE")
#undef PERMISSION
#undef GRANULAR_TEMPLATE

#define PERMISSION(type, txType, value) {#type, type},
#define GRANULAR_TEMPLATE(txType, allowedFlags, allowedFields)

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#undef GRANULAR_TEMPLATE
#pragma pop_macro("GRANULAR_TEMPLATE")
#pragma pop_macro("PERMISSION")
    };

    granularNameMap_ = {
#pragma push_macro("PERMISSION")
#pragma push_macro("GRANULAR_TEMPLATE")
#undef PERMISSION
#undef GRANULAR_TEMPLATE

#define PERMISSION(type, txType, value) {type, #type},
#define GRANULAR_TEMPLATE(...)

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#undef GRANULAR_TEMPLATE
#pragma pop_macro("GRANULAR_TEMPLATE")
#pragma pop_macro("PERMISSION")
    };

    granularTxTypeMap_ = {
#pragma push_macro("PERMISSION")
#pragma push_macro("GRANULAR_TEMPLATE")
#undef PERMISSION
#undef GRANULAR_TEMPLATE

#define PERMISSION(type, txType, value) {type, txType},
#define GRANULAR_TEMPLATE(...)

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#undef GRANULAR_TEMPLATE
#pragma pop_macro("GRANULAR_TEMPLATE")
#pragma pop_macro("PERMISSION")
    };

    granularPermittedFlags_ = {
#pragma push_macro("GRANULAR_TEMPLATE")
#undef GRANULAR_TEMPLATE
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(...)
#define GRANULAR_TEMPLATE(txType, flags, fields) {txType, flags},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_TEMPLATE
#undef PERMISSION
#pragma pop_macro("PERMISSION")
#pragma pop_macro("GRANULAR_TEMPLATE")
    };

    {
        std::unordered_map<TxType, std::vector<SOElement>> tempFields = {
#pragma push_macro("GRANULAR_TEMPLATE")
#undef GRANULAR_TEMPLATE
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(...)
#define GRANULAR_TEMPLATE(txType, flags, fields) {txType, std::vector<SOElement> fields},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_TEMPLATE
#undef PERMISSION
#pragma pop_macro("PERMISSION")
#pragma pop_macro("GRANULAR_TEMPLATE")
        };

        for (auto& [txType, fields] : tempFields)
        {
            granularTemplates_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(txType),
                std::forward_as_tuple(std::move(fields), TxFormats::getCommonFields()));
        }
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
Permission::checkGranularSandbox(STTx const& tx) const
{
    auto const txType = tx.getTxnType();

    auto const flagsIt = granularPermittedFlags_.find(txType);
    auto const templateIt = granularTemplates_.find(txType);

    if (templateIt == granularTemplates_.end() || flagsIt == granularPermittedFlags_.end())
        return false;

    // Check if the flag is permitted
    if ((tx.getFlags() & ~flagsIt->second) != 0)
        return false;

    // Check if the field is permitted
    for (auto const& field : tx)
    {
        if (field.getSType() == STI_NOTPRESENT)
            continue;
        if (templateIt->second.getIndex(field.getFName()) == -1)
        {
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
