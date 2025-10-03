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

namespace detail {

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
loanPeriodicPayment(
    Number principalOutstanding,
    Number periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;

    // Special case for interest free loans - equal payments of the principal.
    if (periodicRate == beast::zero)
        return principalOutstanding / paymentsRemaining;

    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment), though the awkwardly-named "timeFactor" is computed only once
     * and used twice.
     */
    // TODO: Need a better name
    Number const timeFactor = power(1 + periodicRate, paymentsRemaining);

    return principalOutstanding * periodicRate * timeFactor / (timeFactor - 1);
}

Number
loanPeriodicPayment(
    Number principalOutstanding,
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
loanLatePaymentInterest(
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate)
{
    /*
     * This formula is from the XLS-66 spec, section 3.2.4.1.2 (Late payment),
     * specifically "latePaymentInterest = ..."
     */
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    auto const rate =
        loanPeriodicRate(lateInterestRate, secondsSinceLastPayment);

    return principalOutstanding * rate;
}

Number
loanAccruedInterest(
    Number principalOutstanding,
    Number periodicRate,
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
