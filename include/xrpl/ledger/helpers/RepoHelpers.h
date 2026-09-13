#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {
namespace repo {

/**
 * Seconds in a year, the denominator of the annualized interest rate.
 */

/**
 * A repo is active once the buyer has accepted it, which is recorded by
 * sfStartDate. Before that it is a pending offer.
 */
[[nodiscard]] inline bool
isActive(SLE::const_ref sleRepo)
{
    return sleRepo->isFieldPresent(sfStartDate);
}

/**
 * The amount the seller owes to repurchase the collateral.
 *
 * PurchasePrice * (1 + InterestRate * elapsed / kSecondsInYear), where elapsed
 * runs from StartDate to the close time, capped at MaturityDate so the seller
 * never pays for time past maturity. Computed in Number and rounded up, so
 * rounding never favours the seller.
 */
[[nodiscard]] STAmount
repurchaseAmount(SLE::const_ref sleRepo, std::uint32_t closeTime);

/**
 * Lock the collateral out of the seller's spendable balance.
 *
 * XRP is deducted from the account balance by the caller; this handles the
 * issued-asset cases the same way an escrow does.
 */
[[nodiscard]] TER
lockCollateral(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& seller,
    STAmount const& amount,
    beast::Journal journal);

/**
 * The XLS-85 checks that decide whether collateral may be locked at all.
 *
 * Both parties are checked, not just the seller: the buyer receives the
 * collateral if the repo defaults, so an unauthorized or frozen buyer would
 * leave the collateral unable to move at exactly the moment it must.
 */
[[nodiscard]] TER
checkCollateral(
    ReadView const& view,
    AccountID const& seller,
    AccountID const& buyer,
    STAmount const& collateral,
    beast::Journal journal);

/**
 * Return the locked collateral to an account and remove the entry.
 *
 * Used by cancel, close and default; they differ only in who receives the
 * collateral and whether any cash moved first.
 */
[[nodiscard]] TER
releaseAndDelete(
    ApplyViewContext ctx,
    SLE::ref sleRepo,
    AccountID const& receiver,
    beast::Journal journal);

}  // namespace repo
}  // namespace xrpl
