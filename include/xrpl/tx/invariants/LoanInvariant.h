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
 * 4. A `LoanSet` that creates a loan must set `lsfLoanPending` if and only if
 *    `Borrower` is present and `CounterpartySignature` is absent.
 * 5. A pending loan (`lsfLoanPending` set) must not be linked into the
 *    borrower's directory (`OwnerNode` absent), and a non-pending loan must
 *    be linked (`OwnerNode` present).
 *
 * While the `LendingProtocolV1_1` amendment is not enabled, a `LoanSet` that
 * creates a loan must not use any of the two-step flow's inputs:
 *
 * 6. It must not create a pending loan (`lsfLoanPending` must be clear).
 * 7. It must not be given a `Borrower`.
 * 8. It must always carry a `CounterpartySignature`.
 *
 * A loan may only be deleted once it is fully paid off (no payments remaining)
 * or, once the `LendingProtocolV1_1` amendment is enabled, while it is still
 * pending:
 *
 * 9. A loan may only be deleted by a `LoanDelete` transaction.
 * 10. A pending loan must not be deleted while the amendment is not enabled.
 * 11. A loan that is neither fully paid off nor pending must not be deleted.
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
