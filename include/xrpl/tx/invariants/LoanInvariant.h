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
 * Balance / payment bookkeeping:
 * - PaymentRemaining == 0 iff the loan is fully paid off (all of
 *   PrincipalOutstanding, TotalValueOutstanding and ManagementFeeOutstanding
 *   are zero).
 * - The STNumber fields LoanServiceFee, LatePaymentFee, ClosePaymentFee,
 *   PrincipalOutstanding, TotalValueOutstanding and ManagementFeeOutstanding
 *   must be non-negative; PeriodicPayment must be positive.
 * - Under featureLendingProtocolV1_1, interest due
 *   (TotalValueOutstanding - PrincipalOutstanding - ManagementFeeOutstanding)
 *   must be non-negative.
 *
 * Creation:
 * - A newly-created loan against a closed-ended vault must satisfy
 *   `StartDate + PaymentInterval * PaymentRemaining < Vault.RedemptionDate`.
 *
 * LoanManage (featureLendingProtocolV1_1, on tesSUCCESS):
 * - The tfLoanImpair, tfLoanUnimpair and tfLoanDefault sub-operation flags
 *   must transition the corresponding ledger flags in the expected
 *   direction (impair sets lsfLoanImpaired on a non-impaired loan,
 *   unimpair clears it, default sets lsfLoanDefault on a non-defaulted loan).
 * - A defaulted loan must have zero NextPaymentDueDate.
 *
 * LoanPay (featureLendingProtocolV1_1, on tesSUCCESS, non-full repayment):
 * - PrincipalOutstanding strictly decreases, PaymentRemaining decreases,
 *   and NextPaymentDueDate advances by a positive multiple of
 *   PaymentInterval.
 *
 * Flag immutability:
 * - Pre-featureLendingProtocolV1_1: lsfLoanOverpayment is set-once.
 *   Under featureLendingProtocolV1_1 the equivalent set-once check for
 *   lsfLoanOverpayment and the weakly-set-once check for lsfLoanDefault
 *   (may transition from unset to set only) live in
 *   NoModifiedUnmodifiableFields.
 *
 * Broker / vault linkage and deletion (featureLendingProtocolV1_1):
 * - Every loan must reference a live loan broker, and that broker must
 *   reference a live vault.
 * - A loan may only be deleted by a LoanDelete transaction, and only once
 *   it is fully paid off.
 * - A successful LoanBrokerDelete must not touch any loan (its preclaim
 *   requires OwnerCount == 0, so no loan should reference the broker).
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
