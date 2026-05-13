/** @file
 *  Implements the `Permission` singleton — the central authority for
 *  XRPL's account-delegation permission system.
 *
 *  The constructor uses X-macro expansions of `transactions.macro` and
 *  `permissions.macro` to populate five lookup maps that cover every
 *  known transaction type and granular sub-operation permission.  No
 *  other translation unit defines or mutates these maps, so this file
 *  is the single source of truth for the permission number-space at
 *  runtime.
 */

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

/** Constructs the singleton by populating all five permission lookup maps.
 *
 *  Each map is populated by expanding the same macro files
 *  (`transactions.macro` / `permissions.macro`) with a purpose-specific
 *  `#define TRANSACTION` or `#define PERMISSION` binding.  `#pragma
 *  push_macro` / `pop_macro` guards prevent the temporary definitions from
 *  escaping into surrounding translation units.
 *
 *  Maps built:
 *  - `txFeatureMap_`          — TxType → required amendment (`uint256{}` = none)
 *  - `delegableTx_`           — TxType → `Delegable` / `NotDelegable`
 *  - `granularPermissionMap_` — name string → `GranularPermissionType`
 *  - `granularNameMap_`       — `GranularPermissionType` → name string
 *  - `granularTxTypeMap_`     — `GranularPermissionType` → parent `TxType`
 *
 *  Two startup assertions enforce invariants that cannot be expressed in the
 *  type system:
 *  1. `txFeatureMap_` and `delegableTx_` must have the same number of entries
 *     (a mismatch would mean a transaction appears in one macro expansion but
 *     not the other — a programming error in the macro file).
 *  2. Every granular permission value must be `> UINT16_MAX`, preserving the
 *     numeric partition between transaction-level permissions (≤ UINT16_MAX)
 *     and granular permissions (> UINT16_MAX).
 */
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
#undef PERMISSION

#define PERMISSION(type, txType, value) {#type, type},

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
    };

    granularNameMap_ = {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) {type, #type},

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
    };

    granularTxTypeMap_ = {
#pragma push_macro("PERMISSION")
#undef PERMISSION

#define PERMISSION(type, txType, value) {type, txType},

#include <xrpl/protocol/detail/permissions.macro>

#undef PERMISSION
#pragma pop_macro("PERMISSION")
    };

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

/** Returns the process-wide singleton instance.
 *
 *  Initialized on first call via a function-local `static`; C++11
 *  guarantees the initialization is thread-safe.  The instance is
 *  never mutated after construction, so concurrent read access
 *  requires no synchronization.
 *
 *  @return A `const` reference to the singleton `Permission` object.
 */
Permission const&
Permission::getInstance()
{
    static Permission const kINSTANCE;
    return kINSTANCE;
}

/** Resolves a raw permission value to its human-readable name.
 *
 *  Tries the granular permission table first (values > `UINT16_MAX`).
 *  If that fails, decodes the value as a transaction-level permission
 *  (`value - 1` = `TxType`) and delegates to `TxFormats` for the name,
 *  keeping transaction names canonical in a single place.
 *
 *  @param value Raw `sfPermissionValue` from the ledger.
 *  @return The permission name, or `std::nullopt` if `value` is
 *      unrecognized as either a granular or transaction-level permission.
 */
std::optional<std::string>
Permission::getPermissionName(std::uint32_t const value) const
{
    auto const permissionValue = static_cast<GranularPermissionType>(value);
    if (auto const granular = getGranularName(permissionValue))
        return granular;

    auto const txType = permissionToTxType(value);
    if (auto const* item = TxFormats::getInstance().findByType(txType); item != nullptr)
        return item->getName();

    return std::nullopt;
}

/** Looks up the numeric value of a granular permission by name.
 *
 *  Used when deserializing permission names from JSON (e.g., during
 *  `DelegateSet` preflight or RPC input handling).
 *
 *  @param name Case-sensitive granular permission name (e.g.,
 *      `"TrustlineAuthorize"`).
 *  @return The corresponding `uint32_t` wire value, or `std::nullopt` if
 *      `name` is not a known granular permission.
 */
std::optional<std::uint32_t>
Permission::getGranularValue(std::string const& name) const
{
    auto const it = granularPermissionMap_.find(name);
    if (it != granularPermissionMap_.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

/** Looks up the name of a granular permission by its enum value.
 *
 *  Inverse of `getGranularValue`; used when serializing permission values
 *  to human-readable output.
 *
 *  @param value A `GranularPermissionType` enum value.
 *  @return The permission name string, or `std::nullopt` if `value` is
 *      not a known granular permission.
 */
std::optional<std::string>
Permission::getGranularName(GranularPermissionType const& value) const
{
    auto const it = granularNameMap_.find(value);
    if (it != granularNameMap_.end())
        return it->second;

    return std::nullopt;
}

/** Returns the parent transaction type for a granular permission.
 *
 *  Multiple granular permissions can share the same parent `TxType`
 *  (e.g., `TrustlineAuthorize`, `TrustlineFreeze`, and
 *  `TrustlineUnfreeze` all map to `ttTRUST_SET`).  This relationship is
 *  used by `isDelegable()` to locate the required amendment for any
 *  granular sub-operation.
 *
 *  @param gpType A `GranularPermissionType` enum value.
 *  @return The parent `TxType`, or `std::nullopt` if `gpType` is not
 *      a known granular permission.
 */
std::optional<TxType>
Permission::getGranularTxType(GranularPermissionType const& gpType) const
{
    auto const it = granularTxTypeMap_.find(gpType);
    if (it != granularTxTypeMap_.end())
        return it->second;

    return std::nullopt;
}

/** Returns the amendment required to delegate a transaction type, if any.
 *
 *  @param txType A recognized transaction type.
 *  @return A reference to the required amendment hash, or `std::nullopt`
 *      if the transaction type needs no enabling amendment.
 *  @note Asserts (debug builds) that `txType` is present in
 *      `txFeatureMap_`.  Passing an unregistered `TxType` is a
 *      programming error.
 */
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

/** Determines whether a permission value may appear in a `DelegateSet`
 *  transaction under the current ledger rules.
 *
 *  Called from `DelegateSet::preflight()` for every entry in
 *  `sfPermissions`.  The check differs by permission kind:
 *
 *  - **Granular permission** (value > `UINT16_MAX`): accepted whenever the
 *    value resolves to a known `GranularPermissionType`.  Granular
 *    permissions are intentionally narrow, so no additional gate is needed.
 *  - **Transaction-level permission** (value ≤ `UINT16_MAX`): accepted only
 *    when the decoded `TxType` is recognized, the transaction's required
 *    amendment is currently enabled in `rules` (or no amendment is
 *    required), and the `Delegation` flag for that type is `Delegable`.
 *
 *  @param permissionValue Raw `sfPermissionValue` from the ledger object.
 *  @param rules           Active amendment rules for the current ledger.
 *  @return `true` if the permission may be granted, `false` otherwise.
 *  @note The amendment check prevents a transaction type from being
 *      delegated before the ledger feature that introduces it is live,
 *      even if the macro table already includes it.
 */
bool
Permission::isDelegable(std::uint32_t const& permissionValue, Rules const& rules) const
{
    auto const granularPermission =
        getGranularName(static_cast<GranularPermissionType>(permissionValue));
    if (granularPermission)
        return true;

    auto const txType = permissionToTxType(permissionValue);
    auto const it = delegableTx_.find(txType);

    if (it == delegableTx_.end())
        return false;

    auto const txFeaturesIt = txFeatureMap_.find(txType);
    XRPL_ASSERT(
        txFeaturesIt != txFeatureMap_.end(),
        "xrpl::Permissions::isDelegable : tx exists in txFeatureMap_");

    if (txFeaturesIt->second != uint256{} && !rules.enabled(txFeaturesIt->second))
        return false;

    if (it->second == Delegation::NotDelegable)
        return false;

    return true;
}

/** Converts a `TxType` to its transaction-level permission value.
 *
 *  Transaction-level permissions are encoded as `TxType + 1`.  The +1
 *  offset shifts zero-based `TxType` values to start at 1, keeping the
 *  entire range within `uint16` and reserving 0 as an invalid sentinel.
 *
 *  @param type A transaction type.
 *  @return The corresponding permission value (`static_cast<uint32_t>(type) + 1`).
 */
uint32_t
Permission::txToPermissionType(TxType const& type)
{
    return static_cast<uint32_t>(type) + 1;
}

/** Converts a transaction-level permission value back to its `TxType`.
 *
 *  Inverse of `txToPermissionType`; callers are responsible for verifying
 *  that `value` is in the transaction-level range (≤ `UINT16_MAX`) before
 *  calling this function.
 *
 *  @param value A transaction-level permission value.
 *  @return The decoded `TxType` (`static_cast<TxType>(value - 1)`).
 */
TxType
Permission::permissionToTxType(uint32_t const& value)
{
    return static_cast<TxType>(value - 1);
}

}  // namespace xrpl
