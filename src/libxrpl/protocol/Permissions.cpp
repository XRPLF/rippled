#include <xrpl/protocol/Permissions.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>  // IWYU pragma: keep
#include <xrpl/protocol/TxFormats.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl {

Permission::GranularPermissionEntry::GranularPermissionEntry(
    std::string name,
    TxType txType,
    std::uint32_t permittedFlags,
    std::vector<SOElement> permittedFields)
    : name(std::move(name))
    , txType(txType)
    , permittedFlags(permittedFlags)
    , permittedFields(std::move(permittedFields), TxFormats::getCommonFields())
{
}

Permission::Permission()
{
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegable, amendment, ...) \
    txDelegationMap_[static_cast<TxType>(value)] = {amendment, delegable};

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
    }

    granularPermissionsByName_ = {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(type, ...) {#type, type},

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    };

    {
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

// NOLINTBEGIN(bugprone-macro-parentheses)
#define GRANULAR_PERMISSION(type, txType, value, flags, fields) \
    granularPermissions_.emplace(                               \
        std::piecewise_construct,                               \
        std::forward_as_tuple(GranularPermissionType::type),    \
        std::forward_as_tuple(                                  \
            #type, txType, static_cast<std::uint32_t>(flags), std::vector<SOElement> fields));
        // NOLINTEND(bugprone-macro-parentheses)

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
    }

    if (granularPermissionsByName_.size() != granularPermissions_.size())
    {
        // LCOV_EXCL_START
        Throw<std::logic_error>(
            "granularPermissionsByName_ and granularPermissions_ must have same size");
        // LCOV_EXCL_STOP
    }

    for (auto const& [name, type] : granularPermissionsByName_)
    {
        if (type <= UINT16_MAX)
        {
            // LCOV_EXCL_START
            Throw<std::logic_error>(
                "Granular permission value must exceed the maximum uint16_t value: " + name);
            // LCOV_EXCL_STOP
        }
    }

    for (auto const& [type, entry] : granularPermissions_)
        granularTxTypes_.insert(entry.txType);

    // Validate that all fields listed in permissions.macro exist in the
    // corresponding transaction type's format, catching typos at startup.
    for (auto const& [type, entry] : granularPermissions_)
    {
        if (!txDelegationMap_.contains(entry.txType))
        {
            // LCOV_EXCL_START
            Throw<std::logic_error>("Invalid granular permission txType in txDelegationMap_");
            // LCOV_EXCL_STOP
        }

        auto const* fmt = TxFormats::getInstance().findByType(entry.txType);
        if (fmt == nullptr)
        {
            // LCOV_EXCL_START
            Throw<std::logic_error>("Invalid granular permission txType");
            // LCOV_EXCL_STOP
        }

        for (auto const& field : entry.permittedFields)
        {
            if (fmt->getSOTemplate().getIndex(field.sField()) == -1)
            {
                // LCOV_EXCL_START
                Throw<std::logic_error>("Invalid granular permission field");
                // LCOV_EXCL_STOP
            }
        }
    }
}

Permission const&
Permission::getInstance()
{
    static Permission const kInstance;
    return kInstance;
}

std::optional<std::string>
Permission::getPermissionName(std::uint32_t value) const
{
    if (value == 0)
        return std::nullopt;

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
    auto const it = granularPermissionsByName_.find(name);
    if (it != granularPermissionsByName_.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

std::optional<std::string>
Permission::getGranularName(GranularPermissionType value) const
{
    auto const it = granularPermissions_.find(value);
    if (it != granularPermissions_.end())
        return it->second.name;

    return std::nullopt;
}

std::optional<TxType>
Permission::getGranularTxType(GranularPermissionType gpType) const
{
    auto const it = granularPermissions_.find(gpType);
    if (it != granularPermissions_.end())
        return it->second.txType;

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
    auto const it = txDelegationMap_.find(txType);
    XRPL_ASSERT(
        it != txDelegationMap_.end(),
        "xrpl::Permission::getTxFeature : tx exists in txDelegationMap_");

    if (it->second.amendment == uint256{})
        return std::nullopt;

    return std::optional{std::cref(it->second.amendment)};
}

bool
Permission::isDelegable(std::uint32_t permissionValue, Rules const& rules) const
{
    if (permissionValue == 0)
        return false;  // LCOV_EXCL_LINE

    auto const amendmentEnabled = [&rules](TxDelegationEntry const& entry) {
        return entry.amendment == uint256{} || rules.enabled(entry.amendment);
    };

    // Granular permissions may authorize a limited subset of a tx type even
    // when the full tx type is not delegable. They still require the
    // underlying transaction amendment to be enabled.
    if (auto const granularIt =
            granularPermissions_.find(static_cast<GranularPermissionType>(permissionValue));
        granularIt != granularPermissions_.end())
    {
        auto const txIt = txDelegationMap_.find(granularIt->second.txType);
        return txIt != txDelegationMap_.end() && amendmentEnabled(txIt->second);
    }

    auto const txType = permissionToTxType(permissionValue);
    auto const txIt = txDelegationMap_.find(txType);

    // Tx-level permissions require the transaction type itself to be delegable, and
    // the corresponding amendment enabled.
    return txIt != txDelegationMap_.end() && txIt->second.delegable != NotDelegable &&
        amendmentEnabled(txIt->second);
}

uint32_t
Permission::txToPermissionType(TxType const type)
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
    // Build union of flags upfront to enable an early exit. Fields are not stored and
    // grouped in advance to avoid heap allocation.
    std::uint32_t unionFlags = 0;
    for (auto const& gp : heldPermissions)
    {
        auto const it = granularPermissions_.find(gp);
        if (it != granularPermissions_.end())
            unionFlags |= it->second.permittedFlags;
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

        if (!std::ranges::any_of(heldPermissions, [&](auto const& gp) {
                auto const it = granularPermissions_.find(gp);
                return it != granularPermissions_.end() &&
                    it->second.permittedFields.getIndex(field.getFName()) != -1;
            }))
            return false;
    }

    return true;
}

}  // namespace xrpl
