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
lendingProtocolEnabled(PreflightContext const& ctx);

namespace detail {
// These functions should rarely be used directly. More often, the ultimate
// result needs to be roundToAsset'd.

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

Number
loanPeriodicPayment(
    Number principalOutstanding,
    Number periodicRate,
    std::uint32_t paymentsRemaining);

Number
loanPeriodicPayment(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

template <AssetType A>
Number
loanTotalValueOutstanding(
    A asset,
    Number const& originalPrincipal,
    Number const& periodicPayment,
    std::uint32_t paymentsRemaining)
{
    return roundToAsset(
        asset,
        /*
         * This formula is from the XLS-66 spec, section 3.2.4.2 (Total Loan
         * Value Calculation), specifically "totalValueOutstanding = ..."
         */
        periodicPayment * paymentsRemaining,
        originalPrincipal,
        Number::upward);
}

template <AssetType A>
Number
loanTotalValueOutstanding(
    A asset,
    Number const& originalPrincipal,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    /*
     * This function is derived from the XLS-66 spec, section 3.2.4.2 (Total
     * Loan Value Calculation)
     */
    return loanTotalValueOutstanding(
        asset,
        originalPrincipal,
        loanPeriodicPayment(
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining),
        paymentsRemaining);
}

inline Number
loanTotalInterestOutstanding(
    Number principalOutstanding,
    Number totalValueOutstanding)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.2 (Total Loan
     * Value Calculation), specifically "totalInterestOutstanding = ..."
     */
    return totalValueOutstanding - principalOutstanding;
}

template <AssetType A>
Number
loanTotalInterestOutstanding(
    A asset,
    Number const& originalPrincipal,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    /*
     * This formula is derived from the XLS-66 spec, section 3.2.4.2 (Total Loan
     * Value Calculation)
     */
    return loanTotalInterestOutstanding(
        principalOutstanding,
        loanTotalValueOutstanding(
            asset,
            originalPrincipal,
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining));
}

Number
loanLatePaymentInterest(
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate);

Number
loanAccruedInterest(
    Number principalOutstanding,
    Number periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval);

inline Number
minusManagementFee(Number value, TenthBips32 managementFeeRate)
{
    return tenthBipsOfValue(value, tenthBipsPerUnity - managementFeeRate);
}

}  // namespace detail

template <AssetType A>
Number
valueMinusManagementFee(
    A const& asset,
    Number const& value,
    TenthBips32 managementFeeRate,
    Number const& originalPrincipal)
{
    return roundToAsset(
        asset,
        detail::minusManagementFee(value, managementFeeRate),
        originalPrincipal);
}

template <AssetType A>
Number
loanInterestOutstandingMinusFee(
    A const& asset,
    Number const& originalPrincipal,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate)
{
    return valueMinusManagementFee(
        asset,
        detail::loanTotalInterestOutstanding(
            asset,
            originalPrincipal,
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining),
        managementFeeRate,
        originalPrincipal);
}

template <AssetType A>
Number
loanPeriodicPayment(
    A const& asset,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    Number const& originalPrincipal)
{
    return roundToAsset(
        asset,
        detail::loanPeriodicPayment(
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining),
        originalPrincipal);
}

template <AssetType A>
Number
loanLatePaymentInterest(
    A const& asset,
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    Number const& originalPrincipal)
{
    return roundToAsset(
        asset,
        detail::loanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            parentCloseTime,
            startDate,
            prevPaymentDate),
        originalPrincipal);
}

struct PeriodicPaymentParts
{
    Number interest;
    Number principal;
};

template <AssetType A>
PeriodicPaymentParts
computePeriodicPaymentParts(
    A const& asset,
    Number const& originalPrincipal,
    Number const& principalOutstanding,
    Number const& periodicPaymentAmount,
    Number const& periodicRate,
    std::uint32_t paymentRemaining)
{
    /*
     * This function is derived from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment)
     */
    if (paymentRemaining == 1)
    {
        // If there's only one payment left, we need to pay off the principal.
        Number const interest = roundToAsset(
            asset,
            periodicPaymentAmount - principalOutstanding,
            originalPrincipal);
        return {interest, principalOutstanding};
    }
    Number const interest = roundToAsset(
        asset, principalOutstanding * periodicRate, originalPrincipal);
    XRPL_ASSERT(
        interest >= 0,
        "ripple::detail::computePeriodicPayment : valid interest");

    auto const roundedPayment = [&]() {
        auto roundedPayment =
            roundToAsset(asset, periodicPaymentAmount, originalPrincipal);
        if (roundedPayment > interest)
            return roundedPayment;
        auto newPayment = roundedPayment;
        if (asset.native() || !asset.template holds<Issue>())
        {
            // integral types, just add one
            ++newPayment;
        }
        else
        {
            // Non-integral types: IOU. Add "dust" that will not be lost in
            // rounding.
            auto const epsilon = Number{1, originalPrincipal.exponent() - 14};
            newPayment += epsilon;
        }
        roundedPayment = roundToAsset(asset, newPayment, originalPrincipal);
        XRPL_ASSERT_PARTS(
            roundedPayment == newPayment,
            "ripple::computePeriodicPaymentParts",
            "epsilon preserved in rounding");
        return roundedPayment;
    }();
    Number const principal =
        roundToAsset(asset, roundedPayment - interest, originalPrincipal);
    XRPL_ASSERT_PARTS(
        principal > 0 && principal <= principalOutstanding,
        "ripple::computePeriodicPaymentParts",
        "valid principal");

    return {interest, principal};
}

struct LoanPaymentParts
{
    Number principalPaid;
    Number interestPaid;
    Number valueChange;
    Number feePaid;
};

struct LatePaymentParams
{
};

/* Handle possible late payments.
 *
 * If this function processed a late payment, the return value will be
 * a LoanPaymentParts object. If the loan is not late, the return will be an
 * Unexpected(tesSUCCESS). Otherwise, it'll be an Unexpected with the error code
 * the caller is expected to return.
 */
template <AssetType A, class NumberProxy, class Int32Proxy>
Expected<LoanPaymentParts, TER>
handleLatePayment(
    A const& asset,
    ApplyView& view,
    NumberProxy& principalOutstandingField,
    Int32Proxy& paymentRemainingField,
    Int32Proxy& prevPaymentDateField,
    Int32Proxy& nextDueDateField,
    PeriodicPaymentParts const& periodic,
    std::uint32_t const startDate,
    std::uint32_t const paymentInterval,
    TenthBips32 const lateInterestRate,
    Number const& originalPrincipalRequested,
    Number const& periodicPaymentAmount,
    Number const& latePaymentFee,
    STAmount const& amount,
    beast::Journal j)
{
    if (!hasExpired(view, nextDueDateField))
        return Unexpected(tesSUCCESS);

    // the payment is late
    auto const latePaymentInterest = loanLatePaymentInterest(
        asset,
        principalOutstandingField,
        lateInterestRate,
        view.parentCloseTime(),
        startDate,
        prevPaymentDateField,
        originalPrincipalRequested);
    XRPL_ASSERT(
        latePaymentInterest >= 0,
        "ripple::loanComputePaymentParts : valid late interest");
    auto const latePaymentAmount =
        periodicPaymentAmount + latePaymentInterest + latePaymentFee;

    if (amount < latePaymentAmount)
    {
        JLOG(j.warn()) << "Late loan payment amount is insufficient. Due: "
                       << latePaymentAmount << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    paymentRemainingField -= 1;
    // A single payment always pays the same amount of principal. Only the
    // interest and fees are extra for a late payment
    principalOutstandingField -= periodic.principal;

    // Make sure this does an assignment
    prevPaymentDateField = nextDueDateField;
    nextDueDateField += paymentInterval;

    // A late payment increases the value of the loan by the difference
    // between periodic and late payment interest
    return LoanPaymentParts{
        periodic.principal,
        latePaymentInterest + periodic.interest,
        latePaymentInterest,
        latePaymentFee};
}

/* Handle possible full payments.
 *
 * If this function processed a full payment, the return value will be
 * a LoanPaymentParts object. If the payment should not be considered as a full
 * payment, the return will be an Unexpected(tesSUCCESS). Otherwise, it'll be an
 * Unexpected with the error code the caller is expected to return.
 */
template <AssetType A, class NumberProxy, class Int32Proxy>
Expected<LoanPaymentParts, TER>
handleFullPayment(
    A const& asset,
    ApplyView& view,
    NumberProxy& principalOutstandingField,
    Int32Proxy& paymentRemainingField,
    Int32Proxy& prevPaymentDateField,
    std::uint32_t const startDate,
    std::uint32_t const paymentInterval,
    TenthBips32 const closeInterestRate,
    Number const& originalPrincipalRequested,
    Number const& totalInterestOutstanding,
    Number const& periodicRate,
    Number const& closePaymentFee,
    STAmount const& amount,
    beast::Journal j)
{
    if (paymentRemainingField <= 1)
        // If this is the last payment, it has to be a regular payment
        return Unexpected(tesSUCCESS);

    // If there is more than one payment remaining, see if enough was
    // paid for a full payment
    auto const accruedInterest = roundToAsset(
        asset,
        detail::loanAccruedInterest(
            principalOutstandingField,
            periodicRate,
            view.parentCloseTime(),
            startDate,
            prevPaymentDateField,
            paymentInterval),
        originalPrincipalRequested);
    XRPL_ASSERT(
        accruedInterest >= 0,
        "ripple::loanComputePaymentParts : valid accrued interest");
    auto const closePrepaymentInterest = roundToAsset(
        asset,
        tenthBipsOfValue(principalOutstandingField.value(), closeInterestRate),
        originalPrincipalRequested);
    XRPL_ASSERT(
        closePrepaymentInterest >= 0,
        "ripple::loanComputePaymentParts : valid prepayment "
        "interest");
    auto const totalInterest = accruedInterest + closePrepaymentInterest;
    auto const closeFullPayment =
        principalOutstandingField + totalInterest + closePaymentFee;

    if (amount < closeFullPayment)
        // If the payment is less than the full payment amount, it's not
        // sufficient to be a full payment.
        return Unexpected(tesSUCCESS);

    // Make a full payment

    // A full payment decreases the value of the loan by the
    // difference between the interest paid and the expected
    // outstanding interest return
    auto const valueChange = totalInterest - totalInterestOutstanding;

    LoanPaymentParts const result{
        principalOutstandingField, totalInterest, valueChange, closePaymentFee};

    paymentRemainingField = 0;
    principalOutstandingField = 0;

    return result;
}

template <AssetType A>
Expected<LoanPaymentParts, TER>
loanComputePaymentParts(
    A const& asset,
    ApplyView& view,
    SLE::ref loan,
    STAmount const& amount,
    beast::Journal j)
{
    /*
     * This function is an implementation of the XLS-66 spec,
     * section 3.2.4.3 (Transaction Pseudo-code)
     */
    Number const originalPrincipalRequested = loan->at(sfPrincipalRequested);
    auto principalOutstandingField = loan->at(sfPrincipalOutstanding);
    bool const allowOverpayment = loan->isFlag(lsfLoanOverpayment);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};
    TenthBips32 const overpaymentInterestRate{
        loan->at(sfOverpaymentInterestRate)};

    Number const serviceFee = loan->at(sfLoanServiceFee);
    Number const latePaymentFee = loan->at(sfLatePaymentFee);
    Number const closePaymentFee = roundToAsset(
        asset, loan->at(sfClosePaymentFee), originalPrincipalRequested);
    TenthBips32 const overpaymentFee{loan->at(sfOverpaymentFee)};

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);
    auto paymentRemainingField = loan->at(sfPaymentRemaining);

    auto prevPaymentDateField = loan->at(sfPreviousPaymentDate);
    std::uint32_t const startDate = loan->at(sfStartDate);
    auto nextDueDateField = loan->at(sfNextPaymentDueDate);

    if (paymentRemainingField == 0 || principalOutstandingField == 0)
    {
        // Loan complete
        JLOG(j.warn()) << "Loan is already paid off.";
        return Unexpected(tecKILLED);
    }

    // Compute the normal periodic rate, payment, etc.
    // We'll need it in the remaining calculations
    Number const periodicRate =
        detail::loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(
        interestRate == 0 || periodicRate > 0,
        "ripple::loanComputePaymentParts : valid rate");

    // Don't round the payment amount. Only round the final computations
    // using it.
    Number const periodicPaymentAmount = detail::loanPeriodicPayment(
        principalOutstandingField, periodicRate, paymentRemainingField);
    XRPL_ASSERT(
        periodicPaymentAmount > 0,
        "ripple::computePeriodicPayment : valid payment");

    auto const periodic = computePeriodicPaymentParts(
        asset,
        originalPrincipalRequested,
        principalOutstandingField,
        periodicPaymentAmount,
        periodicRate,
        paymentRemainingField);

    Number const totalValueOutstanding = detail::loanTotalValueOutstanding(
        asset,
        originalPrincipalRequested,
        periodicPaymentAmount,
        paymentRemainingField);
    XRPL_ASSERT(
        totalValueOutstanding > 0,
        "ripple::loanComputePaymentParts : valid total value");
    Number const totalInterestOutstanding =
        detail::loanTotalInterestOutstanding(
            principalOutstandingField, totalValueOutstanding);
    XRPL_ASSERT_PARTS(
        totalInterestOutstanding >= 0,
        "ripple::loanComputePaymentParts",
        "valid total interest");
    XRPL_ASSERT_PARTS(
        totalValueOutstanding - totalInterestOutstanding ==
            principalOutstandingField,
        "ripple::loanComputePaymentParts",
        "valid principal computation");

    view.update(loan);

    // -------------------------------------------------------------
    // late payment handling
    if (auto const latePaymentParts = handleLatePayment(
            asset,
            view,
            principalOutstandingField,
            paymentRemainingField,
            prevPaymentDateField,
            nextDueDateField,
            periodic,
            startDate,
            paymentInterval,
            lateInterestRate,
            originalPrincipalRequested,
            periodicPaymentAmount,
            latePaymentFee,
            amount,
            j))
        return *latePaymentParts;
    else if (latePaymentParts.error())
        return latePaymentParts;

    // -------------------------------------------------------------
    // full payment handling
    if (auto const fullPaymentParts = handleFullPayment(
            asset,
            view,
            principalOutstandingField,
            paymentRemainingField,
            prevPaymentDateField,
            startDate,
            paymentInterval,
            closeInterestRate,
            originalPrincipalRequested,
            totalInterestOutstanding,
            periodicRate,
            closePaymentFee,
            amount,
            j))
        return *fullPaymentParts;
    else if (fullPaymentParts.error())
        return fullPaymentParts;

    // -------------------------------------------------------------
    // regular periodic payment handling

    // if the payment is not late nor if it's a full payment, then it must
    // be a periodic one, with possible overpayments

    auto const totalDue = roundToAsset(
        asset,
        periodicPaymentAmount + serviceFee,
        originalPrincipalRequested,
        Number::upward);

    std::optional<NumberRoundModeGuard> mg(Number::downward);
    std::int64_t const fullPeriodicPayments = [&]() {
        std::int64_t const full{amount / totalDue};
        return full < paymentRemainingField ? full : paymentRemainingField;
    }();
    mg.reset();
    // Temporary asserts
    XRPL_ASSERT(
        amount >= totalDue || fullPeriodicPayments == 0,
        "temp full periodic rounding");
    XRPL_ASSERT(
        amount < totalDue || fullPeriodicPayments >= 1,
        "temp full periodic rounding");

    if (fullPeriodicPayments < 1)
    {
        JLOG(j.warn()) << "Periodic loan payment amount is insufficient. Due: "
                       << periodicPaymentAmount << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    nextDueDateField += paymentInterval * fullPeriodicPayments;
    prevPaymentDateField = nextDueDateField - paymentInterval;

    Number totalPrincipalPaid = 0;
    Number totalInterestPaid = 0;
    Number loanValueChange = 0;

    std::optional<PeriodicPaymentParts> future = periodic;
    for (int i = 0; i < fullPeriodicPayments; ++i)
    {
        // Only do the work if we need to
        if (!future)
            future = computePeriodicPaymentParts(
                asset,
                originalPrincipalRequested,
                principalOutstandingField,
                periodicPaymentAmount,
                periodicRate,
                paymentRemainingField);
        XRPL_ASSERT(
            future->interest <= periodic.interest,
            "ripple::loanComputePaymentParts : decreasing interest");
        XRPL_ASSERT(
            future->principal >= periodic.principal,
            "ripple::loanComputePaymentParts : increasing principal");

        totalPrincipalPaid += future->principal;
        totalInterestPaid += future->interest;
        paymentRemainingField -= 1;
        principalOutstandingField -= future->principal;

        future.reset();
    }

    Number totalFeePaid = serviceFee * fullPeriodicPayments;

    Number const newInterest = detail::loanTotalInterestOutstanding(
                                   asset,
                                   originalPrincipalRequested,
                                   principalOutstandingField,
                                   interestRate,
                                   paymentInterval,
                                   paymentRemainingField) +
        totalInterestPaid;

    // -------------------------------------------------------------
    // overpayment handling
    Number overpaymentInterestPortion = 0;
    if (allowOverpayment)
    {
        Number const overpayment = std::min(
            principalOutstandingField.value(),
            amount - (totalPrincipalPaid + totalInterestPaid + totalFeePaid));

        if (roundToAsset(asset, overpayment, originalPrincipalRequested) > 0)
        {
            Number const interestPortion = roundToAsset(
                asset,
                tenthBipsOfValue(overpayment, overpaymentInterestRate),
                originalPrincipalRequested);
            Number const feePortion = roundToAsset(
                asset,
                tenthBipsOfValue(overpayment, overpaymentFee),
                originalPrincipalRequested);
            Number const remainder = roundToAsset(
                asset,
                overpayment - interestPortion - feePortion,
                originalPrincipalRequested);

            // Don't process an overpayment if the whole amount (or more!)
            // gets eaten by fees
            if (remainder > 0)
            {
                overpaymentInterestPortion = interestPortion;
                totalPrincipalPaid += remainder;
                totalInterestPaid += interestPortion;
                totalFeePaid += feePortion;

                principalOutstandingField -= remainder;
            }
        }
    }

    loanValueChange =
        (newInterest - totalInterestOutstanding) + overpaymentInterestPortion;

    // Check the final results are rounded, to double-check that the
    // intermediate steps were rounded.
    XRPL_ASSERT(
        roundToAsset(asset, totalPrincipalPaid, originalPrincipalRequested) ==
            totalPrincipalPaid,
        "ripple::loanComputePaymentParts : totalPrincipalPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalInterestPaid, originalPrincipalRequested) ==
            totalInterestPaid,
        "ripple::loanComputePaymentParts : totalInterestPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, loanValueChange, originalPrincipalRequested) ==
            loanValueChange,
        "ripple::loanComputePaymentParts : loanValueChange rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalFeePaid, originalPrincipalRequested) ==
            totalFeePaid,
        "ripple::loanComputePaymentParts : totalFeePaid rounded");
    return LoanPaymentParts{
        totalPrincipalPaid, totalInterestPaid, loanValueChange, totalFeePaid};
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
