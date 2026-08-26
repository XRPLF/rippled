#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>

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
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loans will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    auto const txType = tx.getTxnType();
    bool const v1Enabled = view.rules().enabled(featureLendingProtocolV1_1);

    // Ledger entry validation checks.
    for (auto const& [before, after] : loans_)
    {
        // Only LoanSet may create a loan. This is an object-existence rule, not
        // a transaction post-condition, so it applies even when apply failed.
        if (!before && txType != ttLOAN_SET)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan created by a transaction "
                               "other than LoanSet";
            return false;
        }

        // If `Loan.PaymentRemaining = 0` then the loan MUST be fully paid off
        if (after->at(sfPaymentRemaining) == 0)
        {
            if ((after->at(sfTotalValueOutstanding) != beast::kZero ||
                 after->at(sfPrincipalOutstanding) != beast::kZero ||
                 after->at(sfManagementFeeOutstanding) != beast::kZero))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan with zero payments "
                                   "remaining has not been paid off";
                return false;
            }
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

        if (!v1Enabled)
        {
            if (before && before->isFlag(lsfLoanOverpayment) != after->isFlag(lsfLoanOverpayment))
            {
                JLOG(j.fatal()) << "Invariant failed: Loan Overpayment flag changed";
                return false;
            }
        }
        else
        {
            if (after->at(sfPaymentRemaining) == 0 &&
                after->at(~sfNextPaymentDueDate).value_or(0) != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan with zero payments must have zero next "
                                   "payment due date";
                return false;
            }

            if (before)
            {
                bool const wasImpaired = before->isFlag(lsfLoanImpaired);
                bool const isImpaired = after->isFlag(lsfLoanImpaired);
                bool const wasDefaulted = before->isFlag(lsfLoanDefault);
                bool const isDefaulted = after->isFlag(lsfLoanDefault);

                if (wasImpaired != isImpaired && txType != ttLOAN_MANAGE && txType != ttLOAN_PAY)
                {
                    JLOG(j.fatal()) << "Invariant failed: lsfLoanImpaired changed "
                                       "outside LoanManage or LoanPay";
                    return false;
                }
                if (wasDefaulted != isDefaulted && txType != ttLOAN_MANAGE)
                {
                    JLOG(j.fatal()) << "Invariant failed: lsfLoanDefault changed "
                                       "outside LoanManage";
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
    }

    // Deletion by the wrong transaction is an invalid object transition even
    // when apply failed, so check it before the success-only post-conditions.
    if (v1Enabled && txType != ttLOAN_DELETE && !deletedLoans_.empty())
    {
        JLOG(j.fatal()) << "Invariant failed: Loan deleted by a transaction "
                           "other than LoanDelete";
        return false;
    }

    // Transaction success post-conditions.
    if (!isTesSuccess(result))
        return true;

    if (!v1Enabled)
        return true;

    if (txType == ttLOAN_SET)
    {
        if (loans_.size() != 1 || !deletedLoans_.empty())
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: lending transaction must touch exactly one loan";
            return false;
        }

        auto const& [before, after] = loans_.front();
        if (before)
        {
            JLOG(j.fatal()) << "Invariant failed: lending transaction must not modify an "
                               "existing loan";
            return false;
        }

        if (after->at(sfPrincipalOutstanding) != tx[sfPrincipalRequested])
        {
            JLOG(j.fatal()) << "Invariant failed: loan set principal outstanding must equal "
                               "principal requested";
            return false;
        }

        // A closed-ended vault must not accept a loan whose final scheduled
        // payment falls on or after its RedemptionDate.
        auto const broker = view.read(keylet::loanBroker(after->at(sfLoanBrokerID)));
        auto const vault = broker ? view.read(keylet::vault(broker->at(sfVaultID))) : nullptr;
        if (vault && getVaultKind(vault) == VaultKind::ClosedEnded)
        {
            std::uint32_t const startDate = after->at(sfStartDate);
            std::uint32_t const interval = after->at(sfPaymentInterval);
            std::uint32_t const remaining = after->at(sfPaymentRemaining);
            std::uint32_t const redemption = vault->at(sfRedemptionDate);
            if (std::uint64_t{startDate} + (std::uint64_t{interval} * remaining) >= redemption)
            {
                JLOG(j.fatal()) << "Invariant failed: closed-ended loan final payment "
                                   "must precede RedemptionDate";
                return false;
            }
        }
    }
    else if (txType == ttLOAN_MANAGE || txType == ttLOAN_PAY)
    {
        if (loans_.size() != 1 || !deletedLoans_.empty())
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: lending transaction must touch exactly one loan";
            return false;
        }

        auto const& [before, after] = loans_.front();
        if (txType == ttLOAN_MANAGE)
        {
            bool const wasImpaired = before->isFlag(lsfLoanImpaired);
            bool const isImpaired = after->isFlag(lsfLoanImpaired);
            bool const wasDefaulted = before->isFlag(lsfLoanDefault);
            bool const isDefaulted = after->isFlag(lsfLoanDefault);

            if (tx.isFlag(tfLoanImpair) && (wasImpaired || !isImpaired))
            {
                JLOG(j.fatal()) << "Invariant failed: LoanManage(tfLoanImpair) "
                                   "must set lsfLoanImpaired on a non-impaired loan";
                return false;
            }
            if (tx.isFlag(tfLoanUnimpair) && (!wasImpaired || isImpaired))
            {
                JLOG(j.fatal()) << "Invariant failed: LoanManage(tfLoanUnimpair) "
                                   "must clear lsfLoanImpaired on an impaired loan";
                return false;
            }
            if (tx.isFlag(tfLoanDefault) && (wasDefaulted || !isDefaulted))
            {
                JLOG(j.fatal()) << "Invariant failed: LoanManage(tfLoanDefault) "
                                   "must newly set lsfLoanDefault";
                return false;
            }
        }
        else if (after->at(sfPaymentRemaining) != 0)
        {
            if (!(after->at(sfPrincipalOutstanding) < before->at(sfPrincipalOutstanding)))
            {
                JLOG(j.fatal()) << "Invariant failed: loan pay must strictly decrease "
                                   "PrincipalOutstanding on a non-full-repayment";
                return false;
            }
            if (!(after->at(sfPaymentRemaining) < before->at(sfPaymentRemaining)))
            {
                JLOG(j.fatal()) << "Invariant failed: loan pay must decrease "
                                   "PaymentRemaining on a non-full-repayment";
                return false;
            }

            std::uint32_t const beforeDue = before->at(~sfNextPaymentDueDate).value_or(0);
            std::uint32_t const afterDue = after->at(~sfNextPaymentDueDate).value_or(0);
            std::uint32_t const interval = after->at(sfPaymentInterval);
            if (afterDue <= beforeDue || interval == 0 || (afterDue - beforeDue) % interval != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: loan pay must advance "
                                   "NextPaymentDueDate by a positive multiple of "
                                   "PaymentInterval on a non-full-repayment";
                return false;
            }
        }
    }
    else if (txType == ttLOAN_DELETE)
    {
        // A loan may only be deleted once it is fully paid off.
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
    else if (txType == ttLOAN_BROKER_DELETE && !loans_.empty())
    {
        JLOG(j.fatal()) << "Invariant failed: LoanBrokerDelete must not touch any loan";
        return false;
    }

    return true;
}

}  // namespace xrpl
