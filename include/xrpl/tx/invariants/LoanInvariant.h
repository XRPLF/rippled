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
 * 1. If `Loan.PaymentRemaining = 0` then `Loan.PrincipalOutstanding = 0`
 *
 * The following invariants only apply once the `LendingProtocolV1_1`
 * amendment is enabled (the two-step "pending loan" flow):
 *
 * 2. A loan's `OwnerNode` may only be added, removed, or changed on an
 *    existing loan by `LoanAccept`.
 * 3. A loan's `lsfLoanPending` flag may only be cleared (never set) on an
 *    existing loan, and only by `LoanAccept`.
 * 4. A `LoanAccept` may only modify an existing loan when:
 *      a. the loan was pending (`lsfLoanPending` set);
 *      b. the submitting account (`Account`) is the loan's `Borrower`;
 *      c. the loan's `StartDate` is still in the future.
 * 5. A `LoanSet` that creates a loan must use exactly one of two mutually
 *    exclusive creation paths: it either names a `Borrower`, or it carries a
 *    `Counterparty` together with a `CounterpartySignature`. Specifically:
 *      a. If `Borrower` is present, `Counterparty` and `CounterpartySignature`
 *         must be absent.
 *      b. If `Counterparty` and `CounterpartySignature` are present, `Borrower`
 *         must be absent.
 *      c. Either `Borrower`, or both `Counterparty` and `CounterpartySignature`,
 *         must be present.
 * 6. A `LoanSet` that creates a loan must set `lsfLoanPending` if and only if
 *    `Borrower` is present and `CounterpartySignature` is absent.
 * 7. A `LoanSet` that creates a loan must set the loan's `Borrower`.
 * 8. A pending loan (`lsfLoanPending` set) must not be linked into the
 *    borrower's directory (`OwnerNode` absent), and a non-pending loan must
 *    be linked (`OwnerNode` present).
 *
 * While the `LendingProtocolV1_1` amendment is not enabled, a `LoanSet` that
 * creates a loan must not use any of the two-step flow's inputs:
 *
 * 9. It must not create a pending loan (`lsfLoanPending` must be clear).
 * 10. It must not be given a `Borrower`.
 * 11. It must always carry a `CounterpartySignature`.
 *
 * A loan may only be deleted once it is fully paid off (no payments remaining)
 * or, once the `LendingProtocolV1_1` amendment is enabled, while it is still
 * pending:
 *
 * 12. A loan may only be deleted by a `LoanDelete` transaction.
 * 13. A pending loan must not be deleted while the amendment is not enabled.
 * 14. A loan that is neither fully paid off nor pending must not be deleted.
 *
 */
class ValidLoan
{
    // Pair is <before, after>. After is used for most of the checks, except
    // those that check changed values.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;
    // Loans removed from the ledger (final state captured at deletion).
    std::vector<SLE::const_pointer> deletedLoans_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
