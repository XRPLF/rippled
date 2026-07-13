#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

void
ValidLoan::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (isDelete)
    {
        // On deletion `after` holds the loan's final state.
        if (after && after->getType() == ltLOAN)
            deletedLoans_.emplace_back(after);
    }
    else if (after && after->getType() == ltLOAN)
    {
        loans_.emplace_back(before, after);
    }
}

bool
ValidLoan::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loans will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    auto const txType = tx.getTxnType();
    // The pending-loan checks below only apply once the two-step flow exists.
    bool const lendingV11Enabled = view.rules().enabled(featureLendingProtocolV1_1);

    for (auto const& [before, after] : loans_)
    {
        // https://github.com/Tapanito/XRPL-Standards/blob/xls-66-lending-protocol/XLS-0066d-lending-protocol/README.md#3223-invariants
        // If `Loan.PaymentRemaining = 0` then the loan MUST be fully paid off
        if (after->at(sfPaymentRemaining) == 0 &&
            (after->at(sfTotalValueOutstanding) != beast::kZero ||
             after->at(sfPrincipalOutstanding) != beast::kZero ||
             after->at(sfManagementFeeOutstanding) != beast::kZero))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan with zero payments "
                               "remaining has not been paid off";
            return false;
        }
        // If `Loan.PaymentRemaining != 0` then the loan MUST NOT be fully paid
        // off
        if (after->at(sfPaymentRemaining) != 0 &&
            after->at(sfTotalValueOutstanding) == beast::kZero &&
            after->at(sfPrincipalOutstanding) == beast::kZero &&
            after->at(sfManagementFeeOutstanding) == beast::kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: Fully paid off Loan still has payments remaining";
            return false;
        }
        if (before && (before->isFlag(lsfLoanOverpayment) != after->isFlag(lsfLoanOverpayment)))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Overpayment flag changed";
            return false;
        }
        if (before)
        {
            // The lsfLoanPending flag may only be cleared (finalising the
            // loan), and only by LoanAccept. It must never be set on an
            // existing loan.
            bool const wasPending = before->isFlag(lsfLoanPending);
            bool const isPending = after->isFlag(lsfLoanPending);

            if (!lendingV11Enabled && (wasPending || isPending))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Pending flag changed "
                                   "when the amendment is not enabled";
                return false;
            }

            if (wasPending != isPending &&
                (!lendingV11Enabled || txType != ttLOAN_ACCEPT || (!wasPending && isPending)))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Pending flag changed "
                                   "by an unauthorized transaction";
                return false;
            }

            // LoanAccept may only process a loan that was pending.
            if (txType == ttLOAN_ACCEPT && !wasPending)
            {
                JLOG(j.fatal()) << "Invariant failed: LoanAccept modified a "
                                   "Loan that was not pending";
                return false;
            }

            // The OwnerNode may only be added to an existing loan, and only by
            // LoanAccept (which links the loan into the borrower's directory
            // once accepted). It must never be removed or changed.
            bool const beforeHasNode = before->isFieldPresent(sfOwnerNode);
            bool const afterHasNode = after->isFieldPresent(sfOwnerNode);
            if (beforeHasNode &&
                (!afterHasNode ||
                 before->getFieldU64(sfOwnerNode) != after->getFieldU64(sfOwnerNode)))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan OwnerNode removed "
                                   "or changed";
                return false;
            }
            if (!beforeHasNode && afterHasNode && (!lendingV11Enabled || txType != ttLOAN_ACCEPT))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan OwnerNode added "
                                   "by an unauthorized transaction";
                return false;
            }

            // LoanAccept must be submitted by the loan's Borrower, and may only
            // finalise a loan whose StartDate is still in the future.
            if (lendingV11Enabled && txType == ttLOAN_ACCEPT)
            {
                if (after->getAccountID(sfBorrower) != tx.getAccountID(sfAccount))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanAccept submitted "
                                       "by an account other than the Borrower";
                    return false;
                }
                if (after->getFieldU32(sfStartDate) <=
                    view.parentCloseTime().time_since_epoch().count())
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanAccept processed a "
                                       "Loan whose StartDate is not in the future";
                    return false;
                }
            }
        }
        // On creation, LoanSet must use exactly one of two mutually exclusive
        // paths: it either names a Borrower with a StartDate (starting the
        // two-step flow) or carries a CounterpartySignature. A Borrower with a
        // StartDate and no CounterpartySignature or Counterparty starts the
        // two-step flow and must create a pending loan; any other LoanSet must
        // create an active (non-pending) loan.
        if (isTesSuccess(result))
        {
            if (lendingV11Enabled && !before && txType == ttLOAN_SET)
            {
                bool const hasBorrower = tx.isFieldPresent(sfBorrower);
                bool const hasCounterpartySig = tx.isFieldPresent(sfCounterpartySignature);
                bool const hasStartDate = tx.isFieldPresent(sfStartDate);
                bool const hasCounterparty = tx.isFieldPresent(sfCounterparty);
                bool isTwoStepFlow = hasBorrower && hasStartDate;
                bool isOneStepFlow = hasCounterpartySig;

                if (!isTwoStepFlow && !isOneStepFlow)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet specified neither "
                                       "a Borrower with a StartDate nor a CounterpartySignature";
                    return false;
                }

                if (isTwoStepFlow && isOneStepFlow)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet specified both "
                                       "a Borrower with a StartDate and a CounterpartySignature";
                    return false;
                }

                if (isOneStepFlow && hasBorrower)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet specified a "
                                       "Borrower with a CounterpartySignature";
                    return false;
                }

                if (isTwoStepFlow && hasCounterparty)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet specified a "
                                       "Borrower with a StartDate and a Counterparty";
                    return false;
                }

                // In the two-step flow the named Borrower must be a different
                // account from the one submitting the LoanSet.
                if (hasBorrower && tx.getAccountID(sfBorrower) == tx.getAccountID(sfAccount))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet Borrower is the "
                                       "submitting account";
                    return false;
                }

                // The two-step flow is started (and the loan created pending) when
                // the LoanSet names a Borrower and a StartDate but carries neither a
                // Counterparty nor a CounterpartySignature.
                bool const shouldPend =
                    hasBorrower && hasStartDate && !hasCounterpartySig && !hasCounterparty;
                if (shouldPend != after->isFlag(lsfLoanPending))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet pending flag does "
                                       "not match the two-step flow inputs";
                    return false;
                }

                // A pending loan (the two-step flow) is accepted in a later ledger,
                // so its StartDate must be strictly in the future at creation to
                // remain in the future when LoanAccept finalizes it.
                if (shouldPend &&
                    after->getFieldU32(sfStartDate) <=
                        view.parentCloseTime().time_since_epoch().count())
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet created a pending "
                                       "Loan whose StartDate is not in the future";
                    return false;
                }

                // A created loan must always record its Borrower. As sfBorrower is a
                // required field it is always present (defaulting to the zero
                // account), so a loan whose Borrower was never set carries the zero
                // account.
                if (!after->isFieldPresent(sfBorrower) ||
                    after->getAccountID(sfBorrower) == beast::kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet did not set the "
                                       "Loan Borrower";
                    return false;
                }
            }
            // Without the two-step flow amendment, LoanSet must not make use of any
            // of its inputs: it must not create a pending loan, must not be given a
            // Borrower, and must always carry a CounterpartySignature.
            if (!lendingV11Enabled && !before && txType == ttLOAN_SET)
            {
                if (after->isFlag(lsfLoanPending))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet set the Loan "
                                       "Pending flag when the amendment is not enabled";
                    return false;
                }
                if (tx.isFieldPresent(sfBorrower))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet specified a "
                                       "Borrower when the amendment is not enabled";
                    return false;
                }
                if (!tx.isFieldPresent(sfCounterpartySignature))
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet omitted the "
                                       "CounterpartySignature when the amendment is "
                                       "not enabled";
                    return false;
                }
            }
        }
        // Must not be negative - STNumber
        for (auto const field :
             {&sfLoanServiceFee,
              &sfLatePaymentFee,
              &sfClosePaymentFee,
              &sfPrincipalOutstanding,
              &sfTotalValueOutstanding,
              &sfManagementFeeOutstanding})
        {
            if (after->at(*field) < 0)
            {
                JLOG(j.fatal()) << "Invariant failed: " << field->getName() << " is negative ";
                return false;
            }
        }
        // Interest due (the total value owed less principal and management fee)
        // must never be negative.
        if (after->at(sfTotalValueOutstanding) - after->at(sfPrincipalOutstanding) -
                after->at(sfManagementFeeOutstanding) <
            beast::kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan interest due is negative";
            return false;
        }
        // Must be positive - STNumber
        for (auto const field : {
                 &sfPeriodicPayment,
             })
        {
            if (after->at(*field) <= 0)
            {
                JLOG(j.fatal()) << "Invariant failed: " << field->getName()
                                << " is zero or negative ";
                return false;
            }
        }
        // A pending loan must not be linked into the borrower's directory, and
        // a non-pending loan must be linked.
        if (lendingV11Enabled)
        {
            bool const isPending = after->isFlag(lsfLoanPending);
            bool const hasNode = after->isFieldPresent(sfOwnerNode);
            if (isPending && hasNode)
            {
                JLOG(j.fatal()) << "Invariant failed: pending Loan is linked "
                                   "into the borrower's directory";
                return false;
            }
            if (!isPending && !hasNode)
            {
                JLOG(j.fatal()) << "Invariant failed: active Loan is not linked "
                                   "into the borrower's directory";
                return false;
            }
        }
    }

    // A loan may only be deleted by a LoanDelete transaction, and only once it
    // is fully paid off (no payments remaining) or, once the two-step flow
    // exists, while it is still pending acceptance. Deleting an active loan with
    // outstanding obligations is a violation.
    for (auto const& loan : deletedLoans_)
    {
        if (txType != ttLOAN_DELETE)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan deleted by a transaction "
                               "other than LoanDelete";
            return false;
        }

        bool const wasPending = loan->isFlag(lsfLoanPending);
        // A pending loan cannot exist while the amendment is not enabled.
        if (wasPending && !lendingV11Enabled)
        {
            JLOG(j.fatal()) << "Invariant failed: pending Loan deleted when the "
                               "amendment is not enabled";
            return false;
        }

        bool const paidOff = loan->at(sfPaymentRemaining) == 0 &&
            loan->at(sfTotalValueOutstanding) == beast::kZero &&
            loan->at(sfPrincipalOutstanding) == beast::kZero &&
            loan->at(sfManagementFeeOutstanding) == beast::kZero;
        bool const pending = lendingV11Enabled && wasPending;
        if (!paidOff && !pending)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan deleted while not fully "
                               "paid off and not pending";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
