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

namespace ripple {

bool
lendingProtocolEnabled(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureLendingProtocol) &&
        VaultCreate::isEnabled(ctx);
}

namespace detail {

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    // Need floating point math for this one, since we're dividing by some
    // large numbers
    return tenthBipsOfValue(Number(paymentInterval), interestRate) /
        (365 * 24 * 60 * 60);
}

Number
loanPeriodicPayment(
    Number principalOutstanding,
    Number periodicRate,
    std::uint32_t paymentsRemaining)
{
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
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    return principalOutstanding * periodicRate * secondsSinceLastPayment /
        paymentInterval;
}

}  // namespace detail

}  // namespace ripple
