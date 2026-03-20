#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace xrpl {

class STTx;

enum GranularPermissionType : std::uint32_t {
#pragma push_macro("PERMISSION")
#pragma push_macro("GRANULAR_TEMPLATE")
#undef PERMISSION
#undef GRANULAR_TEMPLATE

#define PERMISSION(name, txType, value) name = value,
#define GRANULAR_TEMPLATE(txType, allowedFlags, allowedFields)

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#undef GRANULAR_TEMPLATE
#pragma pop_macro("GRANULAR_TEMPLATE")
#pragma pop_macro("PERMISSION")
};

enum Delegation { delegable, notDelegable };

class Permission
{
private:
    Permission();

    std::unordered_map<std::uint16_t, uint256> txFeatureMap_;
    std::unordered_map<std::uint16_t, Delegation> delegableTx_;
    std::unordered_map<std::string, GranularPermissionType> granularPermissionMap_;

    std::unordered_map<GranularPermissionType, std::string> granularNameMap_;
    std::unordered_map<GranularPermissionType, TxType> granularTxTypeMap_;

    std::unordered_map<TxType, std::uint32_t> granularPermittedFlags_;
    std::unordered_map<TxType, SOTemplate> granularTemplates_;

public:
    static Permission const&
    getInstance();

    Permission(Permission const&) = delete;
    Permission&
    operator=(Permission const&) = delete;

    std::optional<std::string>
    getPermissionName(std::uint32_t const value) const;

    std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    std::optional<std::string>
    getGranularName(GranularPermissionType const& value) const;

    std::optional<TxType>
    getGranularTxType(GranularPermissionType const& gpType) const;

    std::optional<std::reference_wrapper<uint256 const>>
    getTxFeature(TxType txType) const;

    bool
    isDelegable(std::uint32_t const& permissionValue, Rules const& rules) const;

    // for tx level permission, permission value is equal to tx type plus one
    static uint32_t
    txToPermissionType(TxType const& type);

    // tx type value is permission value minus one
    static TxType
    permissionToTxType(uint32_t const& value);

    [[nodiscard]] bool
    checkGranularSandbox(STTx const& tx) const;
};

}  // namespace xrpl
