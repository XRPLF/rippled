#include <xrpl/tx/invariants/LoanInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

void
ValidLoan::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // Classify here, but leave the decision about which checks apply to
    // finalize(), which is the only place that can see the Rules.
    if (isDelete)
    {
        if (before && before->getType() == ltLOAN)
            deletedLoans_.emplace_back(before, after);
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
    bool const lpV11Enabled = view.rules().enabled(featureLendingProtocolV1_1);

    // Without featureLendingProtocolV1_1 an erased Loan is subject to the same
    // per-entry checks as any modified Loan. From V1_1 onward it is only subject
    // to the ttLOAN_DELETE check below.
    if (!lpV11Enabled)
        loans_.insert(loans_.end(), deletedLoans_.begin(), deletedLoans_.end());

    // Ledger entry validation checks.
    for (auto const& [before, after] : loans_)
    {
        // A closed-ended vault must not accept a loan whose final scheduled payment falls fewer
        // than kLoanRedemptionBuffer seconds before the vault's RedemptionDate. This mirrors the
        // LoanSet::preclaim gate and only fires on loan creation; once the loan exists, its
        // StartDate / PaymentInterval are immutable and PaymentRemaining only decreases, so the
        // bound is preserved.
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
                    if (std::uint64_t{startDate} + (std::uint64_t{interval} * remaining) +
                            kLoanRedemptionBuffer >
                        redemption)
                    {
                        JLOG(j.fatal()) << "Invariant failed: closed-ended loan final payment "
                                           "must precede RedemptionDate by at least "
                                           "kLoanRedemptionBuffer";
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

        // From featureLendingProtocolV1_1 onwards this flag is immutable by way of
        // NoModifiedUnmodifiableFields.
        if (!lpV11Enabled && before &&
            (before->isFlag(lsfLoanOverpayment) != after->isFlag(lsfLoanOverpayment)))
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
        if (lpV11Enabled)
        {
            // Only LoanSet may create a loan.
            if (!before && txType != ttLOAN_SET)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan created by a transaction "
                                   "other than LoanSet";
                return false;
            }

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

            // A loan must reference a live loan broker, and that broker must
            // reference a live vault; otherwise the loan is orphaned and its
            // balances have no counterparty on the ledger.
            auto const brokerSle = view.read(keylet::loanBroker(after->at(sfLoanBrokerID)));
            if (!brokerSle)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan broker does not exist";
                return false;
            }
            auto const vaultSle = view.read(keylet::vault(brokerSle->at(sfVaultID)));
            if (!vaultSle)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan broker vault does not exist";
                return false;
            }

            // Interest due (the total value owed less principal and management fee)
            // must never be negative. TotalValueOutstanding, PrincipalOutstanding and
            // ManagementFeeOutstanding are each independently rounded to sfLoanScale
            // by the accounting code, so their difference can carry one unit of
            // quantization noise even when the underlying flow is correct. Absorb
            // one unit at that scale, matching the pattern used in ValidVault.
            auto const interestDue = after->at(sfTotalValueOutstanding) -
                after->at(sfPrincipalOutstanding) - after->at(sfManagementFeeOutstanding);

            // Only IOU amounts can accumulate STAmount quantization noise. For integral-domain
            // assets (XRP/MPT) enforce the boundary strictly.
            bool const integral = Asset{vaultSle->at(sfAsset)}.integral();

            Number const tolerance = integral ? Number{} : Number{-1, after->at(sfLoanScale)};
            if (interestDue < tolerance)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan interest due is negative";
                return false;
            }

            // Transaction success post-conditions. A successful loan pay makes at least
            // one scheduled payment, so a loan left with payments still outstanding
            // must show that payment in its balance and schedule. A payment that clears
            // the loan outright instead drives PaymentRemaining to zero, which the
            // fully-paid-off and zero due-date checks above pin.
            if (isTesSuccess(result) && txType == ttLOAN_PAY)
            {
                if (before && after->at(sfPaymentRemaining) != 0)
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
                    if (afterDue <= beforeDue || interval == 0 ||
                        (afterDue - beforeDue) % interval != 0)
                    {
                        JLOG(j.fatal()) << "Invariant failed: loan pay must advance "
                                           "NextPaymentDueDate by a positive multiple of "
                                           "PaymentInterval on a non-full-repayment";
                        return false;
                    }
                }
            }
        }
    }

    // Only LoanDelete may delete a loan.
    if (lpV11Enabled && txType != ttLOAN_DELETE && !deletedLoans_.empty())
    {
        JLOG(j.fatal()) << "Invariant failed: Loan deleted by a transaction "
                           "other than LoanDelete";
        return false;
    }
    return true;
}

}  // namespace xrpl
