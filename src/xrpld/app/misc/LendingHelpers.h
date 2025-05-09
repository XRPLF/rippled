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

#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/st.h>

namespace ripple {

struct PreflightContext;

// Lending protocol has dependencies, so capture them here.
bool
lendingProtocolEnabled(PreflightContext const& ctx);

namespace detail {
// These functions should rarely be used directly. More often, the ultimate
// result needs to be roundToAsset'd.

struct LoanPaymentParts
{
    Number principalPaid;
    Number interestPaid;
    Number valueChange;
    Number feePaid;
};

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

Number
loanTotalValueOutstanding(
    Number periodicPayment,
    std::uint32_t paymentsRemaining);

Number
loanTotalValueOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

Number
loanTotalInterestOutstanding(
    Number principalOutstanding,
    Number totalValueOutstanding);

Number
loanTotalInterestOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

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

struct PeriodicPayment
{
    Number interest;
    Number principal;
};

template <AssetType A>
PeriodicPayment
computePeriodicPaymentParts(
    A const& asset,
    Number const& principalOutstanding,
    Number const& periodicPaymentAmount,
    Number const& periodicRate)
{
    Number const interest =
        roundToAsset(asset, principalOutstanding * periodicRate);
    XRPL_ASSERT(
        interest >= 0,
        "ripple::detail::computePeriodicPayment : valid interest");

    Number const principal =
        roundToAsset(asset, periodicPaymentAmount - interest);
    XRPL_ASSERT(
        principal > 0 && principal <= principalOutstanding,
        "ripple::detail::computePeriodicPayment : valid principal");

    return {interest, principal};
}

inline Number
minusManagementFee(Number value, TenthBips32 managementFeeRate)
{
    return tenthBipsOfValue(value, tenthBipsPerUnity - managementFeeRate);
}

}  // namespace detail

template <AssetType A>
Number
loanInterestOutstandingMinusFee(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate)
{
    return roundToAsset(
        asset,
        detail::minusManagementFee(
            detail::loanTotalInterestOutstanding(
                principalOutstanding,
                interestRate,
                paymentInterval,
                paymentsRemaining),
            managementFeeRate));
}

template <AssetType A>
Number
loanPeriodicPayment(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    return roundToAsset(
        asset,
        detail::loanPeriodicPayment(
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining));
}

template <AssetType A>
Number
loanLatePaymentInterest(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate)
{
    return roundToAsset(
        asset,
        detail::loanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            parentCloseTime,
            startDate,
            prevPaymentDate));
}

struct LoanPaymentParts
{
    Number principalPaid;
    Number interestPaid;
    Number valueChange;
    Number feePaid;
};

template <AssetType A>
Expected<LoanPaymentParts, TER>
loanComputePaymentParts(
    A const& asset,
    ApplyView& view,
    SLE::ref loan,
    STAmount const& amount,
    beast::Journal j)
{
    auto principalOutstandingField = loan->at(sfPrincipalOutstanding);
    bool const allowOverpayment = loan->isFlag(lsfLoanOverpayment);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};
    TenthBips32 const overpaymentInterestRate{
        loan->at(sfOverpaymentInterestRate)};

    Number const serviceFee = loan->at(sfLoanServiceFee);
    Number const latePaymentFee = loan->at(sfLatePaymentFee);
    Number const closePaymentFee =
        roundToAsset(asset, loan->at(sfClosePaymentFee));
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
        periodicRate > 0, "ripple::loanComputePaymentParts : valid rate");

    // Don't round the payment amount. Only round the final computations using
    // it.
    Number const periodicPaymentAmount = detail::loanPeriodicPayment(
        principalOutstandingField, periodicRate, paymentRemainingField);
    XRPL_ASSERT(
        periodicPaymentAmount > 0,
        "ripple::computePeriodicPayment : valid payment");

    auto const periodic = detail::computePeriodicPaymentParts(
        asset, principalOutstandingField, periodicPaymentAmount, periodicRate);

    Number const totalValueOutstanding = roundToAsset(
        asset,
        detail::loanTotalValueOutstanding(
            periodicPaymentAmount, paymentRemainingField));
    XRPL_ASSERT(
        totalValueOutstanding > 0,
        "ripple::loanComputePaymentParts : valid total value");
    Number const totalInterestOutstanding =
        detail::loanTotalInterestOutstanding(
            principalOutstandingField, totalValueOutstanding);
    XRPL_ASSERT(
        totalInterestOutstanding >= 0,
        "ripple::loanComputePaymentParts : valid total interest");

    view.update(loan);

    // -------------------------------------------------------------
    // late payment handling
    if (hasExpired(view, nextDueDateField))
    {
        // the payment is late
        auto const latePaymentInterest = loanLatePaymentInterest(
            asset,
            principalOutstandingField,
            lateInterestRate,
            view.parentCloseTime(),
            startDate,
            prevPaymentDateField);
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

    // -------------------------------------------------------------
    // full payment handling
    if (paymentRemainingField > 1)
    {
        // If there is more than one payment remaining, see if enough was paid
        // for a full payment
        auto const accruedInterest = roundToAsset(
            asset,
            detail::loanAccruedInterest(
                principalOutstandingField,
                periodicRate,
                view.parentCloseTime(),
                startDate,
                prevPaymentDateField,
                paymentInterval));
        XRPL_ASSERT(
            accruedInterest >= 0,
            "ripple::loanComputePaymentParts : valid accrued interest");
        auto const closePrepaymentInterest = roundToAsset(
            asset,
            tenthBipsOfValue(
                principalOutstandingField.value(), closeInterestRate));
        XRPL_ASSERT(
            closePrepaymentInterest >= 0,
            "ripple::loanComputePaymentParts : valid prepayment "
            "interest");
        auto const totalInterest = accruedInterest + closePrepaymentInterest;
        auto const closeFullPayment =
            principalOutstandingField + totalInterest + closePaymentFee;

        // if the payment is equal or higher than full payment amount, make a
        // full payment
        if (amount >= closeFullPayment)
        {
            // A full payment decreases the value of the loan by the
            // difference between the interest paid and the expected
            // outstanding interest return
            auto const valueChange = totalInterest - totalInterestOutstanding;

            LoanPaymentParts const result{
                principalOutstandingField,
                totalInterest,
                valueChange,
                closePaymentFee};

            paymentRemainingField = 0;
            principalOutstandingField = 0;

            return result;
        }
    }

    // -------------------------------------------------------------
    // normal payment handling

    // if the payment is not late nor if it's a full payment, then it must be a
    // periodic one, with possible overpayments

    std::optional<NumberRoundModeGuard> mg(Number::downward);
    std::int64_t const fullPeriodicPayments{
        amount / roundToAsset(asset, periodicPaymentAmount, Number::upward)};
    mg.reset();
    // Temporary asserts
    XRPL_ASSERT(
        amount >= periodicPaymentAmount || fullPeriodicPayments == 0,
        "temp full periodic rounding");
    XRPL_ASSERT(
        amount < periodicPaymentAmount || fullPeriodicPayments >= 1,
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

    std::optional<detail::PeriodicPayment> future = periodic;
    for (int i = 0; i < fullPeriodicPayments; ++i)
    {
        // Only do the work if we need to
        if (!future)
            future = detail::computePeriodicPaymentParts(
                asset,
                principalOutstandingField,
                periodicPaymentAmount,
                periodicRate);
        XRPL_ASSERT(
            future->interest < periodic.interest,
            "ripple::loanComputePaymentParts : decreasing interest");
        XRPL_ASSERT(
            future->principal > periodic.principal,
            "ripple::loanComputePaymentParts : increasing principal");

        totalPrincipalPaid += future->principal;
        totalInterestPaid += future->interest;
        paymentRemainingField -= 1;
        principalOutstandingField -= future->principal;

        future.reset();
    }

    Number totalFeePaid = serviceFee * fullPeriodicPayments;

    if (allowOverpayment)
    {
        Number const overpayment = std::min(
            principalOutstandingField.value(),
            amount - periodicPaymentAmount * fullPeriodicPayments);

        if (roundToAsset(asset, overpayment) > 0)
        {
            Number const interestPortion = roundToAsset(
                asset, tenthBipsOfValue(overpayment, overpaymentInterestRate));
            Number const feePortion = roundToAsset(
                asset, tenthBipsOfValue(overpayment, overpaymentFee));
            Number const remainder =
                roundToAsset(asset, overpayment - interestPortion - feePortion);

            // Don't process an overpayment if the whole amount (or more!) gets
            // eaten by fees
            if (remainder > 0)
            {
                totalPrincipalPaid += remainder;
                totalInterestPaid += interestPortion;
                totalFeePaid += feePortion;

                principalOutstandingField -= remainder;

                Number const newInterest = roundToAsset(
                    asset,
                    detail::loanTotalInterestOutstanding(
                        principalOutstandingField,
                        interestRate,
                        paymentInterval,
                        paymentRemainingField));

                loanValueChange =
                    (newInterest - totalInterestOutstanding) + interestPortion;
            }
        }
    }

    XRPL_ASSERT(
        loanValueChange <= 0,
        "ripple::loanComputePaymentParts : valid normal / overpayment "
        "value change");
    // Check the final results are rounded, to double-check that the
    // intermediate steps were rounded.
    XRPL_ASSERT(
        roundToAsset(asset, totalPrincipalPaid) == totalPrincipalPaid,
        "ripple::loanComputePaymentParts : totalPrincipalPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalInterestPaid) == totalInterestPaid,
        "ripple::loanComputePaymentParts : totalInterestPaid rounded");
    XRPL_ASSERT(
        roundToAsset(asset, loanValueChange) == loanValueChange,
        "ripple::loanComputePaymentParts : loanValueChange rounded");
    XRPL_ASSERT(
        roundToAsset(asset, totalFeePaid) == totalFeePaid,
        "ripple::loanComputePaymentParts : totalFeePaid rounded");
    return LoanPaymentParts{
        totalPrincipalPaid, totalInterestPaid, loanValueChange, totalFeePaid};
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
