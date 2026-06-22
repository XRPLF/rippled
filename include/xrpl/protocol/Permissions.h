#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xrpl {

class STTx;

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
#pragma push_macro("GRANULAR_PERMISSION")
#undef GRANULAR_PERMISSION

#define GRANULAR_PERMISSION(name, txType, value, ...) name = (value),

#include <xrpl/protocol/detail/permissions.macro>

#undef GRANULAR_PERMISSION
#pragma pop_macro("GRANULAR_PERMISSION")
};

// Injected bare enumerators (xrpl::delegable / xrpl::notDelegable) are required by preprocessor
// tricks in tests and macro-generated code; enum class would break that.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Delegation { Delegable, NotDelegable };

class Permission
{
private:
    Permission();

    struct GranularPermissionEntry
    {
        std::string name;
        TxType txType;
        std::uint32_t permittedFlags;
        SOTemplate permittedFields;

        GranularPermissionEntry(
            std::string name,
            TxType txType,
            std::uint32_t permittedFlags,
            std::vector<SOElement> fields);
    };

    struct TxDelegationEntry
    {
        uint256 amendment;
        Delegation delegable{NotDelegable};
    };

    std::unordered_set<TxType> granularTxTypes_;
    std::unordered_map<TxType, TxDelegationEntry> txDelegationMap_;
    std::unordered_map<std::string, GranularPermissionType> granularPermissionsByName_;
    std::unordered_map<GranularPermissionType, GranularPermissionEntry> granularPermissions_;

public:
    static Permission const&
    getInstance();

    Permission(Permission const&) = delete;
    Permission&
    operator=(Permission const&) = delete;

    [[nodiscard]] std::optional<std::string>
    getPermissionName(std::uint32_t value) const;

    [[nodiscard]] std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    [[nodiscard]] std::optional<std::string>
    getGranularName(GranularPermissionType value) const;

    [[nodiscard]] std::optional<TxType>
    getGranularTxType(GranularPermissionType gpType) const;

    // Returns a reference to avoid copying uint256 - 32 bytes. std::optional
    // cannot hold references directly, so std::reference_wrapper is used.
    [[nodiscard]] std::optional<std::reference_wrapper<uint256 const>>
    getTxFeature(TxType txType) const;

    [[nodiscard]] bool
    isDelegable(std::uint32_t permissionValue, Rules const& rules) const;

    [[nodiscard]] bool
    hasGranularPermissions(TxType txType) const;

    // for tx level permission, permission value is equal to tx type plus one
    [[nodiscard]] static uint32_t
    txToPermissionType(TxType type);

    // tx type value is permission value minus one
    [[nodiscard]] static TxType
    permissionToTxType(std::uint32_t value);

    /**
     * @brief Verifies a delegated transaction against its granular permission template.
     *
     * @note WARNING: Do not move this check before standard transaction-level
     * format checks, which is in preclaim. This function assumes the transaction's
     * base structural integrity (fees, sequence, signatures) has already been
     * validated.
     *
     * @param tx The transaction to verify.
     * @param heldPermissions The granular permissions that the sender hold.
     * @return true if the transaction fields and flags comply with the granular template.
     */
    [[nodiscard]] bool
    checkGranularSandbox(
        STTx const& tx,
        std::unordered_set<GranularPermissionType> const& heldPermissions) const;
};

}  // namespace xrpl
