#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: the continuous-accrual state of a vault moves only where it
 * is allowed to.
 *
 * Enforces XLS Vault Continuous Accrual section 7 for every Vault entry the
 * transaction touches:
 *
 * 1. sfAccrualRate and sfUnearnedInterest are never negative.
 * 2. sfLastAccrualTime never decreases, and changes only in a ttLOAN_*
 *    transaction, which is where the vault settles.
 * 4. VaultDeposit, VaultWithdraw and VaultClawback leave sfAccrualRate,
 *    sfUnearnedInterest and sfLastAccrualTime untouched: they price against the
 *    accrued value but never settle it.
 * 6. sfStruckPrice changes at most once per window and only in a dealing
 *    transaction, and sfStruckUntil only ever moves forward.
 *
 * Numbers 3 and 5 of that section need the loan book and the fee split
 * respectively, and are not enforced here.
 */
class ValidVaultAccrual
{
    struct Accrual final
    {
        Number accrualRate = Number{};
        Number unearnedInterest = Number{};
        std::uint32_t lastAccrualTime = 0;
        Number struckPrice = Number{};
        std::uint32_t struckUntil = 0;
    };

    std::vector<Accrual> before_;
    std::vector<Accrual> after_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
