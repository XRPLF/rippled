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

namespace detail {
// These functions should rarely be used directly. More often, the ultimate
// result needs to be roundToAsset'd.

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

Number
loanPeriodicPayment(
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining);

Number
loanPeriodicPayment(
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate);

Number
loanAccruedInterest(
    Number const& principalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval);

inline Number
minusManagementFee(Number const& value, TenthBips32 managementFeeRate)
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
    std::int32_t scale)
{
    return roundToAsset(
        asset, detail::minusManagementFee(value, managementFeeRate), scale);
}

inline Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    return detail::loanPeriodicRate(interestRate, paymentInterval);
}

template <AssetType A>
Number
loanPeriodicPayment(
    A const& asset,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining,
    std::int32_t scale)
{
    return roundToAsset(
        asset,
        detail::loanPeriodicPayment(
            principalOutstanding, periodicRate, paymentsRemaining),
        scale,
        Number::upward);
}

template <AssetType A>
Number
loanPeriodicPayment(
    A const& asset,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    std::int32_t scale)
{
    return loanPeriodicPayment(
        asset,
        principalOutstanding,
        loanPeriodicRate(interestRate, paymentInterval),
        paymentsRemaining,
        scale);
}

template <AssetType A>
Number
loanTotalValueOutstanding(
    A asset,
    std::int32_t scale,
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
        scale,
        Number::upward);
}

template <AssetType A>
Number
loanTotalValueOutstanding(
    A asset,
    std::int32_t scale,
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
        scale,
        loanPeriodicPayment(
            asset,
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining,
            scale),
        paymentsRemaining);
}

inline Number
loanTotalInterestOutstanding(
    Number const& principalOutstanding,
    Number const& totalValueOutstanding)
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
    std::int32_t scale,
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
            scale,
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining));
}

template <AssetType A>
Number
loanInterestOutstandingMinusFee(
    A const& asset,
    Number const& totalInterestOutstanding,
    TenthBips32 managementFeeRate,
    std::int32_t scale)
{
    return valueMinusManagementFee(
        asset, totalInterestOutstanding, managementFeeRate, scale);
}

template <AssetType A>
Number
loanInterestOutstandingMinusFee(
    A const& asset,
    std::int32_t scale,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate)
{
    return loanInterestOutstandingMinusFee(
        asset,
        loanTotalInterestOutstanding(
            asset,
            scale,
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining),
        managementFeeRate,
        scale);
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
    Number const& scale)
{
    return roundToAsset(
        asset,
        detail::loanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            parentCloseTime,
            startDate,
            prevPaymentDate),
        scale);
}

template <AssetType A>
bool
isRounded(A const& asset, Number const& value, std::int32_t scale)
{
    return roundToAsset(asset, value, scale, Number::downward) == value &&
        roundToAsset(asset, value, scale, Number::upward) == value;
}

struct PaymentParts
{
    Number interest;
    Number principal;
    Number fee;
};

template <AssetType A>
PaymentParts
computePaymentParts(
    A const& asset,
    std::int32_t scale,
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& periodicPaymentAmount,
    Number const& serviceFee,
    Number const& periodicRate,
    std::uint32_t paymentRemaining)
{
    /*
     * This function is derived from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment)
     */
    XRPL_ASSERT_PARTS(
        isRounded(asset, totalValueOutstanding, scale) &&
            isRounded(asset, principalOutstanding, scale) &&
            isRounded(asset, periodicPaymentAmount, scale),
        "ripple::computePaymentParts",
        "Asset values are rounded");
    Number const roundedFee = roundToAsset(asset, serviceFee, scale);
    if (paymentRemaining == 1 || periodicPaymentAmount > totalValueOutstanding)
    {
        // If there's only one payment left, we need to pay off the principal.
        Number const interest = totalValueOutstanding - principalOutstanding;
        return {
            .interest = interest,
            .principal = principalOutstanding,
            .fee = roundedFee};
    }
    /*
     * From the spec, once the periodicPayment is computed:
     *
     * The principal and interest portions can be derived as follows:
     *  interest = principalOutstanding * periodicRate
     *  principal = periodicPayment - interest
     *
     * Because those values deal with funds, they need to be rounded.
     */
    Number const interest = roundToAsset(
        asset, principalOutstanding * periodicRate, scale, Number::upward);
    XRPL_ASSERT(
        interest >= 0,
        "ripple::detail::computePeriodicPayment : valid interest");

    // To compute the principal using the above formulas, use the rounded
    // payment amount, ensuring that some principal is paid regardless of any
    // other results.
    auto const roundedPayment = [&]() {
        auto roundedPayment =
            roundToAsset(asset, periodicPaymentAmount, scale, Number::upward);
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
            auto const epsilon = Number{1, scale - 14};
            newPayment += epsilon;
        }
        roundedPayment = roundToAsset(asset, newPayment, scale);
        XRPL_ASSERT_PARTS(
            roundedPayment == newPayment,
            "ripple::computePaymentParts",
            "epsilon preserved in rounding");
        return roundedPayment;
    }();
    Number const principal =
        roundToAsset(asset, roundedPayment - interest, scale);
    XRPL_ASSERT_PARTS(
        principal > 0 && principal <= principalOutstanding,
        "ripple::computePaymentParts",
        "valid principal");

    return {.interest = interest, .principal = principal, .fee = roundedFee};
}

// This structure is explained in the XLS-66 spec, section 3.2.4.4 (Failure
// Conditions)
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
    /// fee_paid is the amount of fee that the payment covered.
    Number feePaid;
};

/* Handle possible late payments.
 *
 * If this function processed a late payment, the return value will be
 * a LoanPaymentParts object. If the loan is not late, the return will be an
 * Unexpected(tesSUCCESS). Otherwise, it'll be an Unexpected with the error code
 * the caller is expected to return.
 *
 *
 * This function is an implementation of the XLS-66 spec, based on
 * * section 3.2.4.3 (Transaction Pseudo-code), specifically the bit
 *   labeled "the payment is late"
 * * section 3.2.4.1.2 (Late Payment)
 */
template <AssetType A, class NumberProxy, class Int32Proxy>
Expected<LoanPaymentParts, TER>
handleLatePayment(
    A const& asset,
    ApplyView& view,
    NumberProxy& principalOutstandingProxy,
    Int32Proxy& paymentRemainingProxy,
    Int32Proxy& prevPaymentDateProxy,
    Int32Proxy& nextDueDateProxy,
    PaymentParts const& periodic,
    std::uint32_t const startDate,
    std::uint32_t const paymentInterval,
    TenthBips32 const lateInterestRate,
    std::int32_t loanScale,
    Number const& latePaymentFee,
    STAmount const& amount,
    beast::Journal j)
{
    if (!hasExpired(view, nextDueDateProxy))
        return Unexpected(tesSUCCESS);

    // the payment is late
    // Late payment interest is only the part of the interest that comes from
    // being late, as computed by 3.2.4.1.2.
    auto const latePaymentInterest = loanLatePaymentInterest(
        asset,
        principalOutstandingProxy,
        lateInterestRate,
        view.parentCloseTime(),
        startDate,
        prevPaymentDateProxy,
        loanScale);
    XRPL_ASSERT(
        latePaymentInterest >= 0,
        "ripple::handleLatePayment : valid late interest");
    PaymentParts const late{
        .interest = latePaymentInterest + periodic.interest,
        .principal = periodic.principal,
        .fee = latePaymentFee + periodic.fee};
    auto const totalDue = late.principal + late.interest + late.fee;

    if (amount < totalDue)
    {
        JLOG(j.warn()) << "Late loan payment amount is insufficient. Due: "
                       << totalDue << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    paymentRemainingProxy -= 1;
    // A single payment always pays the same amount of principal. Only the
    // interest and fees are extra for a late payment
    principalOutstandingProxy -= late.principal;

    // Make sure this does an assignment
    prevPaymentDateProxy = nextDueDateProxy;
    nextDueDateProxy += paymentInterval;

    // A late payment increases the value of the loan by the difference
    // between periodic and late payment interest
    return LoanPaymentParts{
        .principalPaid = late.principal,
        .interestPaid = late.interest,
        .valueChange = latePaymentInterest,
        .feePaid = late.fee};
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
    NumberProxy& principalOutstandingProxy,
    Int32Proxy& paymentRemainingProxy,
    Int32Proxy& prevPaymentDateProxy,
    std::uint32_t const startDate,
    std::uint32_t const paymentInterval,
    TenthBips32 const closeInterestRate,
    std::int32_t loanScale,
    Number const& totalInterestOutstanding,
    Number const& periodicRate,
    Number const& closePaymentFee,
    STAmount const& amount,
    beast::Journal j)
{
    if (paymentRemainingProxy <= 1)
        // If this is the last payment, it has to be a regular payment
        return Unexpected(tesSUCCESS);

    // If there is more than one payment remaining, see if enough was
    // paid for a full payment
    auto const accruedInterest = roundToAsset(
        asset,
        detail::loanAccruedInterest(
            principalOutstandingProxy,
            periodicRate,
            view.parentCloseTime(),
            startDate,
            prevPaymentDateProxy,
            paymentInterval),
        loanScale);
    XRPL_ASSERT(
        accruedInterest >= 0,
        "ripple::handleFullPayment : valid accrued interest");
    auto const prepaymentPenalty = roundToAsset(
        asset,
        tenthBipsOfValue(principalOutstandingProxy.value(), closeInterestRate),
        loanScale);
    XRPL_ASSERT(
        prepaymentPenalty >= 0,
        "ripple::handleFullPayment : valid prepayment "
        "interest");
    auto const totalInterest = accruedInterest + prepaymentPenalty;
    auto const closeFullPayment =
        principalOutstandingProxy + totalInterest + closePaymentFee;

    if (amount < closeFullPayment)
        // If the payment is less than the full payment amount, it's not
        // sufficient to be a full payment, but that's not an error.
        return Unexpected(tesSUCCESS);

    // Make a full payment

    // A full payment decreases the value of the loan by the
    // difference between the interest paid and the expected
    // outstanding interest return
    auto const valueChange = totalInterest - totalInterestOutstanding;

    LoanPaymentParts const result{
        .principalPaid = principalOutstandingProxy,
        .interestPaid = totalInterest,
        .valueChange = valueChange,
        .feePaid = closePaymentFee};

    paymentRemainingProxy = 0;
    principalOutstandingProxy = 0;

    return result;
}

template <AssetType A>
Expected<LoanPaymentParts, TER>
loanMakePayment(
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
    std::int32_t const loanScale = loan->at(sfLoanScale);
    auto totalValueOutstandingProxy = loan->at(sfTotalValueOutstanding);
    auto principalOutstandingProxy = loan->at(sfPrincipalOutstanding);
    bool const allowOverpayment = loan->isFlag(lsfLoanOverpayment);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};
    TenthBips32 const overpaymentInterestRate{
        loan->at(sfOverpaymentInterestRate)};

    Number const serviceFee = loan->at(sfLoanServiceFee);
    Number const latePaymentFee = loan->at(sfLatePaymentFee);
    Number const closePaymentFee =
        roundToAsset(asset, loan->at(sfClosePaymentFee), loanScale);
    TenthBips32 const overpaymentFee{loan->at(sfOverpaymentFee)};

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);
    auto paymentRemainingProxy = loan->at(sfPaymentRemaining);

    auto prevPaymentDateProxy = loan->at(sfPreviousPaymentDate);
    std::uint32_t const startDate = loan->at(sfStartDate);
    auto nextDueDateProxy = loan->at(sfNextPaymentDueDate);

    if (paymentRemainingProxy == 0 || principalOutstandingProxy == 0)
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
        "ripple::loanMakePayment : valid rate");

    // Don't round the payment amount. Only round the final computations
    // using it.
    Number const periodicPaymentAmount = detail::loanPeriodicPayment(
        principalOutstandingProxy, periodicRate, paymentRemainingProxy);
    XRPL_ASSERT(
        periodicPaymentAmount > 0,
        "ripple::computePeriodicPayment : valid payment");

    auto const periodic = computePaymentParts(
        asset,
        loanScale,
        totalValueOutstandingProxy,
        principalOutstandingProxy,
        periodicPaymentAmount,
        serviceFee,
        periodicRate,
        paymentRemainingProxy);

    Number const totalValueOutstanding = loanTotalValueOutstanding(
        asset, loanScale, periodicPaymentAmount, paymentRemainingProxy);
    XRPL_ASSERT(
        totalValueOutstanding > 0,
        "ripple::loanMakePayment : valid total value");
    Number const totalInterestOutstanding = loanTotalInterestOutstanding(
        principalOutstandingProxy, totalValueOutstanding);
    XRPL_ASSERT_PARTS(
        totalInterestOutstanding >= 0,
        "ripple::loanMakePayment",
        "valid total interest");
    XRPL_ASSERT_PARTS(
        totalValueOutstanding - totalInterestOutstanding ==
            principalOutstandingProxy,
        "ripple::loanMakePayment",
        "valid principal computation");

    view.update(loan);

    // -------------------------------------------------------------
    // late payment handling
    if (auto const latePaymentParts = handleLatePayment(
            asset,
            view,
            principalOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            nextDueDateProxy,
            periodic,
            startDate,
            paymentInterval,
            lateInterestRate,
            loanScale,
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
            principalOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            startDate,
            paymentInterval,
            closeInterestRate,
            loanScale,
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

    auto const totalDue = periodic.interest + periodic.principal + periodic.fee;

    std::optional<NumberRoundModeGuard> mg(Number::downward);
    std::int64_t const fullPeriodicPayments = [&]() {
        std::int64_t const full{amount / totalDue};
        return full < paymentRemainingProxy ? full : paymentRemainingProxy;
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
                       << totalDue << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    nextDueDateProxy += paymentInterval * fullPeriodicPayments;
    prevPaymentDateProxy = nextDueDateProxy - paymentInterval;

    Number totalPrincipalPaid = 0;
    Number totalInterestPaid = 0;
    Number loanValueChange = 0;

    std::optional<PaymentParts> future = periodic;
    for (int i = 0; i < fullPeriodicPayments; ++i)
    {
        // Only do the work if we need to
        if (!future)
            future = computePaymentParts(
                asset,
                loanScale,
                totalValueOutstandingProxy,
                principalOutstandingProxy,
                periodicPaymentAmount,
                serviceFee,
                periodicRate,
                paymentRemainingProxy);
        XRPL_ASSERT(
            future->interest <= periodic.interest,
            "ripple::loanMakePayment : decreasing interest");
        XRPL_ASSERT(
            future->principal >= periodic.principal,
            "ripple::loanMakePayment : increasing principal");

        totalPrincipalPaid += future->principal;
        totalInterestPaid += future->interest;
        paymentRemainingProxy -= 1;
        principalOutstandingProxy -= future->principal;

        future.reset();
    }

    Number totalFeePaid = serviceFee * fullPeriodicPayments;

    Number const newInterest = loanTotalInterestOutstanding(
                                   asset,
                                   loanScale,
                                   principalOutstandingProxy,
                                   interestRate,
                                   paymentInterval,
                                   paymentRemainingProxy) +
        totalInterestPaid;

    // -------------------------------------------------------------
    // overpayment handling
    Number overpaymentInterestPortion = 0;
    if (allowOverpayment)
    {
        Number const overpayment = std::min(
            principalOutstandingProxy.value(),
            amount - (totalPrincipalPaid + totalInterestPaid + totalFeePaid));

        if (roundToAsset(asset, overpayment, loanScale) > 0)
        {
            Number const interestPortion = roundToAsset(
                asset,
                tenthBipsOfValue(overpayment, overpaymentInterestRate),
                loanScale);
            Number const feePortion = roundToAsset(
                asset,
                tenthBipsOfValue(overpayment, overpaymentFee),
                loanScale);
            Number const remainder = roundToAsset(
                asset, overpayment - interestPortion - feePortion, loanScale);

            // Don't process an overpayment if the whole amount (or more!)
            // gets eaten by fees
            if (remainder > 0)
            {
                overpaymentInterestPortion = interestPortion;
                totalPrincipalPaid += remainder;
                totalInterestPaid += interestPortion;
                totalFeePaid += feePortion;

                principalOutstandingProxy -= remainder;
            }
        }
    }

    loanValueChange =
        (newInterest - totalInterestOutstanding) + overpaymentInterestPortion;

    // Check the final results are rounded, to double-check that the
    // intermediate steps were rounded.
    XRPL_ASSERT(
        roundToAsset(asset, totalPrincipalPaid, loanScale) ==
            totalPrincipalPaid,
        "ripple::loanMakePayment : totalPrincipalPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalInterestPaid, loanScale) == totalInterestPaid,
        "ripple::loanMakePayment : totalInterestPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, loanValueChange, loanScale) == loanValueChange,
        "ripple::loanMakePayment : loanValueChange rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalFeePaid, loanScale) == totalFeePaid,
        "ripple::loanMakePayment : totalFeePaid rounded");
    return LoanPaymentParts{
        .principalPaid = totalPrincipalPaid,
        .interestPaid = totalInterestPaid,
        .valueChange = loanValueChange,
        .feePaid = totalFeePaid};
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
