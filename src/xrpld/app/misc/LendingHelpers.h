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

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>

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
LoanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

Number
LoanTotalValueOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

Number
LoanTotalInterestOutstanding(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

Number
LoanPeriodicPayment(
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining);

Number
LoanLatePaymentInterest(
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate);

LoanPaymentParts
LoanComputePaymentParts(ApplyView& view, SLE::ref loan);

}  // namespace detail

template <AssetType A>
Number
MinusFee(A const& asset, Number value, TenthBips32 managementFeeRate)
{
    return roundToAsset(
        asset, tenthBipsOfValue(value, tenthBipsPerUnity - managementFeeRate));
}

template <AssetType A>
Number
LoanInterestOutstandingMinusFee(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate)
{
    return MinusFee(
        asset,
        detail::LoanTotalInterestOutstanding(
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining),
        managementFeeRate);
}

template <AssetType A>
Number
LoanPeriodicPayment(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining)
{
    return roundToAsset(
        asset,
        detail::LoanPeriodicPayment(
            principalOutstanding,
            interestRate,
            paymentInterval,
            paymentsRemaining));
}

template <AssetType A>
Number
LoanLatePaymentInterest(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate)
{
    return roundToAsset(
        asset,
        detail::LoanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            parentCloseTime,
            startDate,
            prevPaymentDate));
}

struct LoanPaymentParts
{
    STAmount principalPaid;
    STAmount interestPaid;
    STAmount valueChange;
    STAmount feePaid;
};

template <AssetType A>
LoanPaymentParts
LoanComputePaymentParts(A const& asset, ApplyView& view, SLE::ref loan)
{
    auto const parts = detail::LoanComputePaymentParts(view, loan);
    return LoanPaymentParts{
        roundToAsset(asset, parts.principalPaid),
        roundToAsset(asset, parts.interestPaid),
        roundToAsset(asset, parts.valueChange),
        roundToAsset(asset, parts.feePaid)};
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
