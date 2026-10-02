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
 * 6. Under `featureLendingProtocolV1_2` (the two-step "pending loan" flow):
 *    a. A loan's `OwnerNode` may only be added to an existing loan, and only
 *       by `LoanAccept`. It must never be removed or changed.
 *    b. A loan's `lsfLoanPending` flag may only be cleared (never set) on an
 *       existing loan, and only by `LoanAccept`.
 *       More broadly, a pending loan may only be modified by `LoanAccept`
 *       (it may still be deleted by `LoanDelete`).
 *    c. A `LoanAccept` may only modify an existing loan when:
 *       - the loan was pending (`lsfLoanPending` set);
 *       - it finalizes the loan (clears `lsfLoanPending`);
 *       - the loan's `StartDate` is still in the future.
 *    d. A `LoanSet` that creates a pending loan must give it a `StartDate` in
 *       the future (so it is still in the future when `LoanAccept` finalizes
 *       it).
 *    e. A pending loan (`lsfLoanPending` set) must not be linked into the
 *       borrower's directory (`OwnerNode` absent), and a non-pending loan must
 *       be linked (`OwnerNode` present).
 *    f. A loan must have a non-zero `Borrower` and `StartDate`.
 * 7. With or without `featureLendingProtocolV1_2`, a `LoanSet` that creates
 *    a loan must record what the transaction specified:
 *    - If `StartDate` is present, the loan's `StartDate` must match it.
 *    - If `Borrower` is present, the loan's `Borrower` must match it.
 *    These checks do not re-derive which flow `LoanSet` used. In particular
 *    they do not require a `CounterpartySignature` for an active loan, as the
 *    counterparty may instead authorize it by signing an outer `Batch` or a
 *    Cosign proposal, which the invariant cannot see.
 * 8. Without `featureLendingProtocolV1_2`, a `LoanSet` must not create a
 *    pending loan. The checks on existing loans in 6 do not apply.
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

}  // namespace xrpl
