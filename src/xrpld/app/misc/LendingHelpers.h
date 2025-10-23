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
template <AssetType A>
Number
roundPeriodicPayment(
    A const& asset,
    Number const& periodicPayment,
    std::int32_t scale)
{
    return roundToAsset(asset, periodicPayment, scale, Number::upward);
}

enum class SpecialCase { none, final, extra };

/// This structure is used internally to compute the breakdown of a
/// single loan payment
struct PaymentComponents
{
    Number rawInterest;
    Number rawPrincipal;
    Number rawManagementFee;
    Number roundedInterest;
    Number roundedPrincipal;
    // roundedManagementFee is explicitly on for the portion of the pre-computed
    // periodic payment that goes toward the Broker's management fee, which is
    // tracked by sfManagementFeeOutstanding
    Number roundedManagementFee;
    SpecialCase specialCase = SpecialCase::none;
};

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
    operator+=(LoanPaymentParts const& other)
    {
        principalPaid += other.principalPaid;
        interestPaid += other.interestPaid;
        valueChange += other.valueChange;
        feePaid += other.feePaid;
        return *this;
    }
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
    /// Interest still due to be paid by the borrower.
    Number interestOutstanding;
    /// Interest still due to be paid TO the Vault.
    // This is a portion of interestOutstanding
    Number interestDue;
    /// Management fee still due to be paid TO the broker.
    // This is a portion of interestOutstanding
    Number managementFeeDue;
};

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips16 const managementFeeRate);

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips16 const managementFeeRate);

LoanState
calculateRoundedLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding);

LoanState
calculateRoundedLoanState(SLE::const_ref loan);

template <AssetType A>
Number
computeFee(
    A const& asset,
    Number const& value,
    TenthBips16 managementFeeRate,
    std::int32_t scale)
{
    return roundToAsset(
        asset,
        tenthBipsOfValue(value, managementFeeRate),
        scale,
        Number::downward);
}

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
// These functions should rarely be used directly. More often, the ultimate
// result needs to be roundToAsset'd.

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
loanPrincipalFromPeriodicPayment(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining);

Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t nextPaymentDueDate);

Number
loanAccruedInterest(
    Number const& principalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval);

#if LOANCOMPLETE
inline Number
minusFee(Number const& value, TenthBips16 managementFeeRate)
{
    return tenthBipsOfValue(value, tenthBipsPerUnity - managementFeeRate);
}
#endif

template <AssetType A>
Number
computeRoundedPrincipalComponent(
    A const& asset,
    Number const& principalOutstanding,
    Number const& rawPrincipalOutstanding,
    Number const& rawPrincipal,
    Number const& roundedPeriodicPayment,
    std::int32_t scale)
{
    // Adjust the principal payment by the rounding error between the true
    // and  rounded principal outstanding
    auto const diff = roundToAsset(
        asset,
        principalOutstanding - rawPrincipalOutstanding,
        scale,
        Number::downward);

    // If the rounded principal outstanding is greater than the true
    // principal outstanding, we need to pay more principal to reduce
    // the rounded principal outstanding
    //
    // If the rounded principal outstanding is less than the true
    // principal outstanding, we need to pay less principal to allow the
    // rounded principal outstanding to catch up

    auto const p =
        roundToAsset(asset, rawPrincipal + diff, scale, Number::downward);

    // For particular loans, it's entirely possible for many of the first
    // rounded payments to be all principal.
    XRPL_ASSERT_PARTS(
        p >= 0,
        "rippled::detail::computeRoundedPrincipalComponent",
        "principal part not negative");
    XRPL_ASSERT_PARTS(
        p <= principalOutstanding,
        "rippled::detail::computeRoundedPrincipalComponent",
        "principal part not larger than outstanding principal");
    XRPL_ASSERT_PARTS(
        !asset.integral() || abs(p - rawPrincipal) <= 1,
        "rippled::detail::computeRoundedPrincipalComponent",
        "principal part not larger than outstanding principal");
    XRPL_ASSERT_PARTS(
        p <= roundedPeriodicPayment,
        "rippled::detail::computeRoundedPrincipalComponent",
        "principal part not larger than total payment");

    // The asserts will be skipped in release builds, so check here to make
    // sure nothing goes negative
    if (p > roundedPeriodicPayment || p > principalOutstanding)
        return std::min(roundedPeriodicPayment, principalOutstanding);
    else if (p < 0)
        return Number{};

    return p;
}

/** Returns the interest component of a payment WITHOUT accounting for
 ** management fees
 *
 * In other words, it returns the combined value of the interest part that will
 * go to the Vault and the management fee that will go to the Broker.
 */
template <AssetType A>
Number
computeRoundedInterestComponent(
    A const& asset,
    Number const& interestOutstanding,
    Number const& roundedPrincipal,
    Number const& rawInterestOutstanding,
    Number const& roundedPeriodicPayment,
    std::int32_t scale)
{
    // Start by just using the non-principal part of the payment for interest
    Number roundedInterest = roundedPeriodicPayment - roundedPrincipal;
    XRPL_ASSERT_PARTS(
        isRounded(asset, roundedInterest, scale),
        "ripple::detail::computeRoundedInterestComponent",
        "initial interest computation is rounded");

    {
        // Adjust the interest payment by the rounding error between the true
        // and rounded interest outstanding
        //
        // If the rounded interest outstanding is greater than the true interest
        // outstanding, we need to pay more interest to reduce the rounded
        // interest outstanding
        //
        // If the rounded interest outstanding is less than the true interest
        // outstanding, we need to pay less interest to allow the rounded
        // interest outstanding to catch up
        auto const diff = roundToAsset(
            asset,
            interestOutstanding - rawInterestOutstanding,
            scale,
            Number::downward);
        if (diff < beast::zero)
            roundedInterest += diff;
    }

    // However, we cannot allow negative interest payments, therefore we need to
    // cap the interest payment at 0.
    //
    // Ensure interest payment is non-negative and does not exceed the remaining
    // payment after principal
    return std::max(Number{}, roundedInterest);
}

// The Interest and Fee components need to be calculated together, because they
// can affect each other during computation in both directions.
template <AssetType A>
std::pair<Number, Number>
computeRoundedInterestAndFeeComponents(
    A const& asset,
    Number const& interestOutstanding,
    Number const& managementFeeOutstanding,
    Number const& roundedPrincipal,
    Number const& rawInterestOutstanding,
    Number const& rawManagementFeeOutstanding,
    Number const& roundedPeriodicPayment,
    Number const& periodicRate,
    TenthBips16 managementFeeRate,
    std::int32_t scale)
{
    // Zero interest means ZERO interest
    if (periodicRate == 0)
        return std::make_pair(Number{}, Number{});

    Number roundedInterest = computeRoundedInterestComponent(
        asset,
        interestOutstanding,
        roundedPrincipal,
        rawInterestOutstanding,
        roundedPeriodicPayment,
        scale);

    Number roundedFee =
        computeFee(asset, roundedInterest, managementFeeRate, scale);

    {
        // Adjust the interest fee by the rounding error between the true and
        // rounded interest fee outstanding
        auto const diff = roundToAsset(
            asset,
            managementFeeOutstanding - rawManagementFeeOutstanding,
            scale,
            Number::downward);

        roundedFee += diff;

        // But again, we cannot allow negative interest fees, therefore we need
        // to cap the interest fee at 0
        roundedFee = std::max(Number{}, roundedFee);

        // Finally, the rounded interest fee cannot exceed the outstanding
        // interest fee
        roundedFee = std::min(roundedFee, managementFeeOutstanding);
    }

    // Remove the fee portion from the interest payment, as the fee is paid
    // separately

    // Ensure that the interest payment does not become negative, this may
    // happen with high interest fees
    roundedInterest = std::max(Number{}, roundedInterest - roundedFee);

    // Finally,  ensure that the interest payment does not exceed the
    // interest outstanding
    roundedInterest = std::min(interestOutstanding, roundedInterest);

    // Make sure the parts don't add up to too much
    Number excess = roundedPeriodicPayment - roundedPrincipal -
        roundedInterest - roundedFee;

    XRPL_ASSERT_PARTS(
        isRounded(asset, excess, scale),
        "ripple::detail::computeRoundedInterestAndFeeComponents",
        "excess is rounded");

    if (excess < beast::zero)
    {
        // Take as much of the excess as we can out of the interest
        auto part = std::min(roundedInterest, abs(excess));
        roundedInterest -= part;
        excess += part;

        XRPL_ASSERT_PARTS(
            excess <= beast::zero,
            "ripple::detail::computeRoundedInterestAndFeeComponents",
            "excess not positive (interest)");
    }
    if (excess < beast::zero)
    {
        // If there's any left, take as much of the excess as we can out of the
        // fee
        auto part = std::min(roundedFee, abs(excess));
        roundedFee -= part;
        excess += part;
    }

    // The excess should never be negative, which indicates that the parts are
    // trying to take more than the whole payment. The excess can be positive,
    // which indicates that we're not going to take the whole payment amount,
    // but if so, it must be small.
    XRPL_ASSERT_PARTS(
        excess == beast::zero ||
            (excess > beast::zero &&
             ((asset.integral() && excess < 3) ||
              (roundedPeriodicPayment.exponent() - excess.exponent() > 6))),
        "ripple::detail::computeRoundedInterestAndFeeComponents",
        "excess is zero (fee)");

    XRPL_ASSERT_PARTS(
        roundedFee >= beast::zero,
        "ripple::detail::computeRoundedInterestAndFeeComponents",
        "non-negative fee");
    XRPL_ASSERT_PARTS(
        roundedInterest >= beast::zero,
        "ripple::detail::computeRoundedInterestAndFeeComponents",
        "non-negative interest");

    return std::make_pair(
        std::max(Number{}, roundedInterest), std::max(Number{}, roundedFee));
}

struct PaymentComponentsPlus : public PaymentComponents
{
    Number extraFee{0};
    Number valueChange{0};
    Number totalDue;

    PaymentComponentsPlus(
        PaymentComponents const& p,
        Number f,
        Number v = Number{})
        : PaymentComponents(p)
        , extraFee(f)
        , valueChange(v)
        , totalDue(
              roundedPrincipal + roundedInterest + roundedManagementFee +
              extraFee)
    {
    }
};

template <class NumberProxy, class UInt32Proxy, class UInt32OptionalProxy>
LoanPaymentParts
doPayment(
    PaymentComponentsPlus const& payment,
    NumberProxy& totalValueOutstandingProxy,
    NumberProxy& principalOutstandingProxy,
    NumberProxy& managementFeeOutstandingProxy,
    UInt32Proxy& paymentRemainingProxy,
    UInt32Proxy& prevPaymentDateProxy,
    UInt32OptionalProxy& nextDueDateProxy,
    std::uint32_t paymentInterval)
{
    XRPL_ASSERT_PARTS(
        nextDueDateProxy,
        "ripple::detail::doPayment",
        "Next due date proxy set");

    auto const totalValueDelta = payment.roundedPrincipal +
        payment.roundedInterest + payment.roundedManagementFee -
        payment.valueChange;
    if (payment.specialCase == SpecialCase::final)
    {
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy == payment.roundedPrincipal,
            "ripple::detail::doPayment",
            "Full principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy == totalValueDelta,
            "ripple::detail::doPayment",
            "Full value payment");
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy == payment.roundedManagementFee,
            "ripple::detail::doPayment",
            "Full management fee payment");

        paymentRemainingProxy = 0;

        prevPaymentDateProxy = *nextDueDateProxy;
        // Remove the field. This is the only condition where nextDueDate is
        // allowed to be removed.
        nextDueDateProxy = std::nullopt;

        // Always zero out the the tracked values on a final payment
        principalOutstandingProxy = 0;
        totalValueOutstandingProxy = 0;
        managementFeeOutstandingProxy = 0;
    }
    else
    {
        if (payment.specialCase != SpecialCase::extra)
        {
            paymentRemainingProxy -= 1;

            prevPaymentDateProxy = *nextDueDateProxy;
            // STObject::OptionalField does not define operator+=, so do it the
            // old-fashioned way.
            nextDueDateProxy = *nextDueDateProxy + paymentInterval;
        }
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy > payment.roundedPrincipal,
            "ripple::detail::doPayment",
            "Partial principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy > totalValueDelta,
            "ripple::detail::doPayment",
            "Partial value payment");
        // Management fees are expected to be relatively small, and could get to
        // zero before the loan is paid off
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy >= payment.roundedManagementFee,
            "ripple::detail::doPayment",
            "Valid management fee");

        principalOutstandingProxy -= payment.roundedPrincipal;
        totalValueOutstandingProxy -= totalValueDelta;
        managementFeeOutstandingProxy -= payment.roundedManagementFee;
    }

    XRPL_ASSERT_PARTS(
        // Use an explicit cast because the template parameter can be
        // ValueProxy<Number> or Number
        static_cast<Number>(principalOutstandingProxy) <=
            static_cast<Number>(totalValueOutstandingProxy),
        "ripple::detail::doPayment",
        "principal does not exceed total");
    XRPL_ASSERT_PARTS(
        // Use an explicit cast because the template parameter can be
        // ValueProxy<Number> or Number
        static_cast<Number>(managementFeeOutstandingProxy) >= beast::zero,
        "ripple::detail::doPayment",
        "fee outstanding stays valid");

    return LoanPaymentParts{
        .principalPaid = payment.roundedPrincipal,
        .interestPaid = payment.roundedInterest,
        .valueChange = payment.valueChange,
        // Now that the adjustments have been made, the fee parts can be
        // combined
        .feePaid = payment.roundedManagementFee + payment.extraFee};
}

// This function mainly exists to guarantee isolation of the "sandbox"
// variables from the real / proxy variables that will affect actual
// ledger data in the caller.
template <AssetType A>
Expected<LoanPaymentParts, TER>
tryOverpayment(
    A const& asset,
    std::int32_t loanScale,
    PaymentComponentsPlus const& overpaymentComponents,
    Number& totalValueOutstanding,
    Number& principalOutstanding,
    Number& managementFeeOutstanding,
    Number& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    std::uint32_t prevPaymentDate,
    std::optional<std::uint32_t> nextDueDate,
    TenthBips16 const managementFeeRate,
    beast::Journal j)
{
    auto const raw = calculateRawLoanState(
        periodicPayment, periodicRate, paymentRemaining, managementFeeRate);
    auto const rounded = calculateRoundedLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    auto const totalValueError = totalValueOutstanding - raw.valueOutstanding;
    auto const principalError = principalOutstanding - raw.principalOutstanding;
    auto const feeError = managementFeeOutstanding - raw.managementFeeDue;

    auto const newRawPrincipal =
        raw.principalOutstanding - overpaymentComponents.roundedPrincipal;

    auto newLoanProperties = computeLoanProperties(
        asset,
        newRawPrincipal,
        interestRate,
        paymentInterval,
        paymentRemaining,
        managementFeeRate);

    auto const newRaw = calculateRawLoanState(
        newLoanProperties.periodicPayment,
        periodicRate,
        paymentRemaining,
        managementFeeRate);

    totalValueOutstanding = roundToAsset(
        asset, newRaw.valueOutstanding + totalValueError, loanScale);
    principalOutstanding = roundToAsset(
        asset,
        newRaw.principalOutstanding + principalError,
        loanScale,
        Number::downward);
    managementFeeOutstanding =
        roundToAsset(asset, newRaw.managementFeeDue + feeError, loanScale);

    periodicPayment = newLoanProperties.periodicPayment;

    // check that the loan is still valid
    if (newLoanProperties.firstPaymentPrincipal <= 0 &&
        principalOutstanding > 0)
    {
        // The overpayment has caused the loan to be in a state
        // where no further principal can be paid.
        JLOG(j.warn())
            << "Loan overpayment would cause loan to be stuck. "
               "Rejecting overpayment, but normal payments are unaffected.";
        return Unexpected(tesSUCCESS);
    }

    // Check that the other computed values are valid
    if (newLoanProperties.periodicPayment <= 0 ||
        newLoanProperties.totalValueOutstanding <= 0 ||
        newLoanProperties.managementFeeOwedToBroker < 0)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Overpayment not allowed: Computed loan "
                          "properties are invalid. Does "
                          "not compute. TotalValueOutstanding: "
                       << newLoanProperties.totalValueOutstanding
                       << ", PeriodicPayment : "
                       << newLoanProperties.periodicPayment
                       << ", ManagementFeeOwedToBroker: "
                       << newLoanProperties.managementFeeOwedToBroker;
        return Unexpected(tesSUCCESS);
        // LCOV_EXCL_STOP
    }

    auto const newRounded = calculateRoundedLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);
    auto const valueChange =
        newRounded.interestOutstanding - rounded.interestOutstanding;
    XRPL_ASSERT_PARTS(
        valueChange < beast::zero,
        "ripple::detail::tryOverpayment",
        "principal overpayment reduced value of loan");

    return LoanPaymentParts{
        .principalPaid =
            rounded.principalOutstanding - newRounded.principalOutstanding,
        .interestPaid = rounded.interestDue - newRounded.interestDue,
        .valueChange = valueChange + overpaymentComponents.valueChange,
        .feePaid = rounded.managementFeeDue - newRounded.managementFeeDue +
            overpaymentComponents.extraFee};
}

template <AssetType A, class NumberProxy>
Expected<LoanPaymentParts, TER>
computeOverpayment(
    A const& asset,
    std::int32_t loanScale,
    PaymentComponentsPlus const& overpaymentComponents,
    NumberProxy& totalValueOutstandingProxy,
    NumberProxy& principalOutstandingProxy,
    NumberProxy& managementFeeOutstandingProxy,
    NumberProxy& periodicPaymentProxy,
    TenthBips32 const interestRate,
    std::uint32_t const paymentInterval,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    std::uint32_t const prevPaymentDate,
    std::optional<std::uint32_t> const nextDueDate,
    TenthBips16 const managementFeeRate,
    beast::Journal j)
{
    // Use temp variables to do the payment, so they can be thrown away if
    // they don't work
    Number totalValueOutstanding = totalValueOutstandingProxy;
    Number principalOutstanding = principalOutstandingProxy;
    Number managementFeeOutstanding = managementFeeOutstandingProxy;
    Number periodicPayment = periodicPaymentProxy;

    auto const ret = tryOverpayment(
        asset,
        loanScale,
        overpaymentComponents,
        totalValueOutstanding,
        principalOutstanding,
        managementFeeOutstanding,
        periodicPayment,
        interestRate,
        paymentInterval,
        periodicRate,
        paymentRemaining,
        prevPaymentDate,
        nextDueDate,
        managementFeeRate,
        j);
    if (!ret)
        return Unexpected(ret.error());

    auto const& loanPaymentParts = *ret;

    if (principalOutstandingProxy <= principalOutstanding)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Overpayment not allowed: principal "
                       << "outstanding did not decrease. Before: "
                       << *principalOutstandingProxy
                       << ". After: " << principalOutstanding;
        return Unexpected(tesSUCCESS);
        // LCOV_EXCL_STOP
    }

    XRPL_ASSERT_PARTS(
        principalOutstandingProxy - principalOutstanding ==
            overpaymentComponents.roundedPrincipal,
        "ripple::detail::computeOverpayment",
        "principal change agrees");

    XRPL_ASSERT_PARTS(
        managementFeeOutstandingProxy - managementFeeOutstanding ==
            overpaymentComponents.roundedManagementFee,
        "ripple::detail::computeOverpayment",
        "no fee change");

    XRPL_ASSERT_PARTS(
        totalValueOutstandingProxy - totalValueOutstanding -
                overpaymentComponents.roundedPrincipal ==
            overpaymentComponents.valueChange,
        "ripple::detail::computeOverpayment",
        "value change agrees");

    XRPL_ASSERT_PARTS(
        loanPaymentParts.principalPaid ==
            overpaymentComponents.roundedPrincipal,
        "ripple::detail::computeOverpayment",
        "principal payment matches");

    XRPL_ASSERT_PARTS(
        loanPaymentParts.feePaid ==
            overpaymentComponents.extraFee +
                overpaymentComponents.roundedManagementFee,
        "ripple::detail::computeOverpayment",
        "fee payment matches");

    // Update the loan object (via proxies)
    totalValueOutstandingProxy = totalValueOutstanding;
    principalOutstandingProxy = principalOutstanding;
    managementFeeOutstandingProxy = managementFeeOutstanding;
    periodicPaymentProxy = periodicPayment;

    return loanPaymentParts;
}

std::pair<Number, Number>
computeInterestAndFeeParts(
    Number const& interest,
    TenthBips16 managementFeeRate);

template <AssetType A>
std::pair<Number, Number>
computeInterestAndFeeParts(
    A const& asset,
    Number const& interest,
    TenthBips16 managementFeeRate,
    std::int32_t loanScale)
{
    auto const fee = computeFee(asset, interest, managementFeeRate, loanScale);

    return std::make_pair(interest - fee, fee);
}

/** Handle possible late payments.
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
template <AssetType A>
Expected<PaymentComponentsPlus, TER>
computeLatePayment(
    A const& asset,
    ApplyView const& view,
    Number const& principalOutstanding,
    std::int32_t nextDueDate,
    PaymentComponentsPlus const& periodic,
    TenthBips32 lateInterestRate,
    std::int32_t loanScale,
    Number const& latePaymentFee,
    STAmount const& amount,
    TenthBips16 managementFeeRate,
    beast::Journal j)
{
    if (!hasExpired(view, nextDueDate))
        return Unexpected(tesSUCCESS);

    // the payment is late
    // Late payment interest is only the part of the interest that comes
    // from being late, as computed by 3.2.4.1.2.
    auto const latePaymentInterest = loanLatePaymentInterest(
        principalOutstanding,
        lateInterestRate,
        view.parentCloseTime(),
        nextDueDate);

    auto const [rawLateInterest, rawLateManagementFee] =
        computeInterestAndFeeParts(latePaymentInterest, managementFeeRate);
    auto const [roundedLateInterest, roundedLateManagementFee] = [&]() {
        auto const interest =
            roundToAsset(asset, latePaymentInterest, loanScale);
        return computeInterestAndFeeParts(
            asset, interest, managementFeeRate, loanScale);
    }();

    XRPL_ASSERT(
        roundedLateInterest >= 0,
        "ripple::detail::computeLatePayment : valid late interest");
    PaymentComponentsPlus const late{
        PaymentComponents{
            .rawInterest = periodic.rawInterest + rawLateInterest,
            .rawPrincipal = periodic.rawPrincipal,
            .rawManagementFee = periodic.rawManagementFee,
            .roundedInterest = periodic.roundedInterest + roundedLateInterest,
            .roundedPrincipal = periodic.roundedPrincipal,
            .roundedManagementFee = periodic.roundedManagementFee},
        // A late payment pays both the normal fee, and the extra fees
        periodic.extraFee + latePaymentFee + roundedLateManagementFee,
        // A late payment increases the value of the loan by the difference
        // between periodic and late payment interest
        roundedLateInterest};

    XRPL_ASSERT_PARTS(
        isRounded(asset, late.totalDue, loanScale),
        "ripple::detail::computeLatePayment",
        "total due is rounded");

    if (amount < late.totalDue)
    {
        JLOG(j.warn()) << "Late loan payment amount is insufficient. Due: "
                       << late.totalDue << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    return late;
}

/* Handle possible full payments.
 *
 * If this function processed a full payment, the return value will be
 * a PaymentComponentsPlus object. Otherwise, it'll be an Unexpected with the
 * error code the caller is expected to return. It should NEVER return
 * tesSUCCESS
 */
template <AssetType A>
Expected<PaymentComponentsPlus, TER>
computeFullPayment(
    A const& asset,
    ApplyView& view,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding,
    Number const& periodicPayment,
    std::uint32_t paymentRemaining,
    std::uint32_t prevPaymentDate,
    std::uint32_t const startDate,
    std::uint32_t const paymentInterval,
    TenthBips32 const closeInterestRate,
    std::int32_t loanScale,
    Number const& totalInterestOutstanding,
    Number const& periodicRate,
    Number const& closePaymentFee,
    STAmount const& amount,
    TenthBips16 managementFeeRate,
    beast::Journal j)
{
    if (paymentRemaining <= 1)
        // If this is the last payment, it has to be a regular payment
        return Unexpected(tecKILLED);

    Number const rawPrincipalOutstanding = loanPrincipalFromPeriodicPayment(
        periodicPayment, periodicRate, paymentRemaining);

    // Full payment interest consists of accrued normal interest and the
    // prepayment penalty, as computed by 3.2.4.1.4.
    auto const fullPaymentInterest = calculateFullPaymentInterest(
        rawPrincipalOutstanding,
        periodicRate,
        view.parentCloseTime(),
        paymentInterval,
        prevPaymentDate,
        startDate,
        closeInterestRate);

    auto const [rawFullInterest, rawFullManagementFee] =
        computeInterestAndFeeParts(fullPaymentInterest, managementFeeRate);

    auto const [roundedFullInterest, roundedFullManagementFee] = [&]() {
        auto const interest =
            roundToAsset(asset, fullPaymentInterest, loanScale);
        auto const parts = computeInterestAndFeeParts(
            asset, interest, managementFeeRate, loanScale);
        // Apply as much of the fee to the outstanding fee, but no
        // more
        return std::make_tuple(parts.first, parts.second);
    }();

    PaymentComponentsPlus const full{
        PaymentComponents{
            .rawInterest = rawFullInterest,
            .rawPrincipal = rawPrincipalOutstanding,
            .rawManagementFee = rawFullManagementFee,
            .roundedInterest = roundedFullInterest,
            .roundedPrincipal = principalOutstanding,
            // to make the accounting work later, the tracked part of the fee
            // must be paid in full
            .roundedManagementFee = managementFeeOutstanding,
            .specialCase = SpecialCase::final},
        // A full payment pays the single close payment fee, plus the computed
        // management fee part of the interest portion, but for tracking, the
        // outstanding part is removed. That could make this value negative, but
        // that's ok, because it's not used until it's recombined with
        // roundedManagementFee.
        closePaymentFee + roundedFullManagementFee - managementFeeOutstanding,
        // A full payment decreases the value of the loan by the
        // difference between the interest paid and the expected
        // outstanding interest return
        roundedFullInterest - totalInterestOutstanding};

    XRPL_ASSERT_PARTS(
        isRounded(asset, full.totalDue, loanScale),
        "ripple::detail::computeFullPayment",
        "total due is rounded");

    if (amount < full.totalDue)
        // If the payment is less than the full payment amount, it's not
        // sufficient to be a full payment, but that's not an error.
        return Unexpected(tecINSUFFICIENT_PAYMENT);

    return full;
}

}  // namespace detail

template <AssetType A>
Number
valueMinusFee(
    A const& asset,
    Number const& value,
    TenthBips16 managementFeeRate,
    std::int32_t scale)
{
    return value - computeFee(asset, value, managementFeeRate, scale);
}

template <AssetType A>
PaymentComponents
computePaymentComponents(
    A const& asset,
    std::int32_t scale,
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    TenthBips16 managementFeeRate)
{
    /*
     * This function is derived from the XLS-66 spec, section 3.2.4.1.1 (Regular
     * Payment)
     */
    XRPL_ASSERT_PARTS(
        isRounded(asset, totalValueOutstanding, scale) &&
            isRounded(asset, principalOutstanding, scale) &&
            isRounded(asset, managementFeeOutstanding, scale),
        "ripple::detail::computePaymentComponents",
        "Outstanding values are rounded");
    auto const roundedPeriodicPayment =
        roundPeriodicPayment(asset, periodicPayment, scale);

    LoanState const raw = calculateRawLoanState(
        periodicPayment, periodicRate, paymentRemaining, managementFeeRate);

    if (paymentRemaining == 1 ||
        totalValueOutstanding <= roundedPeriodicPayment)
    {
        // If there's only one payment left, we need to pay off each of the loan
        // parts. It's probably impossible for the subtraction to result in a
        // negative value, but don't leave anything to chance.
        Number interest = std::max(
            Number{},
            totalValueOutstanding - principalOutstanding -
                managementFeeOutstanding);

        // Pay everything off
        return PaymentComponents{
            .rawInterest = raw.interestOutstanding,
            .rawPrincipal = raw.principalOutstanding,
            .rawManagementFee = raw.managementFeeDue,
            .roundedInterest = interest,
            .roundedPrincipal = principalOutstanding,
            .roundedManagementFee = managementFeeOutstanding,
            .specialCase = SpecialCase::final};
    }

    /*
     * From the spec, once the periodicPayment is computed:
     *
     * The principal and interest portions can be derived as follows:
     *  interest = principalOutstanding * periodicRate
     *  principal = periodicPayment - interest
     */
    Number const rawInterest = raw.principalOutstanding * periodicRate;
    Number const rawPrincipal = periodicPayment - rawInterest;
    Number const rawFee = tenthBipsOfValue(rawInterest, managementFeeRate);
    XRPL_ASSERT_PARTS(
        rawInterest >= 0,
        "ripple::detail::computePaymentComponents",
        "valid raw interest");
    XRPL_ASSERT_PARTS(
        rawPrincipal >= 0 && rawPrincipal <= raw.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "valid raw principal");
    XRPL_ASSERT_PARTS(
        rawFee >= 0 && rawFee <= raw.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "valid raw fee");

    /*
        Critical Calculation: Balancing Principal and Interest Outstanding

        This calculation maintains a delicate balance between keeping
        principal outstanding and interest outstanding as close as possible to
        reference values. However, we cannot perfectly match the reference
       values due to rounding issues.

        Key considerations:
            1. Since the periodic payment is rounded up, we have excess funds
               that can be used to pay down the loan faster than the reference
               calculation.

            2. We must ensure that loan repayment is not too fast, otherwise we
               will end up with negative principal outstanding or negative
       interest outstanding.

            3. We cannot allow the borrower to repay interest ahead of schedule.
               If the borrower makes an overpayment, the interest portion could
       go negative, requiring complex recalculation to refund the borrower by
               reflecting the overpayment in the principal portion of the loan.
    */

    Number const roundedPrincipal = detail::computeRoundedPrincipalComponent(
        asset,
        principalOutstanding,
        raw.principalOutstanding,
        rawPrincipal,
        roundedPeriodicPayment,
        scale);

    auto const [roundedInterest, roundedFee] =
        detail::computeRoundedInterestAndFeeComponents(
            asset,
            totalValueOutstanding - principalOutstanding,
            managementFeeOutstanding,
            roundedPrincipal,
            raw.interestOutstanding,
            raw.managementFeeDue,
            roundedPeriodicPayment,
            periodicRate,
            managementFeeRate,
            scale);

    XRPL_ASSERT_PARTS(
        roundedInterest >= 0 && isRounded(asset, roundedInterest, scale),
        "ripple::detail::computePaymentComponents",
        "valid rounded interest");
    XRPL_ASSERT_PARTS(
        roundedFee >= 0 && roundedFee <= managementFeeOutstanding &&
            isRounded(asset, roundedFee, scale),
        "ripple::detail::computePaymentComponents",
        "valid rounded fee");
    XRPL_ASSERT_PARTS(
        roundedPrincipal >= 0 && roundedPrincipal <= principalOutstanding &&
            roundedPrincipal <= roundedPeriodicPayment &&
            isRounded(asset, roundedPrincipal, scale),
        "ripple::detail::computePaymentComponents",
        "valid rounded principal");
    XRPL_ASSERT_PARTS(
        roundedPrincipal + roundedInterest + roundedFee <=
            roundedPeriodicPayment,
        "ripple::detail::computePaymentComponents",
        "payment parts fit within payment limit");

    return PaymentComponents{
        .rawInterest = rawInterest - rawFee,
        .rawPrincipal = rawPrincipal,
        .rawManagementFee = rawFee,
        .roundedInterest = roundedInterest,
        .roundedPrincipal = roundedPrincipal,
        .roundedManagementFee = roundedFee,
    };
}

template <AssetType A>
LoanProperties
computeLoanProperties(
    A const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips16 managementFeeRate)
{
    auto const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(
        interestRate == 0 || periodicRate > 0,
        "ripple::computeLoanProperties : valid rate");

    auto const periodicPayment = detail::loanPeriodicPayment(
        principalOutstanding, periodicRate, paymentsRemaining);
    STAmount const totalValueOutstanding = [&]() {
        NumberRoundModeGuard mg(Number::to_nearest);
        // Use STAmount's internal rounding instead of roundToAsset, because
        // we're going to use this result to determine the scale for all the
        // other rounding.
        return STAmount{
            asset,
            /*
             * This formula is from the XLS-66 spec, section 3.2.4.2 (Total
             * Loan Value Calculation), specifically "totalValueOutstanding
             * = ..."
             */
            periodicPayment * paymentsRemaining};
    }();
    // Base the loan scale on the total value, since that's going to be the
    // biggest number involved (barring unusual parameters for late, full, or
    // over payments)
    auto const loanScale = totalValueOutstanding.exponent();
    XRPL_ASSERT_PARTS(
        totalValueOutstanding.integral() && loanScale == 0 ||
            !totalValueOutstanding.integral() &&
                loanScale ==
                    static_cast<Number>(totalValueOutstanding).exponent(),
        "ripple::computeLoanProperties",
        "loanScale value fits expectations");

    // Since we just figured out the loan scale, we haven't been able to
    // validate that the principal fits in it, so to allow this function to
    // succeed, round it here, and let the caller do the validation.
    principalOutstanding = roundToAsset(
        asset, principalOutstanding, loanScale, Number::to_nearest);

    auto const feeOwedToBroker = computeFee(
        asset,
        /*
         * This formula is from the XLS-66 spec, section 3.2.4.2 (Total Loan
         * Value Calculation), specifically "totalInterestOutstanding = ..."
         */
        totalValueOutstanding - principalOutstanding,
        managementFeeRate,
        loanScale);

    auto const firstPaymentPrincipal = [&]() {
        // Compute the parts for the first payment. Ensure that the
        // principal payment will actually change the principal.
        auto const paymentComponents = computePaymentComponents(
            asset,
            loanScale,
            totalValueOutstanding,
            principalOutstanding,
            feeOwedToBroker,
            periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate);

        // The unrounded principal part needs to be large enough to affect the
        // principal. What to do if not is left to the caller
        return paymentComponents.rawPrincipal;
    }();

    return LoanProperties{
        .periodicPayment = periodicPayment,
        .totalValueOutstanding = totalValueOutstanding,
        .managementFeeOwedToBroker = feeOwedToBroker,
        .loanScale = loanScale,
        .firstPaymentPrincipal = firstPaymentPrincipal};
}

#if LOANCOMPLETE
template <AssetType A>
Number
loanPeriodicPayment(
    A const& asset,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining,
    std::int32_t scale)
{
    return roundPeriodicPayment(
        asset,
        detail::loanPeriodicPayment(
            principalOutstanding, periodicRate, paymentsRemaining),
        scale);
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
    TenthBips16 managementFeeRate,
    std::int32_t scale)
{
    return valueMinusFee(
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
    TenthBips16 managementFeeRate)
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
    std::uint32_t nextPaymentDueDate,
    std::int32_t const& scale)
{
    return roundToAsset(
        asset,
        detail::loanLatePaymentInterest(
            principalOutstanding,
            lateInterestRate,
            parentCloseTime,
            nextPaymentDueDate),
        scale);
}
#endif

template <AssetType A>
bool
isRounded(A const& asset, Number const& value, std::int32_t scale)
{
    return roundToAsset(asset, value, scale, Number::downward) ==
        roundToAsset(asset, value, scale, Number::upward);
}

template <AssetType A>
Expected<LoanPaymentParts, TER>
loanMakeFullPayment(
    A const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    bool const overpaymentAllowed,
    beast::Journal j)
{
    auto principalOutstandingProxy = loan->at(sfPrincipalOutstanding);
    auto paymentRemainingProxy = loan->at(sfPaymentRemaining);

    if (paymentRemainingProxy == 0 || principalOutstandingProxy == 0)
    {
        // Loan complete
        JLOG(j.warn()) << "Loan is already paid off.";
        return Unexpected(tecKILLED);
    }

    auto totalValueOutstandingProxy = loan->at(sfTotalValueOutstanding);
    auto managementFeeOutstandingProxy = loan->at(sfManagementFeeOutstanding);

    // Next payment due date must be set unless the loan is complete
    auto nextDueDateProxy = loan->at(~sfNextPaymentDueDate);
    if (!nextDueDateProxy)
    {
        JLOG(j.warn()) << "Loan next payment due date is not set.";
        return Unexpected(tecINTERNAL);
    }

    std::int32_t const loanScale = loan->at(sfLoanScale);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};

    Number const closePaymentFee =
        roundToAsset(asset, loan->at(sfClosePaymentFee), loanScale);
    TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};

    auto const periodicPayment = loan->at(sfPeriodicPayment);

    auto prevPaymentDateProxy = loan->at(sfPreviousPaymentDate);
    std::uint32_t const startDate = loan->at(sfStartDate);

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);
    //  Compute the normal periodic rate, payment, etc.
    //  We'll need it in the remaining calculations
    Number const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(
        interestRate == 0 || periodicRate > 0,
        "ripple::loanMakeFullPayment : valid rate");

    XRPL_ASSERT(
        *totalValueOutstandingProxy > 0,
        "ripple::loanMakeFullPayment : valid total value");

    view.update(loan);

    // -------------------------------------------------------------
    // full payment handling
    LoanState const roundedLoanState = calculateRoundedLoanState(
        totalValueOutstandingProxy,
        principalOutstandingProxy,
        managementFeeOutstandingProxy);

    if (auto const fullPaymentComponents = detail::computeFullPayment(
            asset,
            view,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            periodicPayment,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            startDate,
            paymentInterval,
            closeInterestRate,
            loanScale,
            roundedLoanState.interestDue,
            periodicRate,
            closePaymentFee,
            amount,
            managementFeeRate,
            j))
        return doPayment(
            *fullPaymentComponents,
            totalValueOutstandingProxy,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            nextDueDateProxy,
            paymentInterval);
    else if (fullPaymentComponents.error())
        // error() will be the TER returned if a payment is not made. It
        // will only evaluate to true if it's unsuccessful. Otherwise,
        // tesSUCCESS means nothing was done, so continue.
        return Unexpected(fullPaymentComponents.error());

    // LCOV_EXCL_START
    UNREACHABLE("ripple::loanMakeFullPayment : invalid result");
    return Unexpected(tecINTERNAL);
    // LCOV_EXCL_STOP
}

template <AssetType A>
Expected<LoanPaymentParts, TER>
loanMakePayment(
    A const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    bool const overpaymentAllowed,
    beast::Journal j)
{
    /*
     * This function is an implementation of the XLS-66 spec,
     * section 3.2.4.3 (Transaction Pseudo-code)
     */
    auto principalOutstandingProxy = loan->at(sfPrincipalOutstanding);
    auto paymentRemainingProxy = loan->at(sfPaymentRemaining);

    if (paymentRemainingProxy == 0 || principalOutstandingProxy == 0)
    {
        // Loan complete
        JLOG(j.warn()) << "Loan is already paid off.";
        return Unexpected(tecKILLED);
    }

    auto totalValueOutstandingProxy = loan->at(sfTotalValueOutstanding);
    auto managementFeeOutstandingProxy = loan->at(sfManagementFeeOutstanding);

    // Next payment due date must be set unless the loan is complete
    auto nextDueDateProxy = loan->at(~sfNextPaymentDueDate);
    if (!nextDueDateProxy)
    {
        JLOG(j.warn()) << "Loan next payment due date is not set.";
        return Unexpected(tecINTERNAL);
    }

    std::int32_t const loanScale = loan->at(sfLoanScale);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};
    TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
    TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};

    Number const serviceFee = loan->at(sfLoanServiceFee);
    Number const latePaymentFee = loan->at(sfLatePaymentFee);
    Number const closePaymentFee =
        roundToAsset(asset, loan->at(sfClosePaymentFee), loanScale);
    TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};

    auto const periodicPayment = loan->at(sfPeriodicPayment);

    auto prevPaymentDateProxy = loan->at(sfPreviousPaymentDate);
    std::uint32_t const startDate = loan->at(sfStartDate);

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);
    // Compute the normal periodic rate, payment, etc.
    // We'll need it in the remaining calculations
    Number const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(
        interestRate == 0 || periodicRate > 0,
        "ripple::loanMakePayment : valid rate");

    XRPL_ASSERT(
        *totalValueOutstandingProxy > 0,
        "ripple::loanMakePayment : valid total value");

    view.update(loan);

    detail::PaymentComponentsPlus const periodic{
        computePaymentComponents(
            asset,
            loanScale,
            totalValueOutstandingProxy,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            periodicPayment,
            periodicRate,
            paymentRemainingProxy,
            managementFeeRate),
        serviceFee};
    XRPL_ASSERT_PARTS(
        periodic.roundedPrincipal >= 0,
        "ripple::loanMakePayment",
        "regular payment valid principal");

    // -------------------------------------------------------------
    // late payment handling
    if (auto const latePaymentComponents = detail::computeLatePayment(
            asset,
            view,
            principalOutstandingProxy,
            *nextDueDateProxy,
            periodic,
            lateInterestRate,
            loanScale,
            latePaymentFee,
            amount,
            managementFeeRate,
            j))
    {
        return doPayment(
            *latePaymentComponents,
            totalValueOutstandingProxy,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            nextDueDateProxy,
            paymentInterval);
    }
    else if (latePaymentComponents.error())
        // error() will be the TER returned if a payment is not made. It will
        // only evaluate to true if it's unsuccessful. Otherwise, tesSUCCESS
        // means nothing was done, so continue.
        return Unexpected(latePaymentComponents.error());

    // -------------------------------------------------------------
    // regular periodic payment handling

    // if the payment is not late nor if it's a full payment, then it must
    // be a periodic one, with possible overpayments

    // This will keep a running total of what is actually paid, if the payment
    // is sufficient for a single payment
    Number totalPaid = periodic.totalDue;

    if (amount < totalPaid)
    {
        JLOG(j.warn()) << "Periodic loan payment amount is insufficient. Due: "
                       << totalPaid << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    LoanPaymentParts totalParts = detail::doPayment(
        periodic,
        totalValueOutstandingProxy,
        principalOutstandingProxy,
        managementFeeOutstandingProxy,
        paymentRemainingProxy,
        prevPaymentDateProxy,
        nextDueDateProxy,
        paymentInterval);

    std::size_t numPayments = 1;

    while (totalPaid < amount && paymentRemainingProxy > 0)
    {
        // Try to make more payments
        detail::PaymentComponentsPlus const nextPayment{
            computePaymentComponents(
                asset,
                loanScale,
                totalValueOutstandingProxy,
                principalOutstandingProxy,
                managementFeeOutstandingProxy,
                periodicPayment,
                periodicRate,
                paymentRemainingProxy,
                managementFeeRate),
            serviceFee};
        XRPL_ASSERT_PARTS(
            nextPayment.roundedPrincipal > 0,
            "ripple::loanMakePayment",
            "additional payment pays principal");
        XRPL_ASSERT(
            nextPayment.rawInterest <= periodic.rawInterest,
            "ripple::loanMakePayment : decreasing interest");
        XRPL_ASSERT(
            nextPayment.rawPrincipal >= periodic.rawPrincipal,
            "ripple::loanMakePayment : increasing principal");

        if (amount < totalPaid + nextPayment.totalDue)
            // We're done making payments.
            break;

        totalPaid += nextPayment.totalDue;
        totalParts += detail::doPayment(
            nextPayment,
            totalValueOutstandingProxy,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            nextDueDateProxy,
            paymentInterval);
        ++numPayments;

        if (nextPayment.specialCase == SpecialCase::final)
            break;
    }

    XRPL_ASSERT_PARTS(
        totalParts.principalPaid + totalParts.interestPaid +
                totalParts.feePaid ==
            totalPaid,
        "ripple::loanMakePayment",
        "payment parts add up");
    XRPL_ASSERT_PARTS(
        totalParts.valueChange == 0,
        "ripple::loanMakePayment",
        "no value change");

    // -------------------------------------------------------------
    // overpayment handling
    if (overpaymentAllowed && loan->isFlag(lsfLoanOverpayment) &&
        paymentRemainingProxy > 0 && nextDueDateProxy && totalPaid < amount)
    {
        TenthBips32 const overpaymentInterestRate{
            loan->at(sfOverpaymentInterestRate)};
        TenthBips32 const overpaymentFeeRate{loan->at(sfOverpaymentFee)};

        Number const overpayment = amount - totalPaid;
        XRPL_ASSERT(
            overpayment > 0 && isRounded(asset, overpayment, loanScale),
            "ripple::loanMakePayment : valid overpayment amount");

        Number const fee = roundToAsset(
            asset,
            tenthBipsOfValue(overpayment, overpaymentFeeRate),
            loanScale);

        Number const payment = overpayment - fee;

        auto const [rawOverpaymentInterest, rawOverpaymentManagementFee] =
            [&]() {
                Number const interest =
                    tenthBipsOfValue(payment, overpaymentInterestRate);
                return detail::computeInterestAndFeeParts(
                    interest, managementFeeRate);
            }();
        auto const
            [roundedOverpaymentInterest, roundedOverpaymentManagementFee] =
                [&]() {
                    Number const interest =
                        roundToAsset(asset, rawOverpaymentInterest, loanScale);
                    return detail::computeInterestAndFeeParts(
                        asset, interest, managementFeeRate, loanScale);
                }();

        detail::PaymentComponentsPlus overpaymentComponents{
            PaymentComponents{
                .rawInterest = rawOverpaymentInterest,
                .rawPrincipal = payment - rawOverpaymentInterest,
                .rawManagementFee = 0,
                .roundedInterest = roundedOverpaymentInterest,
                .roundedPrincipal = payment - roundedOverpaymentInterest,
                .roundedManagementFee = 0,
                .specialCase = SpecialCase::extra},
            fee,
            roundedOverpaymentInterest};

        // Don't process an overpayment if the whole amount (or more!)
        // gets eaten by fees and interest.
        if (overpaymentComponents.rawPrincipal > 0 &&
            overpaymentComponents.roundedPrincipal > 0)
        {
            XRPL_ASSERT_PARTS(
                overpaymentComponents.valueChange >= beast::zero,
                "ripple::loanMakePayment",
                "overpayment penalty did not reduce value of loan");
            // Can't just use `periodicPayment` here, because it might change
            auto periodicPaymentProxy = loan->at(sfPeriodicPayment);
            if (auto const overResult = detail::computeOverpayment(
                    asset,
                    loanScale,
                    overpaymentComponents,
                    totalValueOutstandingProxy,
                    principalOutstandingProxy,
                    managementFeeOutstandingProxy,
                    periodicPaymentProxy,
                    interestRate,
                    paymentInterval,
                    periodicRate,
                    paymentRemainingProxy,
                    prevPaymentDateProxy,
                    nextDueDateProxy,
                    managementFeeRate,
                    j))
                totalParts += *overResult;
            else if (overResult.error())
                // error() will be the TER returned if a payment is not made. It
                // will only evaluate to true if it's unsuccessful. Otherwise,
                // tesSUCCESS means nothing was done, so continue.
                return Unexpected(overResult.error());
        }
    }

    // Check the final results are rounded, to double-check that the
    // intermediate steps were rounded.
    XRPL_ASSERT(
        isRounded(asset, totalParts.principalPaid, loanScale),
        "ripple::loanMakePayment : total principal paid rounded");
    XRPL_ASSERT(
        isRounded(asset, totalParts.interestPaid, loanScale),
        "ripple::loanMakePayment : total interest paid rounded");
    XRPL_ASSERT(
        isRounded(asset, totalParts.valueChange, loanScale),
        "ripple::loanMakePayment : loan value change rounded");
    XRPL_ASSERT(
        isRounded(asset, totalParts.feePaid, loanScale),
        "ripple::loanMakePayment : fee paid rounded");
    return totalParts;
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_LENDINGHELPERS_H_INCLUDED
