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
 *       by `LoanAccept`. With or without the amendment, it must never be
 *       removed or changed.
 *    b. A loan's `lsfLoanPending` flag may only be cleared (never set) on an
 *       existing loan, and only by `LoanAccept`.
 *    c. A `LoanAccept` may only modify an existing loan when:
 *       - the loan was pending (`lsfLoanPending` set);
 *       - the submitting account (`Account`) is the loan's `Borrower`;
 *       - the loan's `StartDate` is still in the future.
 *    d. A `LoanSet` that creates a loan must use exactly one of two mutually
 *       exclusive creation paths: it either names a `Borrower` with a
 *       `StartDate`, or it carries a `CounterpartySignature`. Specifically:
 *       - A `Borrower` with a `StartDate` must not be combined with a
 *         `Counterparty` or a `CounterpartySignature`.
 *       - A `CounterpartySignature` must not be combined with a `Borrower`.
 *       - Either a `Borrower` with a `StartDate`, or a
 *         `CounterpartySignature`, must be present.
 *       - If `Borrower` is present, it must differ from the submitting
 *         `Account`.
 *    e. A `LoanSet` that creates a loan must set `lsfLoanPending` if and only
 *       if it starts the two-step flow, i.e. `Borrower` and `StartDate` are
 *       present while `Counterparty` and `CounterpartySignature` are absent.
 *    f. A `LoanSet` that creates a loan must set the loan's `Borrower`.
 *       Additionally, a pending loan's `StartDate` must be in the future (so
 *       it is still in the future when `LoanAccept` finalizes it).
 *    g. A pending loan (`lsfLoanPending` set) must not be linked into the
 *       borrower's directory (`OwnerNode` absent), and a non-pending loan must
 *       be linked (`OwnerNode` present).
 * 7. Without `featureLendingProtocolV1_2`:
 *    a. The `lsfLoanPending` flag must never be set, and `OwnerNode` must
 *       never be added to an existing loan.
 *    b. A `LoanSet` that creates a loan must not use any of the two-step
 *       flow's inputs: it must not create a pending loan, must not be given a
 *       `Borrower`, and must always carry a `CounterpartySignature`.
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
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
