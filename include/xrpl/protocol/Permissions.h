#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace xrpl {
/**
 * We have both transaction type permissions and granular type permissions.
 * Since we will reuse the TransactionFormats to parse the Transaction
 * Permissions, only the GranularPermissionType is defined here. To prevent
 * conflicts with TxType, the GranularPermissionType is always set to a value
 * greater than the maximum value of uint16.
 */
// Macro-generated, complex
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum GranularPermissionType : std::uint32_t {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) type = (value),

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
};

// Injected bare enumerators (xrpl::delegable / xrpl::notDelegable) are required by preprocessor
// tricks in tests and macro-generated code; enum class would break that.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
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

public:
    static Permission const&
    getInstance();

    Permission(Permission const&) = delete;
    Permission&
    operator=(Permission const&) = delete;

    [[nodiscard]] std::optional<std::string>
    getPermissionName(std::uint32_t const value) const;

    [[nodiscard]] std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    [[nodiscard]] std::optional<std::string>
    getGranularName(GranularPermissionType const& value) const;

    [[nodiscard]] std::optional<TxType>
    getGranularTxType(GranularPermissionType const& gpType) const;

    [[nodiscard]] std::optional<std::reference_wrapper<uint256 const>>
    getTxFeature(TxType txType) const;

    [[nodiscard]] bool
    isDelegable(std::uint32_t const& permissionValue, Rules const& rules) const;

    // for tx level permission, permission value is equal to tx type plus one
    static uint32_t
    txToPermissionType(TxType const& type);

    // tx type value is permission value minus one
    static TxType
    permissionToTxType(uint32_t const& value);
};

}  // namespace xrpl
