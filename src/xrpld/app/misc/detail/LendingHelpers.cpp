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

#include <xrpl/protocol/Feature.h>

namespace ripple {

bool
LendingProtocolEnabled(PreflightContext const& ctx)
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
LoanPeriodicPayment(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;
    Number const periodicRate = LoanPeriodicRate(interestRate, paymentInterval);

    // TODO: Need a better name
    Number const timeFactor = power(1 + periodicRate, paymentsRemaining);

    return principalOutstanding * (periodicRate * timeFactor) /
        (timeFactor - 1);
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

}  // namespace detail

}  // namespace ripple
