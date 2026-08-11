#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace xrpl {

void
ValidLoan::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (isDelete)
    {
        // `before` holds the loan's state before deletion.
        if (before && before->getType() == ltLOAN)
            deletedLoans_.emplace_back(before);
    }
    else if (after && after->getType() == ltLOAN)
    {
        loans_.emplace_back(before, after);
    }
}

bool
ValidLoan::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loans will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    auto const txType = tx.getTxnType();

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
        // A loan must reference a live loan broker, and that broker must
        // reference a live vault; otherwise the loan is orphaned and its
        // balances have no counterparty on the ledger.
        auto const brokerSle = view.read(keylet::loanBroker(after->at(sfLoanBrokerID)));
        if (!brokerSle)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan broker does not exist";
            return false;
        }
        if (!view.read(keylet::vault(brokerSle->at(sfVaultID))))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan broker vault does not exist";
            return false;
        }
    }

    if (txType != ttLOAN_DELETE && !deletedLoans_.empty())
    {
        JLOG(j.fatal()) << "Invariant failed: Loan deleted by a transaction "
                           "other than LoanDelete";
        return false;
    }

    // A loan may only be deleted by a LoanDelete transaction, and only once it
    // is fully paid off (no payments remaining). Deleting a loan with
    // outstanding obligations is a violation.
    return std::ranges::all_of(deletedLoans_, [&](auto const& loan) {
        if (loan->at(sfPaymentRemaining) != 0 ||
            loan->at(sfTotalValueOutstanding) != beast::kZero ||
            loan->at(sfPrincipalOutstanding) != beast::kZero ||
            loan->at(sfManagementFeeOutstanding) != beast::kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan deleted while not fully "
                               "paid off";
            return false;
        }
        return true;
    });
}

}  // namespace xrpl
