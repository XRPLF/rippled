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
 * @brief Invariants: Loans are internally consistent
 *
 * 1. If `Loan.PaymentRemaining = 0` then `Loan.PrincipalOutstanding = 0`.
 * 2. A newly-created Loan against a closed-ended vault must satisfy
 *    `StartDate + PaymentInterval * PaymentRemaining < Vault.RedemptionDate`.
 * 3. An `ltLOAN` may only be created by a `ttLOAN_SET` transaction.
 * 4. Prior to `featureLendingProtocolV1_1`, the `lsfLoanOverpayment` flag on a
 *    Loan must not change. From `featureLendingProtocolV1_1` onward the same
 *    rule is enforced by `NoModifiedUnmodifiableFields`.
 * 5. Under `featureLendingProtocolV1_1`:
 *    a. An `ltLOAN` may only be deleted by a `ttLOAN_DELETE` transaction.
 *    b. If `Loan.PaymentRemaining = 0` then `Loan.NextPaymentDueDate = 0`.
 *    c. The `lsfLoanImpaired` flag may only change through a `ttLOAN_MANAGE`
 *       or `ttLOAN_PAY` transaction.
 *    d. The `lsfLoanDefault` flag may only change through a `ttLOAN_MANAGE`
 *       transaction. Combined with `NoModifiedUnmodifiableFields`, which
 *       rejects any clearing of `lsfLoanDefault`, this makes the flag
 *       write-once: `ttLOAN_MANAGE` may set it, and no transaction may
 *       clear it.
 *    e. Interest due, computed as `TotalValueOutstanding -
 *       PrincipalOutstanding - ManagementFeeOutstanding`, must not be
 *       negative.
 *    f. A Loan must reference a live `ltLOAN_BROKER`, and that broker must
 *       reference a live `ltVAULT`.
 *    g. Post-conditions for the Loan paid down by a successful `ttLOAN_PAY`:
 *       `PaymentRemaining > 0` after: neither `PrincipalOutstanding` nor
 *          `TotalValueOutstanding` increases, and at least one of them
 *          strictly decreases;
 *          `PaymentRemaining` strictly decreases;
 *          `NextPaymentDueDate` advances by N * `PaymentInterval`, N > 0.
 *       `PaymentRemaining == 0` after: pinned by checks 1 and 5b.
 *
 */
class ValidLoan
{
    // Pair is <before, after>. After is used for most of the checks, except
    // those that check changed values.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;
    // Loans removed from the ledger, in the same <before, after> form as loans_.
    // Note that `after` holds the erased entry, so it is not null.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> deletedLoans_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariants: lease loans and their lease accounts (AccountLeasing)
 *
 * A lease account is the pseudo-account (sfLoanID) of a lease loan
 * (lsfLoanLease); it sponsors the reserves of the accounts the lease creates.
 *
 * 1. Every lease account belongs to an existing lease loan whose sfAccount is
 *    that account, and a deleted lease loan deletes its lease account in the
 *    same transaction.
 * 2. A lease account's XRP balance is at least its SponsoringAccountCount times
 *    the loan's LeaseUnit.
 * 3. A lease account has OwnerCount 0, no owner directory, and no Sponsor,
 *    SponsoringOwnerCount or SponsoredOwnerCount.
 * 4. A lease account's balance changes only in LoanSet, LoanPay, LoanManage,
 *    LoanDelete and the takeover SponsorshipTransfer (tfSponsorshipReassign
 *    without Sponsor).
 * 5. A lease loan's sfAccount, LeaseUnit and lsfLoanLease never change, and
 *    LeaseClaimVault and LeaseClaimCover are 0 unless it is defaulted.
 * 6. An account gets a lease account as its Sponsor only when a Payment
 *    creates it.
 */
class ValidLease
{
    // <before, after> of AccountRoots with sfLoanID; `after` is null when
    // deleted.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> leaseAccounts_;
    // <before, after> of lease loans; `after` is null when deleted.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> leaseLoans_;
    // <before, after> of AccountRoots without sfLoanID whose Sponsor was set or
    // changed in this transaction; `before` is null when created.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> sponsorChanges_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
