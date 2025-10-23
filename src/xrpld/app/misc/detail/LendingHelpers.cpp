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
#include <xrpld/app/tx/detail/VaultCreate.h>

namespace ripple {

bool
checkLendingProtocolDependencies(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault) &&
        VaultCreate::checkExtraFeatures(ctx);
}

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    // Need floating point math for this one, since we're dividing by some
    // large numbers
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment), specifically "periodicRate = ...", though it is duplicated in
     * other places.
     */
    return tenthBipsOfValue(Number(paymentInterval), interestRate) /
        (365 * 24 * 60 * 60);
}

Number
calculateFullPaymentInterest(
    Number const& rawPrincipalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate)
{
    // If there is more than one payment remaining, see if enough was
    // paid for a full payment
    auto const accruedInterest = detail::loanAccruedInterest(
        rawPrincipalOutstanding,
        periodicRate,
        parentCloseTime,
        startDate,
        prevPaymentDate,
        paymentInterval);
    XRPL_ASSERT(
        accruedInterest >= 0,
        "ripple::detail::computeFullPaymentInterest : valid accrued interest");

    auto const prepaymentPenalty =
        tenthBipsOfValue(rawPrincipalOutstanding, closeInterestRate);
    XRPL_ASSERT(
        prepaymentPenalty >= 0,
        "ripple::detail::computeFullPaymentInterest : valid prepayment "
        "interest");

    return accruedInterest + prepaymentPenalty;
}

Number
calculateFullPaymentInterest(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate)
{
    Number const rawPrincipalOutstanding =
        detail::loanPrincipalFromPeriodicPayment(
            periodicPayment, periodicRate, paymentRemaining);

    return calculateFullPaymentInterest(
        rawPrincipalOutstanding,
        periodicRate,
        parentCloseTime,
        paymentInterval,
        prevPaymentDate,
        startDate,
        closeInterestRate);
}

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips16 const managementFeeRate)
{
    Number const rawValueOutstanding = periodicPayment * paymentRemaining;
    Number const rawPrincipalOutstanding =
        detail::loanPrincipalFromPeriodicPayment(
            periodicPayment, periodicRate, paymentRemaining);
    Number const rawInterestOutstanding =
        rawValueOutstanding - rawPrincipalOutstanding;
    Number const rawManagementFeeOutstanding =
        tenthBipsOfValue(rawInterestOutstanding, managementFeeRate);

    return LoanState{
        .valueOutstanding = rawValueOutstanding,
        .principalOutstanding = rawPrincipalOutstanding,
        .interestOutstanding = rawInterestOutstanding,
        .interestDue = rawInterestOutstanding - rawManagementFeeOutstanding,
        .managementFeeDue = rawManagementFeeOutstanding};
};

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips16 const managementFeeRate)
{
    return calculateRawLoanState(
        periodicPayment,
        loanPeriodicRate(interestRate, paymentInterval),
        paymentRemaining,
        managementFeeRate);
}

LoanState
calculateRoundedLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding)
{
    // This implementation is pretty trivial, but ensures the calculations are
    // consistent everywhere, and reduces copy/paste errors.
    Number const interestOutstanding =
        totalValueOutstanding - principalOutstanding;
    return {
        .valueOutstanding = totalValueOutstanding,
        .principalOutstanding = principalOutstanding,
        .interestOutstanding = interestOutstanding,
        .interestDue = interestOutstanding - managementFeeOutstanding,
        .managementFeeDue = managementFeeOutstanding};
}

LoanState
calculateRoundedLoanState(SLE::const_ref loan)
{
    return calculateRoundedLoanState(
        loan->at(sfTotalValueOutstanding),
        loan->at(sfPrincipalOutstanding),
        loan->at(sfManagementFeeOutstanding));
}

namespace detail {

Number
computeRaisedRate(Number const& periodicRate, std::uint32_t paymentsRemaining)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment), though "raisedRate" is computed only once and used twice.
     */
    return power(1 + periodicRate, paymentsRemaining);
}

Number
computePaymentFactor(
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment), though "raisedRate" is computed only once and used twice.
     */
    Number const raisedRate =
        computeRaisedRate(periodicRate, paymentsRemaining);

    return (periodicRate * raisedRate) / (raisedRate - 1);
}

Number
loanPeriodicPayment(
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;

    // Special case for interest free loans - equal payments of the principal.
    if (periodicRate == beast::zero)
        return principalOutstanding / paymentsRemaining;

    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment).
     */
    return principalOutstanding *
        computePaymentFactor(periodicRate, paymentsRemaining);
}

Number
loanPeriodicPayment(
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;
    /*
     * This function is derived from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * payment), though it is duplicated in other places.
     */
    Number const periodicRate = loanPeriodicRate(interestRate, paymentInterval);

    return loanPeriodicPayment(
        principalOutstanding, periodicRate, paymentsRemaining);
}

Number
loanPrincipalFromPeriodicPayment(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (periodicRate == 0)
        return periodicPayment * paymentsRemaining;

    /*
     * This formula is the reverse of the one from the XLS-66 spec,
     * section 3.2.4.1.1 (Regular Payment) used in loanPeriodicPayment
     */
    return periodicPayment /
        computePaymentFactor(periodicRate, paymentsRemaining);
}

std::pair<Number, Number>
computeInterestAndFeeParts(
    Number const& interest,
    TenthBips16 managementFeeRate)
{
    auto const fee = tenthBipsOfValue(interest, managementFeeRate);

    // No error tracking needed here because this is extra
    return std::make_pair(interest - fee, fee);
}

Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t nextPaymentDueDate)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.2 (Late payment),
     * specifically "latePaymentInterest = ..."
     *
     * The spec is to be updated to base the duration on the next due date
     */
    auto const secondsOverdue =
        parentCloseTime.time_since_epoch().count() - nextPaymentDueDate;

    auto const rate = loanPeriodicRate(lateInterestRate, secondsOverdue);

    return principalOutstanding * rate;
}

Number
loanAccruedInterest(
    Number const& principalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.4 (Early Full
     * Repayment), specifically "accruedInterest = ...".
     */
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    return principalOutstanding * periodicRate * secondsSinceLastPayment /
        paymentInterval;
}

}  // namespace detail

}  // namespace ripple
