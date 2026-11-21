#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <optional>

namespace xrpl {

/**
 * Whether the Loan is a lease loan: its principal sits on its own
 * pseudo-account (the lease account), which sponsors the reserves of the
 * accounts the lease creates (AccountLeasing).
 */
[[nodiscard]] bool
isLeaseLoan(SLE const& loan);

/**
 * Whether the Loan is paid off: no payment remains and it is not defaulted.
 * A default also sets PaymentRemaining to 0, so the flag check is needed.
 */
[[nodiscard]] bool
isLoanPaidOff(SLE const& loan);

/**
 * The lease loan whose lease account is `sponsorID`.
 *
 * @return The Loan, or a null pointer when `sponsorID` is not the lease
 *         account of an existing lease loan.
 */
[[nodiscard]] SLE::const_pointer
leaseLoanOfSponsor(ReadView const& view, AccountID const& sponsorID);

/**
 * The account that acts for a lease account toward its leased accounts: the
 * Loan's borrower while the Loan is not defaulted, the LoanBroker's owner
 * while it is. A pseudo-account cannot sign, so this account ends, reaps or
 * receives in its place.
 *
 * @return The acting account, or nullopt when `sponsorID` is not the lease
 *         account of an existing lease loan.
 */
[[nodiscard]] std::optional<AccountID>
leaseActingSponsor(ReadView const& view, AccountID const& sponsorID);

/**
 * The XRP a lease account holds for its leased accounts: its
 * SponsoringAccountCount times the Loan's LeaseUnit.
 */
[[nodiscard]] XRPAmount
leaseLocked(SLE const& leaseAccount, SLE const& loan);

/**
 * The XRP on a lease account above leaseLocked, or 0 when its balance is
 * below that.
 */
[[nodiscard]] XRPAmount
leaseFree(SLE const& leaseAccount, SLE const& loan);

/**
 * Send a defaulted lease loan's free XRP to the vault up to LeaseClaimVault,
 * then to the broker's cover up to LeaseClaimCover, then the rest to the
 * borrower, lowering each claim by what it gets.
 *
 * @param view   The apply view.
 * @param loan   The defaulted lease Loan.
 * @param broker The Loan's LoanBroker.
 * @param vault  The LoanBroker's Vault.
 * @param j      Journal.
 * @return tesSUCCESS, or the error of a send or of the vault update.
 */
[[nodiscard]] TER
leaseRecover(ApplyView& view, SLE::Ref loan, SLE::Ref broker, SLE::Ref vault, beast::Journal j);

/**
 * Send a paid-off lease loan's free XRP to its borrower.
 *
 * @param view The apply view.
 * @param loan The paid-off lease Loan.
 * @param j    Journal.
 * @return tesSUCCESS, or the error of the send.
 */
[[nodiscard]] TER
leasePayFreeToBorrower(ApplyView& view, SLE::Ref loan, beast::Journal j);

}  // namespace xrpl
