#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
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

    for (auto const& [before, after] : loans_)
    {
        // A closed-ended vault must not accept a loan whose final scheduled payment falls on or
        // after the vault's RedemptionDate. This mirrors the LoanSet::preclaim gate and only fires
        // on loan creation; once the loan exists, its StartDate / PaymentInterval are immutable and
        // PaymentRemaining only decreases, so the bound is preserved.
        if (!before && isTesSuccess(result))
        {
            auto const broker = view.read(keylet::loanBroker(after->at(sfLoanBrokerID)));
            if (broker)
            {
                auto const vault = view.read(keylet::vault(broker->at(sfVaultID)));
                // We don't check for LendingProtocolV1_1 amendment because a ClosedEnded Vault will
                // not exist without the amendment enabled
                if (vault && getVaultKind(vault) == VaultKind::ClosedEnded)
                {
                    std::uint32_t const startDate = after->at(sfStartDate);
                    std::uint32_t const interval = after->at(sfPaymentInterval);
                    std::uint32_t const remaining = after->at(sfPaymentRemaining);
                    std::uint32_t const redemption = vault->at(sfRedemptionDate);
                    if (std::uint64_t{startDate} + (std::uint64_t{interval} * remaining) >=
                        redemption)
                    {
                        JLOG(j.fatal()) << "Invariant failed: closed-ended loan final payment "
                                           "must precede RedemptionDate";
                        return false;
                    }
                }
            }
        }

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
        // Note: lsfLoanOverpayment set-once immutability and lsfLoanDefault
        // set-once (never cleared) live in NoModifiedUnmodifiableFields, next
        // to the other loan-object constant-field immutability checks.

        // LoanManage sub-operation flag preconditions. These are transaction
        // post-conditions, so they only apply on a successful apply: after a
        // reset the ledger flags revert and the checks would spuriously fail.
        if (before && isTesSuccess(result) && txType == ttLOAN_MANAGE &&
            view.rules().enabled(fixCleanup3_4_0))
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

            // Item 19 (XLS-66 §3.10.5 default): a defaulted loan transitions to a
            // terminal state atomically. The lsfLoanDefault transition is
            // covered above; balance zeroing is covered by the
            // PaymentRemaining==0 rule higher up. What remains is
            // NextPaymentDueDate: LoanManage::defaultLoan clears it (sets to 0)
            // so it is dropped from the ledger entry.
            if (view.rules().enabled(featureLendingProtocolV1_1) &&
                tx.isFlag(tfLoanDefault) &&
                after->at(~sfNextPaymentDueDate).value_or(0) != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: defaulted loan must have zero "
                                   "next payment due date";
                return false;
            }
        }

        // Item 21 (XLS-66 §3.11.5 non-full payment): after a LoanPay that did
        // not fully repay the loan, PrincipalOutstanding strictly decreases,
        // PaymentRemaining decreases by at least 1, and NextPaymentDueDate
        // advances by a positive multiple of PaymentInterval. Class-2. Skip
        // the check when the payment fully repaid the loan (PaymentRemaining
        // and balances go to zero - covered by the fully-paid-off rule
        // above).
        if (before && isTesSuccess(result) && txType == ttLOAN_PAY &&
            view.rules().enabled(featureLendingProtocolV1_1) &&
            after->at(sfPaymentRemaining) != 0)
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
            // NextPaymentDueDate advances by a positive multiple of
            // PaymentInterval. PaymentInterval is immutable so before/after
            // agree; use after's value.
            std::uint32_t const beforeDue = before->at(~sfNextPaymentDueDate).value_or(0);
            std::uint32_t const afterDue = after->at(~sfNextPaymentDueDate).value_or(0);
            std::uint32_t const interval = after->at(sfPaymentInterval);
            if (afterDue <= beforeDue || interval == 0 ||
                (afterDue - beforeDue) % interval != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: loan pay must advance "
                                   "NextPaymentDueDate by a positive multiple of "
                                   "PaymentInterval on a non-full-repayment";
                return false;
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

        if (view.rules().enabled(fixCleanup3_4_0))
        {
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

    if (view.rules().enabled(fixCleanup3_4_0))
    {
        if (txType != ttLOAN_DELETE && !deletedLoans_.empty())
        {
            JLOG(j.fatal()) << "Invariant failed: Loan deleted by a transaction "
                               "other than LoanDelete";
            return false;
        }

        // Item 54 (XLS-66 §3.1.5 precondition 1): LoanBrokerDelete's preclaim
        // rejects a broker with OwnerCount != 0, so no loan should exist that
        // references it; touching any loan alongside the delete is
        // inconsistent with that precondition and points at either an
        // OwnerCount-tracking bug or a spurious cascading write. Class-2
        // (transaction post-condition); gate on isTesSuccess.
        if (isTesSuccess(result) &&
            view.rules().enabled(featureLendingProtocolV1_1) &&
            txType == ttLOAN_BROKER_DELETE && !loans_.empty())
        {
            JLOG(j.fatal()) << "Invariant failed: LoanBrokerDelete must not "
                               "touch any loan";
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

    return true;
}

}  // namespace xrpl
