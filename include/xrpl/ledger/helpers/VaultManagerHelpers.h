#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/**
 * The three role accounts of a vault manager entry.
 */
struct VaultRoles
{
    AccountID manager;
    AccountID valuer;
    AccountID guardian;
};

/**
 * Check the disjointness rule of a managed vault.
 *
 * The roles must be pairwise distinct, the valuer and the guardian must not be
 * the vault owner, and the manager may be the vault owner only while the vault
 * has no loan broker, since a broker's owner is always the vault owner.
 *
 * @param view The ledger to read.
 * @param vault The vault SLE.
 * @param roles The roles to check.
 * @return tecROLE_CONFLICT on a violation, else tesSUCCESS.
 */
[[nodiscard]] TER
checkVaultRoles(ReadView const& view, SLE::const_ref vault, VaultRoles const& roles);

/**
 * Whether any loan broker serves the vault.
 *
 * Brokers are linked into the vault pseudo-account's directory. The walk stops
 * at the first broker; the other entries there are the share issuance and the
 * pseudo-account's holdings, at most 1 + kMaxMandateEntries of them.
 *
 * @param view The ledger to read.
 * @param vault The vault SLE.
 * @return true if an ltLOAN_BROKER is linked to the vault.
 */
[[nodiscard]] bool
vaultHasLoanBroker(ReadView const& view, SLE::const_ref vault);

/**
 * The roles of a manager entry as they stand at the view's parent close time.
 *
 * A pending role change counts from sfPendingUntil whether or not a
 * transaction has applied it yet, so permission checks never lag the timelock.
 *
 * @param view The ledger to read.
 * @param manager The ltVAULT_MANAGER SLE.
 * @return The manager, valuer and guardian in force.
 */
[[nodiscard]] VaultRoles
effectiveVaultRoles(ReadView const& view, SLE::const_ref manager);

/**
 * Apply a manager entry's pending update once its timelock has passed.
 *
 * Copies every field of sfPendingUpdate onto the entry and clears
 * sfPendingUpdate and sfPendingUntil. Does nothing before sfPendingUntil.
 *
 * @param view The view to write.
 * @param manager The ltVAULT_MANAGER SLE, peeked from view.
 * @param vault The vault SLE, peeked from view.
 * @param j Journal for logging.
 * @return tesSUCCESS, or the error of a failed write.
 */
TER
applyDuePendingUpdate(ApplyView& view, SLE::ref manager, SLE::ref vault, beast::Journal j);

/**
 * Whether a proposed mandate loosens the current one.
 *
 * Adding an asset, raising an entry's sfPositionMaximum (including lifting it
 * to 0), and removing or changing an entry's sfDomainID loosen. Everything
 * else, removals included, tightens.
 *
 * @param current The mandate in force.
 * @param proposed The full replacement mandate.
 * @return true if the proposal loosens.
 */
[[nodiscard]] bool
isLooseningMandate(STArray const& current, STArray const& proposed);

/**
 * Whether a proposed value of one scalar limit loosens the value in force.
 *
 * Raising sfDeployedMaximum, sfMarkStaleAfter, sfMarkMoveLimit or either fee
 * rate, and lowering sfMarkHaltPeriod, either cover rate or sfTimelock loosen.
 *
 * @param field The limit.
 * @param current The manager entry.
 * @param proposed The transaction carrying the field.
 * @return true if the proposal loosens.
 */
[[nodiscard]] bool
isLooseningField(SField const& field, STObject const& current, STObject const& proposed);

/**
 * The vault pseudo-account's holding of a position asset: its XRP balance,
 * trust line balance or MPToken amount, whatever any freeze or lock says.
 *
 * @param view The ledger to read.
 * @param vault The vault SLE.
 * @param asset The position asset.
 * @return The holding, zero when the pseudo-account holds none.
 */
[[nodiscard]] STAmount
positionHolding(ReadView const& view, SLE::const_ref vault, Asset const& asset);

/**
 * A position's value in the vault asset: its holding times sfMarkPrice,
 * rounded down to the vault asset's precision so NAV is never overstated.
 *
 * @param view The ledger to read.
 * @param vault The vault SLE.
 * @param position The ltVAULT_POSITION SLE.
 * @return The value in units of the vault asset.
 */
[[nodiscard]] Number
positionValue(ReadView const& view, SLE::const_ref vault, SLE::const_ref position);

/**
 * The value of every open position of a vault, read through its mandate.
 *
 * @param view The ledger to read.
 * @param vault The vault SLE.
 * @return The sum of positionValue, or zero for a vault without a manager entry
 *     or before featureManagedVault.
 */
[[nodiscard]] Number
vaultPositionsValue(ReadView const& view, SLE::const_ref vault);

}  // namespace xrpl
