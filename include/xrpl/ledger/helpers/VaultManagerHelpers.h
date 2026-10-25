#pragma once

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
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

}  // namespace xrpl
