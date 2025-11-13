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

LoanPaymentParts&
LoanPaymentParts::operator+=(LoanPaymentParts const& other)
{
    XRPL_ASSERT(

        other.principalPaid >= beast::zero,
        "ripple::LoanPaymentParts::operator+= : other principal "
        "non-negative");
    XRPL_ASSERT(
        other.interestPaid >= beast::zero,
        "ripple::LoanPaymentParts::operator+= : other interest paid "
        "non-negative");
    XRPL_ASSERT(
        other.feePaid >= beast::zero,
        "ripple::LoanPaymentParts::operator+= : other fee paid "
        "non-negative");

    principalPaid += other.principalPaid;
    interestPaid += other.interestPaid;
    valueChange += other.valueChange;
    feePaid += other.feePaid;
    return *this;
}

bool
LoanPaymentParts::operator==(LoanPaymentParts const& other) const
{
    return principalPaid == other.principalPaid &&
        interestPaid == other.interestPaid &&
        valueChange == other.valueChange && feePaid == other.feePaid;
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

bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale)
{
    return roundToAsset(asset, value, scale, Number::downward) ==
        roundToAsset(asset, value, scale, Number::upward);
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
    // periodicRate should never be zero in this function, but if it is,
    // then 1/paymentRemaining is the most accurate factor that avoids
    // divide by 0.
    if (periodicRate == beast::zero)
        return Number{1} / paymentsRemaining;

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

    // If the payment is not late by any amount of time, then there's no late
    // interest
    if (parentCloseTime.time_since_epoch().count() <= nextPaymentDueDate)
        return 0;

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

    // If the loan has been paid ahead, then "lastPaymentDate" is in the future,
    // and no interest has accrued.
    if (parentCloseTime.time_since_epoch().count() <= lastPaymentDate)
        return 0;

    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    return principalOutstanding * periodicRate * secondsSinceLastPayment /
        paymentInterval;
}

struct PaymentComponentsPlus : public PaymentComponents
{
    // untrackedManagementFeeDelta includes any fees that go directly to the
    // Broker, such as late fees. This value may be negative, though the final
    // value returned in LoanPaymentParts.feePaid will never be negative.
    Number untrackedManagementFee;
    // untrackedInterest includes any fees that go directly to the Vault, such
    // as late payment penalty interest. This value may be negative, though the
    // final value returned in LoanPaymentParts.interestPaid will never be
    // negative.
    Number untrackedInterest;
    Number totalDue;

    PaymentComponentsPlus(
        PaymentComponents const& p,
        Number f,
        Number v = numZero)
        : PaymentComponents(p)
        , untrackedManagementFee(f)
        , untrackedInterest(v)
        , totalDue(
              trackedValueDelta + untrackedInterest + untrackedManagementFee)
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

    if (payment.specialCase == PaymentSpecialCase::final)
    {
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy == payment.trackedPrincipalDelta,
            "ripple::detail::doPayment",
            "Full principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy == payment.trackedValueDelta,
            "ripple::detail::doPayment",
            "Full value payment");
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy == payment.trackedManagementFeeDelta,
            "ripple::detail::doPayment",
            "Full management fee payment");

        paymentRemainingProxy = 0;

        prevPaymentDateProxy = *nextDueDateProxy;
        // Zero out the next due date. Since it's default, it'll be removed from
        // the object.
        nextDueDateProxy = 0;

        // Always zero out the the tracked values on a final payment
        principalOutstandingProxy = 0;
        totalValueOutstandingProxy = 0;
        managementFeeOutstandingProxy = 0;
    }
    else
    {
        if (payment.specialCase != PaymentSpecialCase::extra)
        {
            paymentRemainingProxy -= 1;

            prevPaymentDateProxy = nextDueDateProxy;
            nextDueDateProxy += paymentInterval;
        }
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy > payment.trackedPrincipalDelta,
            "ripple::detail::doPayment",
            "Partial principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy > payment.trackedValueDelta,
            "ripple::detail::doPayment",
            "Partial value payment");
        // Management fees are expected to be relatively small, and could get to
        // zero before the loan is paid off
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy >= payment.trackedManagementFeeDelta,
            "ripple::detail::doPayment",
            "Valid management fee");

        principalOutstandingProxy -= payment.trackedPrincipalDelta;
        totalValueOutstandingProxy -= payment.trackedValueDelta;
        managementFeeOutstandingProxy -= payment.trackedManagementFeeDelta;
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
        .principalPaid = payment.trackedPrincipalDelta,
        // Now that the Loan object has been updated, the tracked interest
        // (computed here) and untracked interest can be combined.
        .interestPaid =
            payment.trackedInterestPart() + payment.untrackedInterest,
        .valueChange = payment.untrackedInterest,
        // Now that the Loan object has been updated, the fee parts can be
        // combined
        .feePaid =
            payment.trackedManagementFeeDelta + payment.untrackedManagementFee};
}

// This function mainly exists to guarantee isolation of the "sandbox"
// variables from the real / proxy variables that will affect actual
// ledger data in the caller.

Expected<LoanPaymentParts, TER>
tryOverpayment(
    Asset const& asset,
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

    auto const newRawPrincipal = std::max(
        raw.principalOutstanding - overpaymentComponents.trackedPrincipalDelta,
        Number{0});

    auto newLoanProperties = computeLoanProperties(
        asset,
        newRawPrincipal,
        interestRate,
        paymentInterval,
        paymentRemaining,
        managementFeeRate,
        loanScale);

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
        newRounded.interestOutstanding() - rounded.interestOutstanding();
    XRPL_ASSERT_PARTS(
        valueChange <= beast::zero,
        "ripple::detail::tryOverpayment",
        "principal overpayment did not increase value of loan");

    return LoanPaymentParts{
        .principalPaid =
            rounded.principalOutstanding - newRounded.principalOutstanding,
        .interestPaid = rounded.interestDue - newRounded.interestDue,
        .valueChange = valueChange + overpaymentComponents.untrackedInterest,
        .feePaid = rounded.managementFeeDue - newRounded.managementFeeDue +
            overpaymentComponents.untrackedManagementFee};
}

template <class NumberProxy>
Expected<LoanPaymentParts, TER>
doOverpayment(
    Asset const& asset,
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

    // We haven't updated the proxies yet, so they still have the original
    // values. Use those to do some checks.
    XRPL_ASSERT_PARTS(
        overpaymentComponents.trackedPrincipalDelta ==
            principalOutstandingProxy - principalOutstanding,
        "ripple::detail::doOverpayment",
        "principal change agrees");

    XRPL_ASSERT_PARTS(
        overpaymentComponents.trackedManagementFeeDelta ==
            managementFeeOutstandingProxy - managementFeeOutstanding,
        "ripple::detail::doOverpayment",
        "no fee change");

    XRPL_ASSERT_PARTS(
        overpaymentComponents.untrackedInterest ==
            totalValueOutstandingProxy - totalValueOutstanding -
                overpaymentComponents.trackedPrincipalDelta,
        "ripple::detail::doOverpayment",
        "value change agrees");

    XRPL_ASSERT_PARTS(
        overpaymentComponents.trackedPrincipalDelta ==
            loanPaymentParts.principalPaid,
        "ripple::detail::doOverpayment",
        "principal payment matches");

    XRPL_ASSERT_PARTS(
        loanPaymentParts.feePaid ==
            overpaymentComponents.untrackedManagementFee +
                overpaymentComponents.trackedManagementFeeDelta,
        "ripple::detail::doOverpayment",
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
    Asset const& asset,
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

Expected<PaymentComponentsPlus, TER>
computeLatePayment(
    Asset const& asset,
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
        return Unexpected(tecTOO_SOON);

    // the payment is late
    // Late payment interest is only the part of the interest that comes
    // from being late, as computed by 3.2.4.1.2.
    auto const latePaymentInterest = loanLatePaymentInterest(
        principalOutstanding,
        lateInterestRate,
        view.parentCloseTime(),
        nextDueDate);

    auto const [roundedLateInterest, roundedLateManagementFee] = [&]() {
        auto const interest =
            roundToAsset(asset, latePaymentInterest, loanScale);
        return computeInterestAndFeeParts(
            asset, interest, managementFeeRate, loanScale);
    }();

    XRPL_ASSERT(
        roundedLateInterest >= 0,
        "ripple::detail::computeLatePayment : valid late interest");
    XRPL_ASSERT_PARTS(
        periodic.specialCase != PaymentSpecialCase::extra,
        "ripple::detail::computeLatePayment",
        "no extra parts to this payment");
    // Copy the periodic payment values, and add on the late interest.
    // This preserves all the other fields without having to enumerate them.
    PaymentComponentsPlus const late = [&]() {
        auto inner = periodic;

        return PaymentComponentsPlus{
            inner,
            // A late payment pays both the normal fee, and the extra fees
            periodic.untrackedManagementFee + latePaymentFee +
                roundedLateManagementFee,
            // A late payment increases the value of the loan by the difference
            // between periodic and late payment interest
            periodic.untrackedInterest + roundedLateInterest};
    }();

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

Expected<PaymentComponentsPlus, TER>
computeFullPayment(
    Asset const& asset,
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
            .trackedValueDelta = principalOutstanding +
                totalInterestOutstanding + managementFeeOutstanding,
            .trackedPrincipalDelta = principalOutstanding,
            // to make the accounting work later, the tracked part of the fee
            // must be paid in full
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::final},
        // A full payment pays the single close payment fee, plus the computed
        // management fee part of the interest portion, but for tracking, the
        // outstanding part is removed. That could make this value negative, but
        // that's ok, because it's not used until it's recombined with
        // roundedManagementFee.
        closePaymentFee + roundedFullManagementFee - managementFeeOutstanding,
        // A full payment changes the value of the loan by the difference
        // between expected outstanding interest return and the actual interest
        // paid. This value can be positive (increasing the value) or negative
        // (decreasing the value).
        roundedFullInterest - totalInterestOutstanding};

    XRPL_ASSERT_PARTS(
        isRounded(asset, full.totalDue, loanScale),
        "ripple::detail::computeFullPayment",
        "total due is rounded");

    JLOG(j.trace()) << "computeFullPayment result: periodicPayment: "
                    << periodicPayment << ", periodicRate: " << periodicRate
                    << ", paymentRemaining: " << paymentRemaining
                    << ", rawPrincipalOutstanding: " << rawPrincipalOutstanding
                    << ", fullPaymentInterest: " << fullPaymentInterest
                    << ", roundedFullInterest: " << roundedFullInterest
                    << ", roundedFullManagementFee: "
                    << roundedFullManagementFee
                    << ", untrackedInterest: " << full.untrackedInterest;

    if (amount < full.totalDue)
        // If the payment is less than the full payment amount, it's not
        // sufficient to be a full payment.
        return Unexpected(tecINSUFFICIENT_PAYMENT);

    return full;
}

Number
PaymentComponents::trackedInterestPart() const
{
    return trackedValueDelta -
        (trackedPrincipalDelta + trackedManagementFeeDelta);
}

void
LoanDeltas::nonNegative()
{
    if (principalDelta < beast::zero)
        principalDelta = numZero;
    if (interestDueDelta < beast::zero)
        interestDueDelta = numZero;
    if (managementFeeDueDelta < beast::zero)
        managementFeeDueDelta = numZero;
}

PaymentComponents
computePaymentComponents(
    Asset const& asset,
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
    XRPL_ASSERT_PARTS(
        paymentRemaining > 0,
        "ripple::detail::computePaymentComponents",
        "some payments remaining");
    auto const roundedPeriodicPayment =
        roundPeriodicPayment(asset, periodicPayment, scale);

    LoanState const trueTarget = calculateRawLoanState(
        periodicPayment, periodicRate, paymentRemaining - 1, managementFeeRate);
    LoanState const roundedTarget = LoanState{
        .valueOutstanding =
            roundToAsset(asset, trueTarget.valueOutstanding, scale),
        .principalOutstanding =
            roundToAsset(asset, trueTarget.principalOutstanding, scale),
        .interestDue = roundToAsset(asset, trueTarget.interestDue, scale),
        .managementFeeDue =
            roundToAsset(asset, trueTarget.managementFeeDue, scale)};
    LoanState const currentLedgerState = calculateRoundedLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    LoanDeltas deltas = currentLedgerState - roundedTarget;
    deltas.nonNegative();

    // Adjust the deltas if necessary for data integrity
    XRPL_ASSERT_PARTS(
        deltas.principalDelta <= currentLedgerState.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "principal delta not greater than outstanding");

    deltas.principalDelta = std::min(
        deltas.principalDelta, currentLedgerState.principalOutstanding);

    XRPL_ASSERT_PARTS(
        deltas.interestDueDelta <= currentLedgerState.interestDue,
        "ripple::detail::computePaymentComponents",
        "interest due delta not greater than outstanding");

    deltas.interestDueDelta = std::min(
        {deltas.interestDueDelta,
         std::max(numZero, roundedPeriodicPayment - deltas.principalDelta),
         currentLedgerState.interestDue});

    XRPL_ASSERT_PARTS(
        deltas.managementFeeDueDelta <= currentLedgerState.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "management fee due delta not greater than outstanding");

    deltas.managementFeeDueDelta = std::min(
        {deltas.managementFeeDueDelta,
         roundedPeriodicPayment -
             (deltas.principalDelta + deltas.interestDueDelta),
         currentLedgerState.managementFeeDue});

    if (paymentRemaining == 1 ||
        totalValueOutstanding <= roundedPeriodicPayment)
    {
        // If there's only one payment left, we need to pay off each of the loan
        // parts.

        XRPL_ASSERT_PARTS(
            deltas.valueDelta() <= totalValueOutstanding,
            "ripple::detail::computePaymentComponents",
            "last payment total value agrees");
        XRPL_ASSERT_PARTS(
            deltas.principalDelta <= principalOutstanding,
            "ripple::detail::computePaymentComponents",
            "last payment principal agrees");
        XRPL_ASSERT_PARTS(
            deltas.managementFeeDueDelta <= managementFeeOutstanding,
            "ripple::detail::computePaymentComponents",
            "last payment management fee agrees");

        // Pay everything off
        return PaymentComponents{
            .trackedValueDelta = totalValueOutstanding,
            .trackedPrincipalDelta = principalOutstanding,
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::final};
    }

    // The shortage must never be negative, which indicates that the parts are
    // trying to take more than the whole payment. The excess can be positive,
    // which indicates that we're not going to take the whole payment amount,
    // but if so, it must be small.
    auto takeFrom = [](Number& component, Number& excess) {
        if (excess > beast::zero)
        {
            // Take as much of the excess as we can out of the provided part and
            // the total
            auto part = std::min(component, excess);
            component -= part;
            excess -= part;
        }
        // If the excess goes negative, we took too much, which should be
        // impossible
        XRPL_ASSERT_PARTS(
            excess >= beast::zero,
            "ripple::detail::computePaymentComponents",
            "excess non-negative");
    };
    auto addressExcess = [&takeFrom](LoanDeltas& deltas, Number& excess) {
        // This order is based on where errors are the least problematic
        takeFrom(deltas.interestDueDelta, excess);
        takeFrom(deltas.managementFeeDueDelta, excess);
        takeFrom(deltas.principalDelta, excess);
    };
    Number totalOverpayment =
        deltas.valueDelta() - currentLedgerState.valueOutstanding;
    if (totalOverpayment > beast::zero)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "ripple::detail::computePaymentComponents : payment exceeded loan "
            "state");
        addressExcess(deltas, totalOverpayment);
        // LCOV_EXCL_STOP
    }

    // Make sure the parts don't add up to too much
    Number shortage = roundedPeriodicPayment - deltas.valueDelta();

    XRPL_ASSERT_PARTS(
        isRounded(asset, shortage, scale),
        "ripple::detail::computePaymentComponents",
        "shortage is rounded");

    if (shortage < beast::zero)
    {
        Number excess = -shortage;

        addressExcess(deltas, excess);

        shortage = -excess;
    }
    // The shortage should never be negative, which indicates that the
    // parts are trying to take more than the whole payment. The
    // shortage may be positive, which indicates that we're not going to
    // take the whole payment amount.
    XRPL_ASSERT_PARTS(
        shortage >= beast::zero,
        "ripple::detail::computePaymentComponents",
        "no shortage or excess");

    XRPL_ASSERT_PARTS(
        deltas.valueDelta() ==
            deltas.principalDelta + deltas.interestDueDelta +
                deltas.managementFeeDueDelta,
        "ripple::detail::computePaymentComponents",
        "total value adds up");

    XRPL_ASSERT_PARTS(
        deltas.principalDelta >= beast::zero &&
            deltas.principalDelta <= currentLedgerState.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "valid principal result");
    XRPL_ASSERT_PARTS(
        deltas.interestDueDelta >= beast::zero &&
            deltas.interestDueDelta <= currentLedgerState.interestDue,
        "ripple::detail::computePaymentComponents",
        "valid interest result");
    XRPL_ASSERT_PARTS(
        deltas.managementFeeDueDelta >= beast::zero &&
            deltas.managementFeeDueDelta <= currentLedgerState.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "valid fee result");

    XRPL_ASSERT_PARTS(
        deltas.principalDelta + deltas.interestDueDelta +
                deltas.managementFeeDueDelta >
            beast::zero,
        "ripple::detail::computePaymentComponents",
        "payment parts add to payment");

    return PaymentComponents{
        // As a final safety check, ensure the value is non-negative, and won't
        // make the corresponding item negative
        .trackedValueDelta = std::clamp(
            deltas.valueDelta(), numZero, currentLedgerState.valueOutstanding),
        .trackedPrincipalDelta = std::clamp(
            deltas.principalDelta,
            numZero,
            currentLedgerState.principalOutstanding),
        .trackedManagementFeeDelta = std::clamp(
            deltas.managementFeeDueDelta,
            numZero,
            currentLedgerState.managementFeeDue),
    };
}

PaymentComponentsPlus
computeOverpaymentComponents(
    Asset const& asset,
    int32_t const loanScale,
    Number const& overpayment,
    TenthBips32 const overpaymentInterestRate,
    TenthBips32 const overpaymentFeeRate,
    TenthBips16 const managementFeeRate)
{
    XRPL_ASSERT(
        overpayment > 0 && isRounded(asset, overpayment, loanScale),
        "ripple::detail::computeOverpaymentComponents : valid overpayment "
        "amount");

    Number const fee = roundToAsset(
        asset, tenthBipsOfValue(overpayment, overpaymentFeeRate), loanScale);

    Number const payment = overpayment - fee;

    auto const [rawOverpaymentInterest, rawOverpaymentManagementFee] = [&]() {
        Number const interest =
            tenthBipsOfValue(payment, overpaymentInterestRate);
        return detail::computeInterestAndFeeParts(interest, managementFeeRate);
    }();
    auto const [roundedOverpaymentInterest, roundedOverpaymentManagementFee] =
        [&]() {
            Number const interest =
                roundToAsset(asset, rawOverpaymentInterest, loanScale);
            return detail::computeInterestAndFeeParts(
                asset, interest, managementFeeRate, loanScale);
        }();

    return detail::PaymentComponentsPlus{
        detail::PaymentComponents{
            .trackedValueDelta = payment,
            .trackedPrincipalDelta = payment - roundedOverpaymentInterest -
                roundedOverpaymentManagementFee,
            .trackedManagementFeeDelta = roundedOverpaymentManagementFee,
            .specialCase = detail::PaymentSpecialCase::extra},
        fee,
        roundedOverpaymentInterest};
}

}  // namespace detail

detail::LoanDeltas
operator-(LoanState const& lhs, LoanState const& rhs)
{
    detail::LoanDeltas result{
        .principalDelta = lhs.principalOutstanding - rhs.principalOutstanding,
        .interestDueDelta = lhs.interestDue - rhs.interestDue,
        .managementFeeDueDelta = lhs.managementFeeDue - rhs.managementFeeDue,
    };

    return result;
}

LoanState
operator-(LoanState const& lhs, detail::LoanDeltas const& rhs)
{
    LoanState result{
        .valueOutstanding = lhs.valueOutstanding - rhs.valueDelta(),
        .principalOutstanding = lhs.principalOutstanding - rhs.principalDelta,
        .interestDue = lhs.interestDue - rhs.interestDueDelta,
        .managementFeeDue = lhs.managementFeeDue - rhs.managementFeeDueDelta,
    };

    return result;
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
    TenthBips32 const managementFeeRate)
{
    if (paymentRemaining == 0)
    {
        return LoanState{
            .valueOutstanding = 0,
            .principalOutstanding = 0,
            .interestDue = 0,
            .managementFeeDue = 0};
    }
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
        .interestDue = rawInterestOutstanding - rawManagementFeeOutstanding,
        .managementFeeDue = rawManagementFeeOutstanding};
};

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate)
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
    return {
        .valueOutstanding = totalValueOutstanding,
        .principalOutstanding = principalOutstanding,
        .interestDue = totalValueOutstanding - principalOutstanding -
            managementFeeOutstanding,
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

Number
computeFee(
    Asset const& asset,
    Number const& value,
    TenthBips32 managementFeeRate,
    std::int32_t scale)
{
    return roundToAsset(
        asset,
        tenthBipsOfValue(value, managementFeeRate),
        scale,
        Number::downward);
}

Number
valueMinusFee(
    Asset const& asset,
    Number const& value,
    TenthBips16 managementFeeRate,
    std::int32_t scale)
{
    return value - computeFee(asset, value, managementFeeRate, scale);
}

LoanProperties
computeLoanProperties(
    Asset const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate,
    std::int32_t minimumScale)
{
    auto const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(
        interestRate == 0 || periodicRate > 0,
        "ripple::computeLoanProperties : valid rate");

    auto const periodicPayment = detail::loanPeriodicPayment(
        principalOutstanding, periodicRate, paymentsRemaining);
    auto const [totalValueOutstanding, loanScale] = [&]() {
        NumberRoundModeGuard mg(Number::to_nearest);
        // Use STAmount's internal rounding instead of roundToAsset, because
        // we're going to use this result to determine the scale for all the
        // other rounding.
        STAmount amount{
            asset,
            /*
             * This formula is from the XLS-66 spec, section 3.2.4.2 (Total
             * Loan Value Calculation), specifically "totalValueOutstanding
             * = ..."
             */
            periodicPayment * paymentsRemaining};

        // Base the loan scale on the total value, since that's going to be the
        // biggest number involved (barring unusual parameters for late, full,
        // or over payments)
        auto const loanScale = std::max(minimumScale, amount.exponent());
        XRPL_ASSERT_PARTS(
            (amount.integral() && loanScale == 0) ||
                (!amount.integral() &&
                 loanScale >= static_cast<Number>(amount).exponent()),
            "ripple::computeLoanProperties",
            "loanScale value fits expectations");

        // We may need to truncate the total value because of the minimum scale
        amount = roundToAsset(asset, amount, loanScale, Number::to_nearest);

        return std::make_pair(amount, loanScale);
    }();

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
        auto const startingState = calculateRawLoanState(
            periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate);
        auto const firstPaymentState = calculateRawLoanState(
            periodicPayment,
            periodicRate,
            paymentsRemaining - 1,
            managementFeeRate);

        // The unrounded principal part needs to be large enough to affect the
        // principal. What to do if not is left to the caller
        return startingState.principalOutstanding -
            firstPaymentState.principalOutstanding;
    }();

    return LoanProperties{
        .periodicPayment = periodicPayment,
        .totalValueOutstanding = totalValueOutstanding,
        .managementFeeOwedToBroker = feeOwedToBroker,
        .loanScale = loanScale,
        .firstPaymentPrincipal = firstPaymentPrincipal};
}

Expected<LoanPaymentParts, TER>
loanMakePayment(
    Asset const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    LoanPaymentType const paymentType,
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
        // This is already checked in LoanPay::preclaim()
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Loan is already paid off.";
        return Unexpected(tecKILLED);
        // LCOV_EXCL_STOP
    }

    auto totalValueOutstandingProxy = loan->at(sfTotalValueOutstanding);
    auto managementFeeOutstandingProxy = loan->at(sfManagementFeeOutstanding);

    // Next payment due date must be set unless the loan is complete
    auto nextDueDateProxy = loan->at(sfNextPaymentDueDate);
    if (*nextDueDateProxy == 0)
    {
        JLOG(j.warn()) << "Loan next payment due date is not set.";
        return Unexpected(tecINTERNAL);
    }

    std::int32_t const loanScale = loan->at(sfLoanScale);

    TenthBips32 const interestRate{loan->at(sfInterestRate)};

    Number const serviceFee = loan->at(sfLoanServiceFee);
    TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};

    Number const periodicPayment = loan->at(sfPeriodicPayment);

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

    // -------------------------------------------------------------
    // A late payment not flagged as late overrides all other options.
    if (paymentType != LoanPaymentType::late &&
        hasExpired(view, nextDueDateProxy))
    {
        // If the payment is late, and the late flag was not set, it's not valid
        JLOG(j.warn())
            << "Loan payment is overdue. Use the tfLoanLatePayment transaction "
               "flag to make a late payment. Loan was created on "
            << startDate << ", prev payment due date is "
            << prevPaymentDateProxy << ", next payment due date is "
            << nextDueDateProxy << ", ledger time is "
            << view.parentCloseTime().time_since_epoch().count();
        return Unexpected(tecEXPIRED);
    }

    // -------------------------------------------------------------
    // full payment handling
    if (paymentType == LoanPaymentType::full)
    {
        TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};
        Number const closePaymentFee =
            roundToAsset(asset, loan->at(sfClosePaymentFee), loanScale);

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
        {
            return doPayment(
                *fullPaymentComponents,
                totalValueOutstandingProxy,
                principalOutstandingProxy,
                managementFeeOutstandingProxy,
                paymentRemainingProxy,
                prevPaymentDateProxy,
                nextDueDateProxy,
                paymentInterval);
        }
        else if (fullPaymentComponents.error())
            // error() will be the TER returned if a payment is not made. It
            // will only evaluate to true if it's unsuccessful. Otherwise,
            // tesSUCCESS means nothing was done, so continue.
            return Unexpected(fullPaymentComponents.error());

        // LCOV_EXCL_START
        UNREACHABLE("ripple::loanMakePayment : invalid full payment result");
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    // -------------------------------------------------------------
    // compute the periodic payment info that will be needed whether the payment
    // is late or regular
    detail::PaymentComponentsPlus periodic{
        detail::computePaymentComponents(
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
        periodic.trackedPrincipalDelta >= 0,
        "ripple::loanMakePayment",
        "regular payment valid principal");

    // -------------------------------------------------------------
    // late payment handling
    if (paymentType == LoanPaymentType::late)
    {
        TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
        Number const latePaymentFee = loan->at(sfLatePaymentFee);

        if (auto const latePaymentComponents = detail::computeLatePayment(
                asset,
                view,
                principalOutstandingProxy,
                nextDueDateProxy,
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
        {
            // error() will be the TER returned if a payment is not made. It
            // will only evaluate to true if it's unsuccessful.
            return Unexpected(latePaymentComponents.error());
        }

        // LCOV_EXCL_START
        UNREACHABLE("ripple::loanMakePayment : invalid late payment result");
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    // -------------------------------------------------------------
    // regular periodic payment handling

    XRPL_ASSERT_PARTS(
        paymentType == LoanPaymentType::regular ||
            paymentType == LoanPaymentType::overpayment,
        "ripple::loanMakePayment",
        "regular payment type");

    // This will keep a running total of what is actually paid, if the payment
    // is sufficient for any payment
    LoanPaymentParts totalParts;
    Number totalPaid;
    std::size_t numPayments = 0;

    while (amount >= totalPaid + periodic.totalDue &&
           paymentRemainingProxy > 0 &&
           numPayments < loanMaximumPaymentsPerTransaction)
    {
        // Try to make more payments
        XRPL_ASSERT_PARTS(
            periodic.trackedPrincipalDelta >= 0,
            "ripple::loanMakePayment",
            "payment pays non-negative principal");

        totalPaid += periodic.totalDue;
        totalParts += detail::doPayment(
            periodic,
            totalValueOutstandingProxy,
            principalOutstandingProxy,
            managementFeeOutstandingProxy,
            paymentRemainingProxy,
            prevPaymentDateProxy,
            nextDueDateProxy,
            paymentInterval);
        ++numPayments;

        XRPL_ASSERT_PARTS(
            (periodic.specialCase == detail::PaymentSpecialCase::final) ==
                (paymentRemainingProxy == 0),
            "ripple::loanMakePayment",
            "final payment is the final payment");

        // Don't compute the next payment if this was the last payment
        if (periodic.specialCase == detail::PaymentSpecialCase::final)
            break;

        periodic = detail::PaymentComponentsPlus{
            detail::computePaymentComponents(
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
    }

    if (numPayments == 0)
    {
        JLOG(j.warn()) << "Regular loan payment amount is insufficient. Due: "
                       << periodic.totalDue << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
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
    if (paymentType == LoanPaymentType::overpayment &&
        loan->isFlag(lsfLoanOverpayment) && paymentRemainingProxy > 0 &&
        totalPaid < amount && numPayments < loanMaximumPaymentsPerTransaction)
    {
        TenthBips32 const overpaymentInterestRate{
            loan->at(sfOverpaymentInterestRate)};
        TenthBips32 const overpaymentFeeRate{loan->at(sfOverpaymentFee)};

        // It shouldn't be possible for the overpayment to be greater than
        // totalValueOutstanding, because that would have been processed as
        // another normal payment. But cap it just in case.
        Number const overpayment =
            std::min(amount - totalPaid, *totalValueOutstandingProxy);

        detail::PaymentComponentsPlus const overpaymentComponents =
            detail::computeOverpaymentComponents(
                asset,
                loanScale,
                overpayment,
                overpaymentInterestRate,
                overpaymentFeeRate,
                managementFeeRate);

        // Don't process an overpayment if the whole amount (or more!)
        // gets eaten by fees and interest.
        if (overpaymentComponents.trackedPrincipalDelta > 0)
        {
            XRPL_ASSERT_PARTS(
                overpaymentComponents.untrackedInterest >= beast::zero,
                "ripple::loanMakePayment",
                "overpayment penalty did not reduce value of loan");
            // Can't just use `periodicPayment` here, because it might change
            auto periodicPaymentProxy = loan->at(sfPeriodicPayment);
            if (auto const overResult = detail::doOverpayment(
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
        isRounded(asset, totalParts.principalPaid, loanScale) &&
            totalParts.principalPaid >= beast::zero,
        "ripple::loanMakePayment : total principal paid is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.interestPaid, loanScale) &&
            totalParts.interestPaid >= beast::zero,
        "ripple::loanMakePayment : total interest paid is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.valueChange, loanScale),
        "ripple::loanMakePayment : loan value change is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.feePaid, loanScale) &&
            totalParts.feePaid >= beast::zero,
        "ripple::loanMakePayment : fee paid is valid");
    return totalParts;
}
}  // namespace ripple
