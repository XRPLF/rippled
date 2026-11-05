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
 * 2. A loan's sfLastAccrualTime never decreases and changes only in a
 *    ttLOAN_* transaction, which is where a loan settles; the vault's
 *    sfAccrualRate, sfUnearnedInterest and sfAccrualAnchor change only there.
 * 4. VaultDeposit, VaultWithdraw and VaultClawback leave sfAccrualRate,
 *    sfUnearnedInterest and sfAccrualAnchor untouched: they price against the
 *    accrued value but never settle it.
 * 6. The struck prices are struck at most once per window by a dealing
 *    transaction, struck again only by a LoanManage that changes the vault's
 *    value inside that window, and sfStruckUntil only ever moves forward.
 *
 * And, for a ttLOAN_* transaction on an accrual vault, sfAssetsTotal rises by
 * no more than the change in its loans' sfAccruedInterest plus the cash the
 * vault received, to the last digit of sfAssetsTotal: a loan transaction never
 * recognizes interest twice.
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
        Number accrualAnchor = Number{};
        Number struckPrice = Number{};
        Number struckDepositPrice = Number{};
        std::uint32_t struckUntil = 0;
        Number assetsTotal = Number{};
        Number assetsAvailable = Number{};
    };

    std::vector<Accrual> before_;
    std::vector<Accrual> after_;
    SLE::const_pointer beforeVault_;
    bool loanClockBackwards_ = false;
    bool loanClockMoved_ = false;
    Number loanAccruedChange_ = Number{};

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
