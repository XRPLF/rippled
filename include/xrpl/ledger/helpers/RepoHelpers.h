#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl::repo {

/**
 * A repo is active once the buyer has accepted it, which is recorded by
 * sfStartDate. Before that it is a pending offer.
 */
[[nodiscard]] inline bool
isActive(SLE::ConstRef sleRepo)
{
    return sleRepo->isFieldPresent(sfStartDate);
}

/**
 * The close time after which the seller can no longer repurchase and the repo
 * may be defaulted: MaturityDate plus GracePeriod, summed without wrapping.
 */
[[nodiscard]] inline std::uint64_t
defaultTime(SLE::ConstRef sleRepo)
{
    return std::uint64_t{(*sleRepo)[sfMaturityDate]} + (*sleRepo)[sfGracePeriod];
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
repurchaseAmount(SLE::ConstRef sleRepo, std::uint32_t closeTime);

/**
 * Whether the repurchase amount at the given elapsed time still fits the cash
 * asset. XRP and MPT amounts are integral and bounded; an IOU always fits.
 */
[[nodiscard]] bool
repurchaseFits(STAmount const& price, std::uint32_t rate, std::uint32_t elapsed);

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
 * Whether the payer can deliver the amount to the payee as a payment would.
 *
 * The payer must not be frozen and the payee not deep frozen, both must be
 * authorized to hold the asset (the payee under payeeAuth: StrongAuth also
 * requires it to hold the asset already), the asset must move between them,
 * and the payer must hold the amount plus any transfer fee after paying the
 * transaction fee.
 */
[[nodiscard]] TER
checkPayment(
    ReadView const& view,
    AccountID const& payer,
    AccountID const& payee,
    STAmount const& amount,
    XRPAmount fee,
    AuthType payeeAuth,
    beast::Journal journal);

/**
 * Whether the issuer still lets the receiver take the collateral: the XLS-85
 * unlock checks of authorization and deep freeze (IOU) or lock (MPT).
 */
[[nodiscard]] TER
checkRelease(ReadView const& view, AccountID const& receiver, STAmount const& collateral);

/**
 * Open the account's trust line or MPToken for the asset when it has none.
 *
 * The owner count rises without a reserve check: the account agreed to receive
 * the asset when the repo was struck, so its reserve cannot block settlement.
 */
[[nodiscard]] TER
ensureHolding(
    ApplyViewContext ctx,
    AccountID const& account,
    Asset const& asset,
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
    SLE::Ref sleRepo,
    AccountID const& receiver,
    beast::Journal journal);

}  // namespace xrpl::repo
