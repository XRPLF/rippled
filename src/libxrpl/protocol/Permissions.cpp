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
        "xrpl::Permission::Permission : txFeatureMap_ and delegableTx_ must have same size");

    for ([[maybe_unused]] auto const& permission : granularPermissionMap_)
    {
        XRPL_ASSERT(
            permission.second > UINT16_MAX,
            "xrpl::Permission::Permission : granular permission value must exceed the maximum "
            "uint16_t value");
    }

    for (auto const& [gp, txType] : granularTxTypeMap_)
        granularTxTypes_.insert(txType);

    // Validate that all fields listed in permissions.macro exist in the
    // corresponding transaction type's format, catching typos at startup.
    {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, txType, value, flags, fields)                                    \
    {                                                                                              \
        auto const* fmt = TxFormats::getInstance().findByType(txType);                             \
        XRPL_ASSERT(                                                                               \
            fmt != nullptr, "xrpl::Permission::Permission : granular permission txType is valid"); \
        for (auto const& field : std::vector<SOElement> fields)                                    \
        {                                                                                          \
            XRPL_ASSERT(                                                                           \
                fmt->getSOTemplate().getIndex(field.sField()) != -1,                               \
                "xrpl::Permission::Permission : granular permission field is valid");              \
        }                                                                                          \
    }

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
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
        "xrpl::Permission::getTxFeature : tx exists in txFeatureMap_");

    if (txFeaturesIt->second == uint256{})
        return std::nullopt;

    return std::optional{std::cref(txFeaturesIt->second)};
}

bool
Permission::isDelegable(std::uint32_t permissionValue, Rules const& rules) const
{
    // Granular permissions are always delegable.
    if (getGranularName(static_cast<GranularPermissionType>(permissionValue)))
        return true;

    auto const txType = permissionToTxType(permissionValue);
    auto const it = delegableTx_.find(txType);

    if (it == delegableTx_.end() || it->second == Delegation::notDelegable)
        return false;

    // Delegation is only allowed if the required amendment is enabled.
    // getTxFeature returns nullopt for transactions that don't require an amendment.
    if (auto const feature = getTxFeature(txType))
        return rules.enabled(*feature);

    return true;
}

uint32_t
Permission::txToPermissionType(TxType type)
{
    return static_cast<uint32_t>(type) + 1;
}

TxType
Permission::permissionToTxType(uint32_t value)
{
    XRPL_ASSERT(value > 0, "xrpl::Permission::permissionToTxType : value is greater than 0");
    return static_cast<TxType>(value - 1);
}

bool
Permission::checkGranularSandbox(
    STTx const& tx,
    std::unordered_set<GranularPermissionType> const& heldPermissions) const
{
    // Build union of flags and templates across all held permissions.
    std::uint32_t unionFlags = 0;
    for (auto const& gp : heldPermissions)
    {
        auto const it = granularPermittedFlags_.find(gp);
        if (it != granularPermittedFlags_.end())
            unionFlags |= it->second;
    }

    // Check if flags are permitted
    if ((tx.getFlags() & ~unionFlags) != 0)
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
