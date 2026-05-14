/** @file
 *  Central definition of XRPL's account-delegation permission system,
 *  used by the `DelegateSet` transaction type.
 *
 *  Two numeric ranges partition the `sfPermissionValue` field stored
 *  on-ledger:
 *  - **Transaction-level** (≤ `UINT16_MAX`): `TxType + 1`, granting
 *    authority over an entire transaction type.
 *  - **Granular** (> `UINT16_MAX`, minimum 65537): covers a specific
 *    sub-operation within a transaction type (e.g., freezing a trustline
 *    without being able to authorize it).
 *
 *  The `Permission` singleton is the runtime authority for both ranges.
 */
#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace xrpl {

/** Granular sub-operation permission values used by the delegation system.
 *
 *  Each enumerator targets a specific capability within a parent transaction
 *  type, enabling fine-grained delegation without granting broad transaction-
 *  level authority.  For example, `TrustlineFreeze` delegates only the ability
 *  to freeze a trustline via `ttTRUST_SET`, not to authorize or unfreeze.
 *
 *  All values are greater than `UINT16_MAX` (minimum 65537), which keeps them
 *  numerically disjoint from transaction-level permissions (≤ `UINT16_MAX`).
 *  This invariant is asserted at startup inside the `Permission` constructor.
 *
 *  Generated from `detail/permissions.macro` via the X-macro pattern.  Adding
 *  a new sub-operation requires only a single `PERMISSION(...)` entry in that
 *  file.
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

/** Indicates whether a transaction type may be delegated in bulk via
 *  a transaction-level `DelegateSet` permission.
 *
 *  The policy for each `TxType` is encoded in `detail/transactions.macro`
 *  as the `delegable` parameter of every `TRANSACTION(...)` entry.
 *  Sensitive types such as `ttACCOUNT_SET` and `ttREGULAR_KEY_SET` are
 *  `NotDelegable`; most operational types are `Delegable`.
 *
 *  @note Bare enumerators (`xrpl::Delegable` / `xrpl::NotDelegable`) are
 *      required by preprocessor expansions in tests and macro-generated
 *      code; `enum class` would break that usage.
 */
// Injected bare enumerators (xrpl::delegable / xrpl::notDelegable) are required by preprocessor
// tricks in tests and macro-generated code; enum class would break that.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Delegation { Delegable, NotDelegable };

/** Central authority for XRPL's account-delegation permission system.
 *
 *  A Meyer's singleton populated at first call to `getInstance()`.  Its
 *  constructor expands `transactions.macro` and `permissions.macro` to build
 *  five immutable lookup maps covering every known transaction type and
 *  granular sub-operation.  After construction the maps are never mutated,
 *  so all concurrent read access from transaction-processing threads is safe
 *  without synchronization.
 *
 *  The two principal call sites are:
 *  - `DelegateSet::preflight()` — calls `isDelegable()` to validate each
 *    `sfPermissionValue` before it is written on-ledger.
 *  - `DelegateUtils` / transactors — call `getGranularTxType()` and related
 *    helpers to enforce granular limits at execution time.
 */
class Permission
{
private:
    Permission();

    /** Maps each `TxType` to the amendment required to use it, or `uint256{}` if none. */
    std::unordered_map<std::uint16_t, uint256> txFeatureMap_;

    /** Maps each `TxType` to its `Delegable` / `NotDelegable` policy tag. */
    std::unordered_map<std::uint16_t, Delegation> delegableTx_;

    /** Maps granular permission name strings to their `GranularPermissionType` values. */
    std::unordered_map<std::string, GranularPermissionType> granularPermissionMap_;

    /** Maps `GranularPermissionType` values to their name strings (inverse of `granularPermissionMap_`). */
    std::unordered_map<GranularPermissionType, std::string> granularNameMap_;

    /** Maps each `GranularPermissionType` to its parent `TxType`. */
    std::unordered_map<GranularPermissionType, TxType> granularTxTypeMap_;

public:
    /** Returns the process-wide singleton instance.
     *
     *  Initialized on first call via a function-local `static`; C++11
     *  guarantees thread-safe initialization.  The instance is never mutated
     *  after construction.
     *
     *  @return A `const` reference to the singleton `Permission` object.
     */
    static Permission const&
    getInstance();

    Permission(Permission const&) = delete;
    Permission&
    operator=(Permission const&) = delete;

    /** Resolves a raw `sfPermissionValue` to its human-readable name.
     *
     *  Checks the granular permission table first (values > `UINT16_MAX`).
     *  If unrecognized there, decodes the value as a transaction-level
     *  permission (`value - 1` = `TxType`) and delegates to `TxFormats` for
     *  the canonical name.  Used by `STUInt32::getText()` and
     *  `STUInt32::getJson()` to render any `sfPermissionValue` as a string
     *  instead of a raw number.
     *
     *  @param value Raw `sfPermissionValue` from the ledger.
     *  @return The permission name, or `std::nullopt` if `value` is not
     *      recognized as either a granular or transaction-level permission.
     */
    [[nodiscard]] std::optional<std::string>
    getPermissionName(std::uint32_t const value) const;

    /** Looks up the numeric wire value of a granular permission by name.
     *
     *  Used when deserializing `sfPermissionValue` from JSON (e.g., during
     *  `DelegateSet` preflight or RPC input parsing) to convert a
     *  human-readable name like `"TrustlineFreeze"` back to its `uint32_t`
     *  representation.
     *
     *  @param name Case-sensitive granular permission name.
     *  @return The corresponding `uint32_t` wire value, or `std::nullopt` if
     *      `name` is not a known granular permission.
     */
    [[nodiscard]] std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    /** Looks up the name of a granular permission by its enum value.
     *
     *  Inverse of `getGranularValue`; used when serializing a granular
     *  permission value to human-readable output.
     *
     *  @param value A `GranularPermissionType` enum value.
     *  @return The permission name string, or `std::nullopt` if `value` is
     *      not a known granular permission.
     */
    [[nodiscard]] std::optional<std::string>
    getGranularName(GranularPermissionType const& value) const;

    /** Returns the parent transaction type for a granular permission.
     *
     *  Multiple granular permissions share the same parent `TxType`; for
     *  example, `TrustlineAuthorize`, `TrustlineFreeze`, and
     *  `TrustlineUnfreeze` all map to `ttTRUST_SET`.  Used by `isDelegable()`
     *  and execution-time helpers to locate the relevant transactor context
     *  and required amendment for a granular sub-operation.
     *
     *  @param gpType A `GranularPermissionType` enum value.
     *  @return The parent `TxType`, or `std::nullopt` if `gpType` is not a
     *      known granular permission.
     */
    [[nodiscard]] std::optional<TxType>
    getGranularTxType(GranularPermissionType const& gpType) const;

    /** Returns the amendment required to use a transaction type, if any.
     *
     *  A `uint256{}` stored in `txFeatureMap_` means the transaction type
     *  requires no enabling amendment.  In that case `std::nullopt` is
     *  returned, signalling that the type is unconditionally available.
     *
     *  @param txType A recognized transaction type.
     *  @return A const reference to the required amendment hash wrapped in
     *      `std::optional`, or `std::nullopt` if no amendment is required.
     *  @note Asserts in debug builds that `txType` is present in
     *      `txFeatureMap_`.  Passing an unregistered `TxType` is a
     *      programming error (a transaction missing from `transactions.macro`).
     */
    [[nodiscard]] std::optional<std::reference_wrapper<uint256 const>>
    getTxFeature(TxType txType) const;

    /** Determines whether a permission value may appear in a `DelegateSet`
     *  transaction under the current ledger rules.
     *
     *  The check differs by permission kind:
     *  - **Granular** (value > `UINT16_MAX`): accepted whenever the value
     *    resolves to a known `GranularPermissionType`; no further gate is
     *    applied because granular permissions are inherently narrow.
     *  - **Transaction-level** (value ≤ `UINT16_MAX`): accepted only when the
     *    decoded `TxType` is recognized, its required amendment is currently
     *    enabled in `rules` (or no amendment is required), and the type is
     *    marked `Delegable` in `transactions.macro`.
     *
     *  @param permissionValue Raw `sfPermissionValue` to validate.
     *  @param rules           Active amendment rules for the current ledger.
     *  @return `true` if the permission may be granted, `false` otherwise.
     *  @note The amendment check prevents a transaction type from being
     *      delegated before the ledger feature that introduces it is live,
     *      even if the macro table already includes it.
     */
    [[nodiscard]] bool
    isDelegable(std::uint32_t const& permissionValue, Rules const& rules) const;

    /** Converts a `TxType` to its transaction-level permission value.
     *
     *  Transaction-level permissions are encoded as `TxType + 1`.  The `+1`
     *  offset ensures zero is never a valid permission value and keeps the
     *  entire range within `uint16` (transaction-level permissions ≤
     *  `UINT16_MAX`).
     *
     *  @param type A transaction type.
     *  @return The corresponding `uint32_t` permission value (`TxType + 1`).
     *  @see permissionToTxType
     */
    static uint32_t
    txToPermissionType(TxType const& type);

    /** Converts a transaction-level permission value back to its `TxType`.
     *
     *  Inverse of `txToPermissionType`.  Callers must verify that `value` is
     *  in the transaction-level range (≤ `UINT16_MAX`) before calling; this
     *  function performs no range check.
     *
     *  @param value A transaction-level permission value (`TxType + 1`).
     *  @return The decoded `TxType` (`value - 1`).
     *  @see txToPermissionType
     */
    static TxType
    permissionToTxType(uint32_t const& value);
};

}  // namespace xrpl
