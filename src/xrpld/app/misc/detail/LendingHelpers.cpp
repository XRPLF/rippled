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

#include <xrpld/app/misc/LendingHelpers.h>
//
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/st.h>

namespace ripple {

bool
lendingProtocolEnabled(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureLendingProtocol) &&
        VaultCreate::isEnabled(ctx);
}

namespace detail {

Number
LoanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    // Need floating point math for this one, since we're dividing by some
    // large numbers
    return tenthBipsOfValue(Number(paymentInterval), interestRate) /
        (365 * 24 * 60 * 60);
}

Number
LoanTotalValueOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    return LoanPeriodicPayment(
               principalOutstanding,
               interestRate,
               paymentInterval,
               paymentsRemaining) *
        paymentsRemaining;
}

Number
LoanTotalInterestOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    return LoanTotalValueOutstanding(
               principalOutstanding,
               interestRate,
               paymentInterval,
               paymentsRemaining) -
        principalOutstanding;
}

Number
LoanPeriodicPayment(
    Number principalOutstanding,
    Number periodicRate,
    std::uint32_t paymentsRemaining)
{
    // TODO: Need a better name
    Number const timeFactor = power(1 + periodicRate, paymentsRemaining);

    return principalOutstanding * (periodicRate * timeFactor) /
        (timeFactor - 1);
}

Number
LoanPeriodicPayment(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;
    Number const periodicRate = LoanPeriodicRate(interestRate, paymentInterval);

    return LoanPeriodicPayment(
        principalOutstanding, periodicRate, paymentsRemaining);
}

Number
LoanLatePaymentInterest(
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate)
{
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    auto const rate =
        LoanPeriodicRate(lateInterestRate, secondsSinceLastPayment);

    return principalOutstanding * rate;
}

Number
LoanAccruedInterest(
    Number principalOutstanding,
    TenthBips32 periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval)
{
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    return tenthBipsOfValue(
               principalOutstanding * secondsSinceLastPayment, periodicRate) /
        paymentInterval;
}

LoanPaymentParts
LoanComputePaymentParts(ApplyView& view, SLE::ref loan)
{
    Number const principalOutstanding = loan->at(sfPrincipalOutstanding);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};

    Number const latePaymentFee = loan->at(sfLatePaymentFee);
    Number const closePaymentFee = loan->at(sfClosePaymentFee);

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);
    std::uint32_t const paymentRemaining = loan->at(sfPaymentRemaining);

    std::uint32_t const prevPaymentDate = loan->at(sfPreviousPaymentDate);
    std::uint32_t const startDate = loan->at(sfStartDate);
    std::uint32_t const nextDueDate = loan->at(sfNextPaymentDueDate);

    // Compute the normal periodic rate, payment, etc.
    // We'll need it in the remaining calculations
    Number const periodicRate = LoanPeriodicRate(interestRate, paymentInterval);
    Number const periodicPaymentAmount = LoanPeriodicPayment(
        principalOutstanding, periodicRate, paymentRemaining);
    Number const periodicInterest = principalOutstanding * periodicRate;
    Number const periodicPrincipal = periodicPaymentAmount - periodicInterest;

    // the payment is late
    if (hasExpired(view, nextDueDate))
    {
        auto const latePaymentInterest = LoanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            view.parentCloseTime(),
            startDate,
            prevPaymentDate);
        auto const latePaymentAmount =
            periodicPaymentAmount + latePaymentInterest + latePaymentFee;

        loan->at(sfPaymentRemaining) -= 1;
        // A single payment always pays the same amount of principal. Only the
        // interest and fees are extra
        loan->at(sfPrincipalOutstanding) -= periodicPrincipal;

        // Make sure this does an assignment
        loan->at(sfPreviousPaymentDate) = loan->at(sfNextPaymentDueDate);
        loan->at(sfNextPaymentDueDate) += paymentInterval;

        // A late payment increases the value of the loan by the difference
        // between periodic and late payment interest
        return {
            periodicPrincipal,
            latePaymentInterest + periodicInterest,
            latePaymentInterest,
            latePaymentFee};
    }

    auto const accruedInterest = LoanAccruedInterest(
        principalOutstanding,
        interestRate,
        view.parentCloseTime(),
        startDate,
        prevPaymentDate,
        paymentInterval);
    auto const prepaymentPenalty =
        tenthBipsOfValue(principalOutstanding, closeInterestRate);

    assert(0);
    return {0, 0, 0, 0};
    /*
function make_payment(amount, current_time) -> (principal_paid, interest_paid,
value_change, fee_paid): if loan.payments_remaining is 0 ||
loan.principal_outstanding is 0 { return "loan complete" error
    }

    .....

    let full_payment = loan.compute_full_payment(current_time)

    // if the payment is equal or higher than full payment amount
    // and there is more than one payment remaining, make a full payment
    if amount >= full_payment && loan.payments_remaining > 1 {
        loan.payments_remaining = 0
        loan.principal_outstanding = 0

        // A full payment decreases the value of the loan by the difference
between the interest paid and the expected outstanding interest return
(full_payment.principal, full_payment.interest, full_payment.interest -
loan.compute_current_value().interest, full_payment.fee)
    }

    // if the payment is not late nor if it's a full payment, then it must be a
periodic once

    let periodic_payment = loan.compute_periodic_payment()

    let full_periodic_payments = floor(amount / periodic_payment)
    if full_periodic_payments < 1 {
        return "insufficient amount paid" error
    }

    loan.payments_remaining -= full_periodic_payments
    loan.next_payment_due_date = loan.next_payment_due_date +
loan.payment_interval * full_periodic_payments loan.last_payment_date =
loan.next_payment_due_date - loan.payment_interval


    let total_principal_paid = 0
    let total_interest_paid = 0
    let loan_value_change = 0
    let total_fee_paid = loan.service_fee * full_periodic_payments

    while full_periodic_payments > 0 {
        total_principal_paid += periodic_payment.principal
        total_interest_paid += periodic_payment.interest
        periodic_payment = loan.compute_periodic_payment()
        full_periodic_payments -= 1
    }

    loan.principal_outstanding -= total_principal_paid

    let overpayment = min(loan.principal_outstanding, amount % periodic_payment)
    if overpayment > 0 && is_set(lsfOverpayment) {
        let interest_portion = overpayment * loan.overpayment_interest_rate
        let fee_portion = overpayment * loan.overpayment_fee
        let remainder = overpayment - interest_portion - fee_portion

        total_principal_paid += remainder
        total_interest_paid += interest_portion
        total_fee_paid += fee_portion

        let current_value = loan.compute_current_value()
        loan.principal_outstanding -= remainder
        let new_value = loan.compute_current_value()

        loan_value_change = (new_value.interest - current_value.interest) +
interest_portion
    }

    return (total_principal_paid, total_interest_paid, loan_value_change,
total_fee_paid)
    */
}

}  // namespace detail

}  // namespace ripple
