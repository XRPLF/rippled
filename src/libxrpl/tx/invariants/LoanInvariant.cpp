/** @file
 *  Implements the `ValidLoan` invariant checker for `ltLOAN` ledger objects
 *  introduced by the XLS-66d Lending Protocol.
 *
 *  This checker runs after every transaction — including failing ones — and
 *  enforces five numeric and state-consistency invariants on every `ltLOAN`
 *  entry touched by the transaction. A `false` return from `finalize` causes
 *  the transaction to be rolled back and escalated to `tecINVARIANT_FAILED`.
 *  See `LoanInvariant.h` for the full invariant specification.
 */
#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>

namespace xrpl {

void
ValidLoan::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltLOAN)
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

    for (auto const& [before, after] : loans_)
    {
        // XLS-66d §3.2.2.3: sfPaymentRemaining == 0 iff all outstanding
        // balance fields are zero (payment schedule and balances must agree).
        // https://github.com/Tapanito/XRPL-Standards/blob/xls-66-lending-protocol/XLS-0066d-lending-protocol/README.md#3223-invariants
        if (after->at(sfPaymentRemaining) == 0 &&
            (after->at(sfTotalValueOutstanding) != beast::kZERO ||
             after->at(sfPrincipalOutstanding) != beast::kZERO ||
             after->at(sfManagementFeeOutstanding) != beast::kZERO))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan with zero payments "
                               "remaining has not been paid off";
            return false;
        }
        if (after->at(sfPaymentRemaining) != 0 &&
            after->at(sfTotalValueOutstanding) == beast::kZERO &&
            after->at(sfPrincipalOutstanding) == beast::kZERO &&
            after->at(sfManagementFeeOutstanding) == beast::kZERO)
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
    }
    return true;
}

}  // namespace xrpl
