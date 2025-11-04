//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
#define RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/st.h>

namespace ripple {

struct PreflightContext;

// Lending protocol has dependencies, so capture them here.
bool
checkLendingProtocolDependencies(PreflightContext const& ctx);

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

/// Ensure the periodic payment is always rounded consistently
inline Number
roundPeriodicPayment(
    Asset const& asset,
    Number const& periodicPayment,
    std::int32_t scale)
{
    return roundToAsset(asset, periodicPayment, scale, Number::upward);
}

/// This structure is explained in the XLS-66 spec, section 3.2.4.4 (Failure
/// Conditions)
struct LoanPaymentParts
{
    /// principal_paid is the amount of principal that the payment covered.
    Number principalPaid;
    /// interest_paid is the amount of interest that the payment covered.
    Number interestPaid;
    /**
     * value_change is the amount by which the total value of the Loan changed.
     *  If value_change < 0, Loan value decreased.
     *  If value_change > 0, Loan value increased.
     * This is 0 for regular payments.
     */
    Number valueChange;
    /// feePaid is amount of fee that is paid to the broker
    Number feePaid;

    LoanPaymentParts&
    operator+=(LoanPaymentParts const& other);
};

/** This structure describes the initial "computed" properties of a loan.
 *
 * It is used at loan creation and when the terms of a loan change, such as
 * after an overpayment.
 */
struct LoanProperties
{
    Number periodicPayment;
    Number totalValueOutstanding;
    Number managementFeeOwedToBroker;
    std::int32_t loanScale;
    Number firstPaymentPrincipal;
};

/** This structure captures the current state of a loan and all the
   relevant parts.

   Whether the values are raw (unrounded) or rounded will
   depend on how it was computed.

   Many of the fields can be derived from each other, but they're all provided
   here to reduce code duplication and possible mistakes.
   e.g.
     * interestOutstanding = valueOutstanding - principalOutstanding
     * interestDue = interestOutstanding - managementFeeDue
 */
struct LoanState
{
    /// Total value still due to be paid by the borrower.
    Number valueOutstanding;
    /// Prinicipal still due to be paid by the borrower.
    Number principalOutstanding;
    /// Interest still due to be paid TO the Vault.
    // This is a portion of interestOutstanding
    Number interestDue;
    /// Management fee still due to be paid TO the broker.
    // This is a portion of interestOutstanding
    Number managementFeeDue;

    /// Interest still due to be paid by the borrower.
    Number
    interestOutstanding() const
    {
        XRPL_ASSERT_PARTS(
            interestDue + managementFeeDue ==
                valueOutstanding - principalOutstanding,
            "ripple::LoanState::interestOutstanding",
            "other values add up correctly");
        return interestDue + managementFeeDue;
    }
};

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

LoanState
calculateRoundedLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding);

LoanState
calculateRoundedLoanState(SLE::const_ref loan);

Number
computeFee(
    Asset const& asset,
    Number const& value,
    TenthBips32 managementFeeRate,
    std::int32_t scale);

Number
calculateFullPaymentInterest(
    Number const& rawPrincipalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate);

Number
calculateFullPaymentInterest(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate);

namespace detail {
// These classes and functions should only be accessed by LendingHelper
// functions and unit tests

enum class PaymentSpecialCase { none, final, extra };

/// This structure is used internally to compute the breakdown of a
/// single loan payment
struct PaymentComponents
{
#if LOANCOMPLETE
    // raw values are unrounded, and are based on pure math
    Number rawInterest;
    Number rawPrincipal;
    Number rawManagementFee;
#endif
    // tracked values are rounded to the asset and loan scale, and correspond to
    // fields in the Loan ledger object.
    // trackedValueDelta modifies sfTotalValueOutstanding.
    Number trackedValueDelta;
    // trackedPrincipalDelta modifies sfPrincipalOutstanding.
    Number trackedPrincipalDelta;
    // trackedManagementFeeDelta modifies sfManagementFeeOutstanding. It will
    // not include any "extra" fees that go directly to the broker, such as late
    // fees.
    Number trackedManagementFeeDelta;

    PaymentSpecialCase specialCase = PaymentSpecialCase::none;

    Number
    trackedInterestPart() const;
};

struct LoanDeltas
{
    Number principalDelta;
    Number interestDueDelta;
    Number managementFeeDueDelta;

    Number
    valueDelta() const
    {
        return principalDelta + interestDueDelta + managementFeeDueDelta;
    }

    void
    nonNegative();
};

PaymentComponents
computePaymentComponents(
    Asset const& asset,
    std::int32_t scale,
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    TenthBips16 managementFeeRate);

}  // namespace detail

detail::LoanDeltas
operator-(LoanState const& lhs, LoanState const& rhs);

LoanState
operator-(LoanState const& lhs, detail::LoanDeltas const& rhs);

Number
valueMinusFee(
    Asset const& asset,
    Number const& value,
    TenthBips16 managementFeeRate,
    std::int32_t scale);

LoanProperties
computeLoanProperties(
    Asset const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate);

bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale);

Expected<LoanPaymentParts, TER>
loanMakeFullPayment(
    Asset const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    bool const overpaymentAllowed,
    beast::Journal j);

Expected<LoanPaymentParts, TER>
loanMakePayment(
    Asset const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    bool const overpaymentAllowed,
    beast::Journal j);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
