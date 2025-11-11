#ifndef XRPL_PROTOCOL_PERMISSION_H_INCLUDED
#define XRPL_PROTOCOL_PERMISSION_H_INCLUDED

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace ripple {
/**
 * We have both transaction type permissions and granular type permissions.
 * Since we will reuse the TransactionFormats to parse the Transaction
 * Permissions, only the GranularPermissionType is defined here. To prevent
 * conflicts with TxType, the GranularPermissionType is always set to a value
 * greater than the maximum value of uint16.
 */
enum GranularPermissionType : std::uint32_t {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) type = value,

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
};

enum Delegation { delegatable, notDelegatable };

class Permission
{
private:
    Permission();

    std::unordered_map<std::uint16_t, uint256> txFeatureMap_;

    std::unordered_map<std::uint16_t, Delegation> delegatableTx_;

    std::unordered_map<std::string, GranularPermissionType>
        granularPermissionMap_;

    std::unordered_map<GranularPermissionType, std::string> granularNameMap_;

    std::unordered_map<GranularPermissionType, TxType> granularTxTypeMap_;

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

    std::optional<std::reference_wrapper<uint256 const>> const
    getTxFeature(TxType txType) const;

    bool
    isDelegatable(std::uint32_t const& permissionValue, Rules const& rules)
        const;

    // for tx level permission, permission value is equal to tx type plus one
    uint32_t
    txToPermissionType(TxType const& type) const;

    // tx type value is permission value minus one
    TxType
    permissionToTxType(uint32_t const& value) const;
};

}  // namespace ripple

#endif
