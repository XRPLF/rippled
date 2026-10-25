#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <utility>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: a managed vault's positions, roles and halt move only
 * where XLS Managed Vault section 13 allows.
 *
 * For every ltVAULT_POSITION, ltVAULT_MANAGER and ltVAULT the transaction
 * touches:
 *
 * 1. A position's sfMarkPrice is positive and its sfCostBasis is not negative;
 *    sfLastMarkTime never decreases; the mark changes only in VaultMark and
 *    the cost basis only in VaultTrade.
 * 2. A position is created and deleted only by VaultTrade, its asset is in the
 *    mandate, and sfManagedPositionCount equals the vault's positions.
 * 3. A VaultTrade leaves no position without a holding, and an open leaves the
 *    positions within sfDeployedMaximum, the position within its
 *    sfPositionMaximum, and the cover at or above its minimum.
 * 4. No role-holder deposits to or withdraws from the vault it serves.
 * 5. A role changes only as its pending change falls due; a tightening never
 *    enters sfPendingUpdate; the roles stay disjoint.
 * 6. sfMarkHaltUntil only moves forward, and only in VaultMark or
 *    VaultManagerSet.
 */
class ValidVaultPositions
{
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> positions_;
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> managers_;
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> vaults_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
