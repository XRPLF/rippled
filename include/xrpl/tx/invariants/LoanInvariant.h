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
 *    Loan must not change. From `featureLendingProtocolV1_1` onward this check
 *    is enforced in `InvariantChecks.cpp`.
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
 *
 */
class ValidLoan
{
    // Pair is <before, after>. After is used for most of the checks, except
    // those that check changed values.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;
    // Loans removed from the ledger, in the same <before, after> form as
    // loans_. Prior to featureLendingProtocolV1_1 these are validated by the
    // same per-entry checks as any other modified Loan; from V1_1 onward they
    // are only used to enforce that a Loan is deleted by ttLOAN_DELETE alone.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> deletedLoans_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
