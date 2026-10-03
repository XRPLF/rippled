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
ValidLoan::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
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
    // The two-step (pending loan) flow only exists from
    // featureLendingProtocolV1_2 onwards.
    bool const lpV12Enabled = view.rules().enabled(featureLendingProtocolV1_2);

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
        // The pending-loan checks on existing loans only apply once the two-step
        // flow exists.
        if (lpV12Enabled && before)
        {
            // The lsfLoanPending flag may only be cleared (finalising the
            // loan), and only by LoanAccept. It must never be set on an
            // existing loan.
            bool const wasPending = before->isFlag(lsfLoanPending);
            bool const isPending = after->isFlag(lsfLoanPending);

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

            // LoanAccept may only finalise a pending loan, and only while its
            // StartDate is still in the future.
            if (txType == ttLOAN_ACCEPT)
            {
                // LoanAccept may only process a loan that was pending.
                if (!wasPending)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanAccept modified a "
                                       "Loan that was not pending";
                    return false;
                }

                if (isPending)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanAccept leaves a "
                                       "Loan pending";
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
            else
            {
                if (wasPending != isPending)
                {
                    JLOG(j.fatal()) << "Invariant failed: Loan Pending flag changed "
                                       "by an unauthorized transaction";
                    return false;
                }

                // A pending loan may only be modified by LoanAccept. Deleting it
                // through LoanDelete is covered by the deleted-loan checks.
                if (wasPending)
                {
                    JLOG(j.fatal()) << "Invariant failed: pending Loan modified by a "
                                       "transaction other than LoanAccept";
                    return false;
                }
                if (!beforeHasNode && afterHasNode)
                {
                    JLOG(j.fatal()) << "Invariant failed: Loan OwnerNode added "
                                       "by an unauthorized transaction";
                    return false;
                }
            }
        }
        // With or without featureLendingProtocolV1_2, a Loan created by LoanSet
        // must be consistent with the transaction that created it. These checks
        // do not re-derive which flow LoanSet chose; they only catch outcomes
        // that must never happen whatever the flow. An active loan does not
        // need a CounterpartySignature: the counterparty may instead sign an
        // outer Batch or a Cosign proposal, neither of which is visible here.
        if (isTesSuccess(result) && !before && txType == ttLOAN_SET)
        {
            bool const isPending = after->isFlag(lsfLoanPending);
            bool const hasBorrower = tx.isFieldPresent(sfBorrower);
            bool const hasStartDate = tx.isFieldPresent(sfStartDate);

            // The Loan must record the StartDate and Borrower the transaction
            // named.
            if (hasStartDate && after->getFieldU32(sfStartDate) != tx.getFieldU32(sfStartDate))
            {
                JLOG(j.fatal()) << "Invariant failed: LoanSet did not record the "
                                   "transaction StartDate";
                return false;
            }
            if (hasBorrower && after->getAccountID(sfBorrower) != tx.getAccountID(sfBorrower))
            {
                JLOG(j.fatal()) << "Invariant failed: LoanSet did not record the "
                                   "transaction Borrower";
                return false;
            }

            if (isPending)
            {
                // The two-step flow does not exist before
                // featureLendingProtocolV1_2, so LoanSet must not create a
                // pending loan without it.
                if (!lpV12Enabled)
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet created a pending "
                                       "Loan without featureLendingProtocolV1_2";
                    return false;
                }

                // A pending loan is accepted in a later ledger, so its StartDate
                // must be strictly in the future at creation to remain in the
                // future when LoanAccept finalizes it.
                if (after->getFieldU32(sfStartDate) <=
                    view.parentCloseTime().time_since_epoch().count())
                {
                    JLOG(j.fatal()) << "Invariant failed: LoanSet created a pending "
                                       "Loan whose StartDate is not in the future";
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
            //
            // PrincipalOutstanding may stay put on a non-final pay: at integer
            // scale, fixCleanup3_2_0 rounds principal up so a fractional
            // amortization step does not reduce it. Interest (TVO) still falls.
            // Neither balance may grow: a payment never adds to what is owed,
            // since late-payment penalties are charged in the same transaction
            // rather than tracked in TotalValueOutstanding.
            if (isTesSuccess(result) && txType == ttLOAN_PAY)
            {
                if (before && after->at(sfPaymentRemaining) != 0)
                {
                    if (after->at(sfPrincipalOutstanding) > before->at(sfPrincipalOutstanding))
                    {
                        JLOG(j.fatal()) << "Invariant failed: loan pay must not increase "
                                           "PrincipalOutstanding on a non-full-repayment";
                        return false;
                    }
                    if (after->at(sfTotalValueOutstanding) > before->at(sfTotalValueOutstanding))
                    {
                        JLOG(j.fatal()) << "Invariant failed: loan pay must not increase "
                                           "TotalValueOutstanding on a non-full-repayment";
                        return false;
                    }
                    if (after->at(sfPrincipalOutstanding) == before->at(sfPrincipalOutstanding) &&
                        after->at(sfTotalValueOutstanding) == before->at(sfTotalValueOutstanding))
                    {
                        JLOG(j.fatal()) << "Invariant failed: loan pay must decrease "
                                           "PrincipalOutstanding or TotalValueOutstanding "
                                           "on a non-full-repayment";
                        return false;
                    }
                    if (after->at(sfPaymentRemaining) >= before->at(sfPaymentRemaining))
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

        // A pending loan must not be linked into the borrower's directory, and
        // a non-pending loan must be linked.
        if (lpV12Enabled)
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

            // Borrower and StartDate are required on the entry, so a loan must
            // carry real values in them rather than their defaults.
            if (after->getAccountID(sfBorrower) == beast::kZero ||
                after->getFieldU32(sfStartDate) == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: Loan has no Borrower or "
                                   "StartDate";
                return false;
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
