#include <xrpld/app/misc/LendingHelpers.h>
// DO NOT REMOVE forces header file include to sort first
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

/* Converts annualized interest rate to per-payment-period rate.
 * The rate is prorated based on the payment interval in seconds.
 *
 * Equation (1) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    // Need floating point math, since we're dividing by a large number
    return tenthBipsOfValue(Number(paymentInterval), interestRate) /
        secondsInYear;
}

/* Checks if a value is already rounded to the specified scale.
 * Returns true if rounding down and rounding up produce the same result,
 * indicating no further precision exists beyond the scale.
 */
bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale)
{
    return roundToAsset(asset, value, scale, Number::downward) ==
        roundToAsset(asset, value, scale, Number::upward);
}

namespace detail {

void
LoanStateDeltas::nonNegative()
{
    if (principal < beast::zero)
        principal = numZero;
    if (interest < beast::zero)
        interest = numZero;
    if (managementFee < beast::zero)
        managementFee = numZero;
}

/* Computes (1 + periodicRate)^paymentsRemaining for amortization calculations.
 *
 * Equation (5) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
computeRaisedRate(Number const& periodicRate, std::uint32_t paymentsRemaining)
{
    return power(1 + periodicRate, paymentsRemaining);
}

/* Computes the payment factor used in standard amortization formulas.
 * This factor converts principal to periodic payment amount.
 *
 * Equation (6) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
computePaymentFactor(
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    // For zero interest, payment factor is simply 1/paymentsRemaining
    if (periodicRate == beast::zero)
        return Number{1} / paymentsRemaining;

    Number const raisedRate =
        computeRaisedRate(periodicRate, paymentsRemaining);

    return (periodicRate * raisedRate) / (raisedRate - 1);
}

/* Calculates the periodic payment amount using standard amortization formula.
 * For interest-free loans, returns principal divided equally across payments.
 *
 * Equation (7) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanPeriodicPayment(
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;

    // Interest-free loans: equal principal payments
    if (periodicRate == beast::zero)
        return principalOutstanding / paymentsRemaining;

    return principalOutstanding *
        computePaymentFactor(periodicRate, paymentsRemaining);
}

/* Calculates the periodic payment amount from annualized interest rate.
 * Converts the annual rate to periodic rate before computing payment.
 *
 * Equation (7) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanPeriodicPayment(
    Number const& principalOutstanding,
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

/* Reverse-calculates principal from periodic payment amount.
 * Used to determine theoretical principal at any point in the schedule.
 *
 * Equation (10) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanPrincipalFromPeriodicPayment(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (periodicRate == 0)
        return periodicPayment * paymentsRemaining;

    return periodicPayment /
        computePaymentFactor(periodicRate, paymentsRemaining);
}

/* Splits gross interest into net interest (to vault) and management fee (to
 * broker). Returns pair of (net interest, management fee).
 *
 * Equation (33) from XLS-66 spec, Section A-2 Equation Glossary
 */
std::pair<Number, Number>
computeInterestAndFeeParts(
    Number const& interest,
    TenthBips16 managementFeeRate)
{
    auto const fee = tenthBipsOfValue(interest, managementFeeRate);

    return std::make_pair(interest - fee, fee);
}

/*
 * Computes the interest and management fee parts from interest amount.
 *
 * Equation (33) from XLS-66 spec, Section A-2 Equation Glossary
 */
std::pair<Number, Number>
computeInterestAndFeeParts(
    Asset const& asset,
    Number const& interest,
    TenthBips16 managementFeeRate,
    std::int32_t loanScale)
{
    auto const fee =
        computeManagementFee(asset, interest, managementFeeRate, loanScale);

    return std::make_pair(interest - fee, fee);
}

/* Calculates penalty interest accrued on overdue payments.
 * Returns 0 if payment is not late.
 *
 * Equation (16) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t nextPaymentDueDate)
{
    auto const now = parentCloseTime.time_since_epoch().count();

    // If the payment is not late by any amount of time, then there's no late
    // interest
    if (now <= nextPaymentDueDate)
        return 0;

    // Equation (3) from XLS-66 spec, Section A-2 Equation Glossary
    auto const secondsOverdue = now - nextPaymentDueDate;

    auto const rate = loanPeriodicRate(lateInterestRate, secondsOverdue);

    return principalOutstanding * rate;
}

/* Calculates interest accrued since the last payment based on time elapsed.
 * Returns 0 if loan is paid ahead of schedule.
 *
 * Equation (27) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
loanAccruedInterest(
    Number const& principalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval)
{
    if (periodicRate == beast::zero)
        return numZero;

    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);
    auto const now = parentCloseTime.time_since_epoch().count();

    // If the loan has been paid ahead, then "lastPaymentDate" is in the future,
    // and no interest has accrued.
    if (now <= lastPaymentDate)
        return numZero;

    // Equation (4) from XLS-66 spec, Section A-2 Equation Glossary
    auto const secondsSinceLastPayment = now - lastPaymentDate;

    // Division is more likely to introduce rounding errors, which will then get
    // amplified by multiplication. Therefore, we first multiply, and only then
    // divide.
    return principalOutstanding * periodicRate * secondsSinceLastPayment /
        paymentInterval;
}

/* Applies a payment to the loan state and returns the breakdown of amounts
 * paid.
 *
 * This is the core function that updates the Loan ledger object fields based on
 * a computed payment.

 * The function is templated to work with both direct Number/uint32_t values
 * (for testing/simulation) and ValueProxy types (for actual ledger updates).
 */
template <class NumberProxy, class UInt32Proxy, class UInt32OptionalProxy>
LoanPaymentParts
doPayment(
    ExtendedPaymentComponents const& payment,
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

        // Mark the loan as complete
        paymentRemainingProxy = 0;

        // Record when the final payment was made
        prevPaymentDateProxy = *nextDueDateProxy;

        // Clear the next due date. Setting it to 0 causes
        // it to be removed from the Loan ledger object, saving space.
        nextDueDateProxy = 0;

        // Zero out all tracked loan balances to mark the loan as paid off.
        // These will be removed from the Loan object since they're default
        // values.
        principalOutstandingProxy = 0;
        totalValueOutstandingProxy = 0;
        managementFeeOutstandingProxy = 0;
    }
    else
    {
        // For regular payments (not overpayments), advance the payment schedule
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

        // Apply the payment deltas to reduce the outstanding balances
        principalOutstandingProxy -= payment.trackedPrincipalDelta;
        totalValueOutstandingProxy -= payment.trackedValueDelta;
        managementFeeOutstandingProxy -= payment.trackedManagementFeeDelta;
    }

    // Principal can never exceed total value (principal is part of total value)
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
        // Principal paid is straightforward - it's the tracked delta
        .principalPaid = payment.trackedPrincipalDelta,

        // Interest paid combines:
        // 1. Tracked interest from the amortization schedule
        //    (derived from the tracked deltas)
        // 2. Untracked interest (e.g., late payment penalties)
        .interestPaid =
            payment.trackedInterestPart() + payment.untrackedInterest,

        // Value change represents how the loan's total value changed beyond
        // normal amortization.
        .valueChange = payment.untrackedInterest,

        // Fee paid combines:
        // 1. Tracked management fees from the amortization schedule
        // 2. Untracked fees (e.g., late payment fees, service fees)
        .feePaid =
            payment.trackedManagementFeeDelta + payment.untrackedManagementFee};
}

/* Simulates an overpayment to validate it won't break the loan's amortization.
 *
 * When a borrower pays more than the scheduled amount, the loan needs to be
 * re-amortized with a lower principal. This function performs that calculation
 * in a "sandbox" using temporary variables, allowing the caller to validate
 * the result before committing changes to the actual ledger.
 *
 * The function preserves accumulated rounding errors across the re-amortization
 * to ensure the loan state remains consistent with its payment history.
 */
Expected<LoanPaymentParts, TER>
tryOverpayment(
    Asset const& asset,
    std::int32_t loanScale,
    ExtendedPaymentComponents const& overpaymentComponents,
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
    // Calculate what the loan state SHOULD be theoretically (at full precision)
    auto const raw = computeRawLoanState(
        periodicPayment, periodicRate, paymentRemaining, managementFeeRate);

    // Get the actual loan state (with accumulated rounding from past payments)
    auto const rounded = constructLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    // Calculate the accumulated rounding errors. These need to be preserved
    // across the re-amortization to maintain consistency with the loan's
    // payment history. Without preserving these errors, the loan could end
    // up with a different total value than what the borrower has actually paid.
    auto const errors = rounded - raw;

    // Compute the new principal by applying the overpayment to the raw
    // (theoretical) principal. Use max with 0 to ensure we never go negative.
    auto const newRawPrincipal = std::max(
        raw.principalOutstanding - overpaymentComponents.trackedPrincipalDelta,
        Number{0});

    // Compute new loan properties based on the reduced principal. This
    // recalculates the periodic payment, total value, and management fees
    // for the remaining payment schedule.
    auto newLoanProperties = computeLoanProperties(
        asset,
        newRawPrincipal,
        interestRate,
        paymentInterval,
        paymentRemaining,
        managementFeeRate,
        loanScale);

    JLOG(j.debug()) << "new periodic payment: "
                    << newLoanProperties.periodicPayment
                    << ", new total value: "
                    << newLoanProperties.totalValueOutstanding
                    << ", first payment principal: "
                    << newLoanProperties.firstPaymentPrincipal;

    // Calculate what the new loan state should be with the new periodic payment
    auto const newRaw = computeRawLoanState(
                            newLoanProperties.periodicPayment,
                            periodicRate,
                            paymentRemaining,
                            managementFeeRate) +
        errors;

    JLOG(j.debug()) << "new raw value: " << newRaw.valueOutstanding
                    << ", principal: " << newRaw.principalOutstanding
                    << ", interest gross: " << newRaw.interestOutstanding();
    // Update the loan state variables with the new values PLUS the preserved
    // rounding errors. This ensures the loan's tracked state remains
    // consistent with its payment history.

    principalOutstanding = std::clamp(
        roundToAsset(
            asset, newRaw.principalOutstanding, loanScale, Number::upward),
        numZero,
        rounded.principalOutstanding);
    totalValueOutstanding = std::clamp(
        roundToAsset(
            asset,
            principalOutstanding + newRaw.interestOutstanding(),
            loanScale,
            Number::upward),
        numZero,
        rounded.valueOutstanding);
    managementFeeOutstanding = std::clamp(
        roundToAsset(asset, newRaw.managementFeeDue, loanScale),
        numZero,
        rounded.managementFeeDue);

    auto const newRounded = constructLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    // Update newLoanProperties so that checkLoanGuards can make an accurate
    // evaluation.
    newLoanProperties.totalValueOutstanding = newRounded.valueOutstanding;

    JLOG(j.debug()) << "new rounded value: " << newRounded.valueOutstanding
                    << ", principal: " << newRounded.principalOutstanding
                    << ", interest gross: " << newRounded.interestOutstanding();

    // Update the periodic payment to reflect the re-amortized schedule
    periodicPayment = newLoanProperties.periodicPayment;

    // check that the loan is still valid
    if (auto const ter = checkLoanGuards(
            asset,
            principalOutstanding,
            // The loan may have been created with interest, but for
            // small interest amounts, that may have already been paid
            // off. Check what's still outstanding. This should
            // guarantee that the interest checks pass.
            newRounded.interestOutstanding() != beast::zero,
            paymentRemaining,
            newLoanProperties,
            j))
    {
        JLOG(j.warn()) << "Principal overpayment would cause the loan to be in "
                          "an invalid state. Ignore the overpayment";

        return Unexpected(tesSUCCESS);
    }

    // Validate that all computed properties are reasonable. These checks should
    // never fail under normal circumstances, but we validate defensively.
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

    auto const deltas = rounded - newRounded;

    auto const hypotheticalValueOutstanding =
        rounded.valueOutstanding - deltas.principal;

    // Calculate how the loan's value changed due to the overpayment.
    // This should be negative (value decreased) or zero. A principal
    // overpayment should never increase the loan's value.
    auto const valueChange =
        newRounded.valueOutstanding - hypotheticalValueOutstanding;
    if (valueChange > 0)
    {
        JLOG(j.warn()) << "Principal overpayment would increase the value of "
                          "the loan. Ignore the overpayment";
        return Unexpected(tesSUCCESS);
    }

    return LoanPaymentParts{
        // Principal paid is the reduction in principal outstanding
        .principalPaid = deltas.principal,
        // Interest paid is the reduction in interest due
        .interestPaid =
            deltas.interest + overpaymentComponents.untrackedInterest,
        // Value change includes both the reduction from paying down principal
        // (negative) and any untracked interest penalties (positive, e.g., if
        // the overpayment itself incurs a fee)
        .valueChange =
            valueChange + overpaymentComponents.trackedInterestPart(),
        // Fee paid includes both the reduction in tracked management fees and
        // any untracked fees on the overpayment itself
        .feePaid = deltas.managementFee +
            overpaymentComponents.untrackedManagementFee};
}

/* Validates and applies an overpayment to the loan state.
 *
 * This function acts as a wrapper around tryOverpayment(), performing the
 * re-amortization calculation in a sandbox (using temporary copies of the
 * loan state), then validating the results before committing them to the
 * actual ledger via the proxy objects.
 *
 * The two-step process (try in sandbox, then commit) ensures that if the
 * overpayment would leave the loan in an invalid state, we can reject it
 * gracefully without corrupting the ledger data.
 */
template <class NumberProxy>
Expected<LoanPaymentParts, TER>
doOverpayment(
    Asset const& asset,
    std::int32_t loanScale,
    ExtendedPaymentComponents const& overpaymentComponents,
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
    // Create temporary copies of the loan state that can be safely modified
    // and discarded if the overpayment doesn't work out. This prevents
    // corrupting the actual ledger data if validation fails.
    Number totalValueOutstanding = totalValueOutstandingProxy;
    Number principalOutstanding = principalOutstandingProxy;
    Number managementFeeOutstanding = managementFeeOutstandingProxy;
    Number periodicPayment = periodicPaymentProxy;

    JLOG(j.debug())
        << "overpayment components:"
        << ", totalValue before: " << *totalValueOutstandingProxy
        << ", valueDelta: " << overpaymentComponents.trackedValueDelta
        << ", principalDelta: " << overpaymentComponents.trackedPrincipalDelta
        << ", managementFeeDelta: "
        << overpaymentComponents.trackedManagementFeeDelta
        << ", interestPart: " << overpaymentComponents.trackedInterestPart()
        << ", untrackedInterest: " << overpaymentComponents.untrackedInterest
        << ", totalDue: " << overpaymentComponents.totalDue
        << ", payments remaining :" << paymentRemaining;

    // Attempt to re-amortize the loan with the overpayment applied.
    // This modifies the temporary copies, leaving the proxies unchanged.
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

    // Safety check: the principal must have decreased. If it didn't (or
    // increased!), something went wrong in the calculation and we should
    // reject the overpayment.
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

    // The proxies still hold the original (pre-overpayment) values, which
    // allows us to compute deltas and verify they match what we expect
    // from the overpaymentComponents and loanPaymentParts.

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

    // I'm not 100% sure the following asserts are correct. If in doubt, and
    // everything else works, remove any that cause trouble.

    JLOG(j.debug()) << "valueChange: " << loanPaymentParts.valueChange
                    << ", totalValue before: " << *totalValueOutstandingProxy
                    << ", totalValue after: " << totalValueOutstanding
                    << ", totalValue delta: "
                    << (totalValueOutstandingProxy - totalValueOutstanding)
                    << ", principalDelta: "
                    << overpaymentComponents.trackedPrincipalDelta
                    << ", principalPaid: " << loanPaymentParts.principalPaid
                    << ", Computed difference: "
                    << overpaymentComponents.trackedPrincipalDelta -
            (totalValueOutstandingProxy - totalValueOutstanding);

    XRPL_ASSERT_PARTS(
        loanPaymentParts.valueChange ==
            totalValueOutstanding -
                (totalValueOutstandingProxy -
                 overpaymentComponents.trackedPrincipalDelta) +
                overpaymentComponents.trackedInterestPart(),
        "ripple::detail::doOverpayment",
        "interest paid agrees");

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

    // All validations passed, so update the proxy objects (which will
    // modify the actual Loan ledger object)
    totalValueOutstandingProxy = totalValueOutstanding;
    principalOutstandingProxy = principalOutstanding;
    managementFeeOutstandingProxy = managementFeeOutstanding;
    periodicPaymentProxy = periodicPayment;

    return loanPaymentParts;
}

/* Computes the payment components for a late payment.
 *
 * A late payment is made after the grace period has expired and includes:
 * 1. All components of a regular periodic payment
 * 2. Late payment penalty interest (accrued since the due date)
 * 3. Late payment fee charged by the broker
 *
 * The late penalty interest increases the loan's total value (the borrower
 * owes more than scheduled), while the regular payment components follow
 * the normal amortization schedule.
 *
 * Implements equation (15) from XLS-66 spec, Section A-2 Equation Glossary
 */
Expected<ExtendedPaymentComponents, TER>
computeLatePayment(
    Asset const& asset,
    ApplyView const& view,
    Number const& principalOutstanding,
    std::int32_t nextDueDate,
    ExtendedPaymentComponents const& periodic,
    TenthBips32 lateInterestRate,
    std::int32_t loanScale,
    Number const& latePaymentFee,
    STAmount const& amount,
    TenthBips16 managementFeeRate,
    beast::Journal j)
{
    // Check if the due date has passed. If not, reject the payment as
    // being too soon
    if (!hasExpired(view, nextDueDate))
        return Unexpected(tecTOO_SOON);

    // Calculate the penalty interest based on how long the payment is overdue.
    auto const latePaymentInterest = loanLatePaymentInterest(
        principalOutstanding,
        lateInterestRate,
        view.parentCloseTime(),
        nextDueDate);

    // Round the late interest and split it between the vault (net interest)
    // and the broker (management fee portion). This lambda ensures we
    // round before splitting to maintain precision.
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

    // Create the late payment components by copying the regular periodic
    // payment and adding the late penalties. We use a lambda to construct
    // this to keep the logic clear. This preserves all the other fields without
    // having to enumerate them.

    ExtendedPaymentComponents const late = [&]() {
        auto inner = periodic;

        return ExtendedPaymentComponents{
            inner,
            // Untracked management fee includes:
            // 1. Regular service fee (from periodic.untrackedManagementFee)
            // 2. Late payment fee (fixed penalty)
            // 3. Management fee portion of late interest
            periodic.untrackedManagementFee + latePaymentFee +
                roundedLateManagementFee,

            // Untracked interest includes:
            // 1. Any untracked interest from the regular payment (usually 0)
            // 2. Late penalty interest (increases loan value)
            // This positive value indicates the loan's value increased due
            // to the late payment.
            periodic.untrackedInterest + roundedLateInterest};
    }();

    XRPL_ASSERT_PARTS(
        isRounded(asset, late.totalDue, loanScale),
        "ripple::detail::computeLatePayment",
        "total due is rounded");

    // Check that the borrower provided enough funds to cover the late payment.
    // The late payment is more expensive than a regular payment due to the
    // penalties.
    if (amount < late.totalDue)
    {
        JLOG(j.warn()) << "Late loan payment amount is insufficient. Due: "
                       << late.totalDue << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    return late;
}

/* Computes payment components for paying off a loan early (before final
 * payment).
 *
 * A full payment closes the loan immediately, paying off all outstanding
 * balances plus a prepayment penalty and any accrued interest since the last
 * payment. This is different from the final scheduled payment, which has no
 * prepayment penalty.
 *
 * The function calculates:
 * - Accrued interest since last payment (time-based)
 * - Prepayment penalty (percentage of remaining principal)
 * - Close payment fee (fixed fee for early closure)
 * - All remaining principal and outstanding fees
 *
 * The loan's value may increase or decrease depending on whether the prepayment
 * penalty exceeds the scheduled interest that would have been paid.
 *
 * Implements equation (26) from XLS-66 spec, Section A-2 Equation Glossary
 */
Expected<ExtendedPaymentComponents, TER>
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
    // Full payment must be made before the final scheduled payment.
    if (paymentRemaining <= 1)
    {
        // If this is the last payment, it has to be a regular payment
        JLOG(j.warn()) << "Last payment cannot be a full payment.";
        return Unexpected(tecKILLED);
    }

    // Calculate the theoretical principal based on the payment schedule.
    // This raw (unrounded) value is used to compute interest and penalties
    // accurately.
    Number const rawPrincipalOutstanding = loanPrincipalFromPeriodicPayment(
        periodicPayment, periodicRate, paymentRemaining);

    // Full payment interest includes both accrued interest (time since last
    // payment) and prepayment penalty (for closing early).
    auto const fullPaymentInterest = computeFullPaymentInterest(
        rawPrincipalOutstanding,
        periodicRate,
        view.parentCloseTime(),
        paymentInterval,
        prevPaymentDate,
        startDate,
        closeInterestRate);

    // Split the full payment interest into net interest (to vault) and
    // management fee (to broker), applying proper rounding.
    auto const [roundedFullInterest, roundedFullManagementFee] = [&]() {
        auto const interest = roundToAsset(
            asset, fullPaymentInterest, loanScale, Number::downward);
        auto const parts = computeInterestAndFeeParts(
            asset, interest, managementFeeRate, loanScale);
        return std::make_tuple(parts.first, parts.second);
    }();

    ExtendedPaymentComponents const full{
        PaymentComponents{
            // Pay off all tracked outstanding balances: principal, interest,
            // and fees.
            // This marks the loan as complete (final payment).
            .trackedValueDelta = principalOutstanding +
                totalInterestOutstanding + managementFeeOutstanding,
            .trackedPrincipalDelta = principalOutstanding,

            // All outstanding management fees are paid. This zeroes out the
            // tracked fee balance.
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::final,
        },

        // Untracked management fee includes:
        // 1. Close payment fee (fixed fee for early closure)
        // 2. Management fee on the full payment interest
        // 3. Minus the outstanding tracked fee (already accounted for above)
        // This can be negative because the outstanding fee is subtracted, but
        // it gets combined with trackedManagementFeeDelta in the final
        // accounting.
        closePaymentFee + roundedFullManagementFee - managementFeeOutstanding,

        // Value change represents the difference between what the loan was
        // expected to earn (totalInterestOutstanding) and what it actually
        // earns (roundedFullInterest with prepayment penalty).
        // - Positive: Prepayment penalty exceeds scheduled interest (loan value
        // increases)
        // - Negative: Prepayment penalty is less than scheduled interest (loan
        // value decreases)
        roundedFullInterest - totalInterestOutstanding,
    };

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

/* Computes the breakdown of a regular periodic payment into principal,
 * interest, and management fee components.
 *
 * This function determines how a single scheduled payment should be split among
 * the three tracked loan components. The calculation accounts for accumulated
 * rounding errors.
 *
 * The algorithm:
 * 1. Calculate what the loan state SHOULD be after this payment (target)
 * 2. Compare current state to target to get deltas
 * 3. Adjust deltas to handle rounding artifacts and edge cases
 * 4. Ensure deltas don't exceed available balances or payment amount
 *
 * Special handling for the final payment: all remaining balances are paid off
 * regardless of the periodic payment amount.
 */
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

    // Final payment: pay off everything remaining, ignoring the normal
    // periodic payment amount. This ensures the loan completes cleanly.
    if (paymentRemaining == 1 ||
        totalValueOutstanding <= roundedPeriodicPayment)
    {
        // If there's only one payment left, we need to pay off each of the loan
        // parts.
        return PaymentComponents{
            .trackedValueDelta = totalValueOutstanding,
            .trackedPrincipalDelta = principalOutstanding,
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::final};
    }

    // Calculate what the loan state SHOULD be after this payment (the target).
    // This is computed at full precision using the theoretical amortization.
    LoanState const trueTarget = computeRawLoanState(
        periodicPayment, periodicRate, paymentRemaining - 1, managementFeeRate);

    // Round the target to the loan's scale to match how actual loan values
    // are stored.
    LoanState const roundedTarget = LoanState{
        .valueOutstanding =
            roundToAsset(asset, trueTarget.valueOutstanding, scale),
        .principalOutstanding =
            roundToAsset(asset, trueTarget.principalOutstanding, scale),
        .interestDue = roundToAsset(asset, trueTarget.interestDue, scale),
        .managementFeeDue =
            roundToAsset(asset, trueTarget.managementFeeDue, scale)};

    // Get the current actual loan state from the ledger values
    LoanState const currentLedgerState = constructLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    // The difference between current and target states gives us the payment
    // components. Any discrepancies from accumulated rounding are captured
    // here.

    LoanStateDeltas deltas = currentLedgerState - roundedTarget;

    // Rounding can occasionally produce negative deltas. Zero them out.
    deltas.nonNegative();

    XRPL_ASSERT_PARTS(
        deltas.principal <= currentLedgerState.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "principal delta not greater than outstanding");

    // Cap each component to never exceed what's actually outstanding
    deltas.principal =
        std::min(deltas.principal, currentLedgerState.principalOutstanding);

    XRPL_ASSERT_PARTS(
        deltas.interest <= currentLedgerState.interestDue,
        "ripple::detail::computePaymentComponents",
        "interest due delta not greater than outstanding");

    // Cap interest to both the outstanding amount AND what's left of the
    // periodic payment after principal is paid
    deltas.interest = std::min(
        {deltas.interest,
         std::max(numZero, roundedPeriodicPayment - deltas.principal),
         currentLedgerState.interestDue});

    XRPL_ASSERT_PARTS(
        deltas.managementFee <= currentLedgerState.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "management fee due delta not greater than outstanding");

    // Cap management fee to both the outstanding amount AND what's left of the
    // periodic payment after principal and interest are paid
    deltas.managementFee = std::min(
        {deltas.managementFee,
         roundedPeriodicPayment - (deltas.principal + deltas.interest),
         currentLedgerState.managementFeeDue});

    // The shortage must never be negative, which indicates that the parts are
    // trying to take more than the whole payment. The excess can be positive,
    // which indicates that we're not going to take the whole payment amount,
    // but if so, it must be small.
    auto takeFrom = [](Number& component, Number& excess) {
        if (excess > beast::zero)
        {
            auto part = std::min(component, excess);
            component -= part;
            excess -= part;
        }
        XRPL_ASSERT_PARTS(
            excess >= beast::zero,
            "ripple::detail::computePaymentComponents",
            "excess non-negative");
    };
    // Helper to reduce deltas when they collectively exceed a limit.
    // Order matters: we prefer to reduce interest first (most flexible),
    // then management fee, then principal (least flexible).
    auto addressExcess = [&takeFrom](LoanStateDeltas& deltas, Number& excess) {
        // This order is based on where errors are the least problematic
        takeFrom(deltas.interest, excess);
        takeFrom(deltas.managementFee, excess);
        takeFrom(deltas.principal, excess);
    };

    // Check if deltas exceed the total outstanding value. This should never
    // happen due to earlier caps, but handle it defensively.
    Number totalOverpayment =
        deltas.total() - currentLedgerState.valueOutstanding;

    if (totalOverpayment > beast::zero)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "ripple::detail::computePaymentComponents : payment exceeded loan "
            "state");
        addressExcess(deltas, totalOverpayment);
        // LCOV_EXCL_STOP
    }

    // Check if deltas exceed the periodic payment amount. Reduce if needed.
    Number shortage = roundedPeriodicPayment - deltas.total();

    XRPL_ASSERT_PARTS(
        isRounded(asset, shortage, scale),
        "ripple::detail::computePaymentComponents",
        "shortage is rounded");

    if (shortage < beast::zero)
    {
        // Deltas exceed payment amount - reduce them proportionally
        Number excess = -shortage;
        addressExcess(deltas, excess);
        shortage = -excess;
    }

    // At this point, shortage >= 0 means we're paying less than the full
    // periodic payment (due to rounding or component caps).
    // shortage < 0 would mean we're trying to pay more than allowed (bug).
    XRPL_ASSERT_PARTS(
        shortage >= beast::zero,
        "ripple::detail::computePaymentComponents",
        "no shortage or excess");

    // Final validation that all components are valid
    XRPL_ASSERT_PARTS(
        deltas.total() ==
            deltas.principal + deltas.interest + deltas.managementFee,
        "ripple::detail::computePaymentComponents",
        "total value adds up");

    XRPL_ASSERT_PARTS(
        deltas.principal >= beast::zero &&
            deltas.principal <= currentLedgerState.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "valid principal result");
    XRPL_ASSERT_PARTS(
        deltas.interest >= beast::zero &&
            deltas.interest <= currentLedgerState.interestDue,
        "ripple::detail::computePaymentComponents",
        "valid interest result");
    XRPL_ASSERT_PARTS(
        deltas.managementFee >= beast::zero &&
            deltas.managementFee <= currentLedgerState.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "valid fee result");

    XRPL_ASSERT_PARTS(
        deltas.principal + deltas.interest + deltas.managementFee > beast::zero,
        "ripple::detail::computePaymentComponents",
        "payment parts add to payment");

    // Final safety clamp to ensure no value exceeds its outstanding balance
    return PaymentComponents{
        .trackedValueDelta = std::clamp(
            deltas.total(), numZero, currentLedgerState.valueOutstanding),
        .trackedPrincipalDelta = std::clamp(
            deltas.principal, numZero, currentLedgerState.principalOutstanding),
        .trackedManagementFeeDelta = std::clamp(
            deltas.managementFee, numZero, currentLedgerState.managementFeeDue),
    };
}

/* Computes payment components for an overpayment scenario.
 *
 * An overpayment occurs when a borrower pays more than the scheduled periodic
 * payment amount. The overpayment is treated as extra principal reduction,
 * but incurs a fee and potentially a penalty interest charge.
 *
 * The calculation (Section 3.2.4.2.3 from XLS-66 spec):
 * 1. Calculate gross penalty interest on the overpayment amount
 * 2. Split the gross interest into net interest and management fee
 * 3. Calculate the penalty fee
 * 4. Determine the principal portion by subtracting the interest (gross) and
 * management fee from the overpayment amount
 *
 * Unlike regular payments which follow the amortization schedule, overpayments
 * apply to principal, reducing the loan balance and future interest costs.
 *
 * Equations (20), (21) and (22) from XLS-66 spec, Section A-2 Equation Glossary
 */
ExtendedPaymentComponents
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

    // First, deduct the fixed overpayment fee from the total amount.
    // This reduces the effective payment that will be applied to the loan.
    // Equation (22) from XLS-66 spec, Section A-2 Equation Glossary
    Number const overpaymentFee = roundToAsset(
        asset, tenthBipsOfValue(overpayment, overpaymentFeeRate), loanScale);

    // Calculate the penalty interest on the effective payment amount.
    // This interest doesn't follow the normal amortization schedule - it's
    // a one-time charge for paying early.
    // Equation (20) and (21) from XLS-66 spec, Section A-2 Equation Glossary
    auto const [rawOverpaymentInterest, _] = [&]() {
        Number const interest =
            tenthBipsOfValue(overpayment, overpaymentInterestRate);
        return detail::computeInterestAndFeeParts(interest, managementFeeRate);
    }();

    // Round the penalty interest components to the loan scale
    auto const [roundedOverpaymentInterest, roundedOverpaymentManagementFee] =
        [&]() {
            Number const interest =
                roundToAsset(asset, rawOverpaymentInterest, loanScale);
            return detail::computeInterestAndFeeParts(
                asset, interest, managementFeeRate, loanScale);
        }();

    auto const result = detail::ExtendedPaymentComponents{
        // Build the payment components, after fees and penalty
        // interest are deducted, the remainder goes entirely to principal
        // reduction.
        detail::PaymentComponents{
            .trackedValueDelta = overpayment - overpaymentFee,
            .trackedPrincipalDelta = overpayment - roundedOverpaymentInterest -
                roundedOverpaymentManagementFee - overpaymentFee,
            .trackedManagementFeeDelta = roundedOverpaymentManagementFee,
            .specialCase = detail::PaymentSpecialCase::extra},
        // Untracked management fee is the fixed overpayment fee
        overpaymentFee,
        // Untracked interest is the penalty interest charged for
        // overpaying.
        // This is positive, representing a one-time cost, but it's
        // typically
        // much smaller than the interest savings from reducing
        // principal.
        roundedOverpaymentInterest};
    XRPL_ASSERT_PARTS(
        result.trackedInterestPart() == roundedOverpaymentInterest,
        "ripple::detail::computeOverpaymentComponents",
        "valid interest computation");
    return result;
}

}  // namespace detail

detail::LoanStateDeltas
operator-(LoanState const& lhs, LoanState const& rhs)
{
    detail::LoanStateDeltas result{
        .principal = lhs.principalOutstanding - rhs.principalOutstanding,
        .interest = lhs.interestDue - rhs.interestDue,
        .managementFee = lhs.managementFeeDue - rhs.managementFeeDue,
    };

    return result;
}

LoanState
operator-(LoanState const& lhs, detail::LoanStateDeltas const& rhs)
{
    LoanState result{
        .valueOutstanding = lhs.valueOutstanding - rhs.total(),
        .principalOutstanding = lhs.principalOutstanding - rhs.principal,
        .interestDue = lhs.interestDue - rhs.interest,
        .managementFeeDue = lhs.managementFeeDue - rhs.managementFee,
    };

    return result;
}

LoanState
operator+(LoanState const& lhs, detail::LoanStateDeltas const& rhs)
{
    LoanState result{
        .valueOutstanding = lhs.valueOutstanding + rhs.total(),
        .principalOutstanding = lhs.principalOutstanding + rhs.principal,
        .interestDue = lhs.interestDue + rhs.interest,
        .managementFeeDue = lhs.managementFeeDue + rhs.managementFee,
    };

    return result;
}

TER
checkLoanGuards(
    Asset const& vaultAsset,
    Number const& principalRequested,
    bool expectInterest,
    std::uint32_t paymentTotal,
    LoanProperties const& properties,
    beast::Journal j)
{
    auto const totalInterestOutstanding =
        properties.totalValueOutstanding - principalRequested;
    // Guard 1: if there is no computed total interest over the life of the
    // loan for a non-zero interest rate, we cannot properly amortize the
    // loan
    if (expectInterest && totalInterestOutstanding <= 0)
    {
        // Unless this is a zero-interest loan, there must be some interest
        // due on the loan, even if it's (measurable) dust
        JLOG(j.warn()) << "Loan for " << principalRequested
                       << " with interest has no interest due";
        return tecPRECISION_LOSS;
    }
    // Guard 1a: If there is any interest computed over the life of the
    // loan, for a zero interest rate, something went sideways.
    if (!expectInterest && totalInterestOutstanding > 0)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Loan for " << principalRequested
                       << " with no interest has interest due";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Guard 2: if the principal portion of the first periodic payment is
    // too small to be accurately represented with the given rounding mode,
    // raise an error
    if (properties.firstPaymentPrincipal <= 0)
    {
        // Check that some true (unrounded) principal is paid each period.
        // Since the first payment pays the least principal, if it's good,
        // they'll all be good. Note that the outstanding principal is
        // rounded, and may not change right away.
        JLOG(j.warn()) << "Loan is unable to pay principal.";
        return tecPRECISION_LOSS;
    }

    // Guard 3: If the periodic payment is so small that it can't even be
    // rounded to a representable value, then the loan can't be paid. Also,
    // avoids dividing by 0.
    auto const roundedPayment = roundPeriodicPayment(
        vaultAsset, properties.periodicPayment, properties.loanScale);
    if (roundedPayment == beast::zero)
    {
        JLOG(j.warn()) << "Loan Periodic payment ("
                       << properties.periodicPayment << ") rounds to 0. ";
        return tecPRECISION_LOSS;
    }

    // Guard 4: if the rounded periodic payment is large enough that the
    // loan can't be amortized in the specified number of payments, raise an
    // error
    {
        NumberRoundModeGuard mg(Number::upward);

        if (std::int64_t const computedPayments{
                properties.totalValueOutstanding / roundedPayment};
            computedPayments != paymentTotal)
        {
            JLOG(j.warn()) << "Loan Periodic payment ("
                           << properties.periodicPayment << ") rounding ("
                           << roundedPayment << ") on a total value of "
                           << properties.totalValueOutstanding
                           << " can not complete the loan in the specified "
                              "number of payments ("
                           << computedPayments << " != " << paymentTotal << ")";
            return tecPRECISION_LOSS;
        }
    }
    return tesSUCCESS;
}

/*
 * This function calculates the full payment interest accrued since the last
 * payment, plus any prepayment penalty.
 *
 * Equations (27) and (28) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
computeFullPaymentInterest(
    Number const& rawPrincipalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate)
{
    auto const accruedInterest = detail::loanAccruedInterest(
        rawPrincipalOutstanding,
        periodicRate,
        parentCloseTime,
        startDate,
        prevPaymentDate,
        paymentInterval);
    XRPL_ASSERT(
        accruedInterest >= 0,
        "ripple::detail::computeFullPaymentInterest : valid accrued "
        "interest");

    // Equation (28) from XLS-66 spec, Section A-2 Equation Glossary
    auto const prepaymentPenalty = closeInterestRate == beast::zero
        ? Number{}
        : tenthBipsOfValue(rawPrincipalOutstanding, closeInterestRate);

    XRPL_ASSERT(
        prepaymentPenalty >= 0,
        "ripple::detail::computeFullPaymentInterest : valid prepayment "
        "interest");

    // Part of equation (27) from XLS-66 spec, Section A-2 Equation Glossary
    return accruedInterest + prepaymentPenalty;
}

Number
computeFullPaymentInterest(
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

    return computeFullPaymentInterest(
        rawPrincipalOutstanding,
        periodicRate,
        parentCloseTime,
        paymentInterval,
        prevPaymentDate,
        startDate,
        closeInterestRate);
}

/* Calculates the theoretical loan state at maximum precision for a given point
 * in the amortization schedule.
 *
 * This function computes what the loan's outstanding balances should be based
 * on the periodic payment amount and number of payments remaining,
 * without considering any rounding that may have been applied to the actual
 * Loan object's state. This "raw" (unrounded) state is used as a target for
 * computing payment components and validating that the loan's tracked state
 * hasn't drifted too far from the theoretical values.
 *
 * The raw state serves several purposes:
 * 1. Computing the expected payment breakdown (principal, interest, fees)
 * 2. Detecting and correcting rounding errors that accumulate over time
 * 3. Validating that overpayments are calculated correctly
 * 4. Ensuring the loan will be fully paid off at the end of its term
 *
 * If paymentRemaining is 0, returns a fully zeroed-out LoanState,
 *       representing a completely paid-off loan.
 */
LoanState
computeRawLoanState(
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

    // Equation (30) from XLS-66 spec, Section A-2 Equation Glossary
    Number const rawTotalValueOutstanding = periodicPayment * paymentRemaining;

    Number const rawPrincipalOutstanding =
        detail::loanPrincipalFromPeriodicPayment(
            periodicPayment, periodicRate, paymentRemaining);

    // Equation (31) from XLS-66 spec, Section A-2 Equation Glossary
    Number const rawInterestOutstandingGross =
        rawTotalValueOutstanding - rawPrincipalOutstanding;

    // Equation (32) from XLS-66 spec, Section A-2 Equation Glossary
    Number const rawManagementFeeOutstanding =
        tenthBipsOfValue(rawInterestOutstandingGross, managementFeeRate);

    // Equation (33) from XLS-66 spec, Section A-2 Equation Glossary
    Number const rawInterestOutstandingNet =
        rawInterestOutstandingGross - rawManagementFeeOutstanding;

    return LoanState{
        .valueOutstanding = rawTotalValueOutstanding,
        .principalOutstanding = rawPrincipalOutstanding,
        .interestDue = rawInterestOutstandingNet,
        .managementFeeDue = rawManagementFeeOutstanding};
};

LoanState
computeRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate)
{
    return computeRawLoanState(
        periodicPayment,
        loanPeriodicRate(interestRate, paymentInterval),
        paymentRemaining,
        managementFeeRate);
}

/* Constructs a LoanState from rounded Loan ledger object values.
 *
 * This function creates a LoanState structure from the three tracked values
 * stored in a Loan ledger object. Unlike calculateRawLoanState(), which
 * computes theoretical unrounded values, this function works with values
 * that have already been rounded to the loan's scale.
 *
 * The key difference from calculateRawLoanState():
 * - calculateRawLoanState: Computes theoretical values at full precision
 * - constructRoundedLoanState: Builds state from actual rounded ledger values
 *
 * The interestDue field is derived from the other three values rather than
 * stored directly, since it can be calculated as:
 *   interestDue = totalValueOutstanding - principalOutstanding -
 * managementFeeOutstanding
 *
 * This ensures consistency across the codebase and prevents copy-paste errors
 * when creating LoanState objects from Loan ledger data.
 */
LoanState
constructLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding)
{
    // This implementation is pretty trivial, but ensures the calculations
    // are consistent everywhere, and reduces copy/paste errors.
    return LoanState{
        .valueOutstanding = totalValueOutstanding,
        .principalOutstanding = principalOutstanding,
        .interestDue = totalValueOutstanding - principalOutstanding -
            managementFeeOutstanding,
        .managementFeeDue = managementFeeOutstanding};
}

LoanState
constructRoundedLoanState(SLE::const_ref loan)
{
    return constructLoanState(
        loan->at(sfTotalValueOutstanding),
        loan->at(sfPrincipalOutstanding),
        loan->at(sfManagementFeeOutstanding));
}

/*
 * This function calculates the fee owed to the broker based on the asset,
 * value, and management fee rate.
 *
 * Equation (32) from XLS-66 spec, Section A-2 Equation Glossary
 */
Number
computeManagementFee(
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

/*
 * Given the loan parameters, compute the derived properties of the loan.
 */
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

        // Equation (30) from XLS-66 spec, Section A-2 Equation Glossary
        STAmount amount{asset, periodicPayment * paymentsRemaining};

        // Base the loan scale on the total value, since that's going to be
        // the biggest number involved (barring unusual parameters for late,
        // full, or over payments)
        auto const loanScale = std::max(minimumScale, amount.exponent());
        XRPL_ASSERT_PARTS(
            (amount.integral() && loanScale == 0) ||
                (!amount.integral() &&
                 loanScale >= static_cast<Number>(amount).exponent()),
            "ripple::computeLoanProperties",
            "loanScale value fits expectations");

        // We may need to truncate the total value because of the minimum
        // scale
        amount = roundToAsset(asset, amount, loanScale, Number::to_nearest);

        return std::make_pair(amount, loanScale);
    }();

    // Since we just figured out the loan scale, we haven't been able to
    // validate that the principal fits in it, so to allow this function to
    // succeed, round it here, and let the caller do the validation.
    principalOutstanding = roundToAsset(
        asset, principalOutstanding, loanScale, Number::to_nearest);

    // E<quation (31) from XLS-66 spec, Section A-2 Equation Glossary
    auto const totalInterestOutstanding =
        totalValueOutstanding - principalOutstanding;
    auto const feeOwedToBroker = computeManagementFee(
        asset, totalInterestOutstanding, managementFeeRate, loanScale);

    // Compute the principal part of the first payment. This is needed
    // because the principal part may be rounded down to zero, which
    // would prevent the principal from ever being paid down.
    auto const firstPaymentPrincipal = [&]() {
        // Compute the parts for the first payment. Ensure that the
        // principal payment will actually change the principal.
        auto const startingState = computeRawLoanState(
            periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate);

        auto const firstPaymentState = computeRawLoanState(
            periodicPayment,
            periodicRate,
            paymentsRemaining - 1,
            managementFeeRate);

        // The unrounded principal part needs to be large enough to affect
        // the principal. What to do if not is left to the caller
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

/*
 * This is the main function to make a loan payment.
 * This function handles regular, late, full, and overpayments.
 * It is an implementation of the make_payment function from the XLS-66
 * spec. Section 3.2.4.4
 */
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
    using namespace Lending;

    auto principalOutstandingProxy = loan->at(sfPrincipalOutstanding);
    auto paymentRemainingProxy = loan->at(sfPaymentRemaining);

    if (paymentRemainingProxy == 0 || principalOutstandingProxy == 0)
    {
        // Loan complete this is already checked in LoanPay::preclaim()
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

    // Compute the periodic rate that will be used for calculations
    // throughout
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
        // If the payment is late, and the late flag was not set, it's not
        // valid
        JLOG(j.warn()) << "Loan payment is overdue. Use the tfLoanLatePayment "
                          "transaction "
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

        LoanState const roundedLoanState = constructLoanState(
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
        JLOG(j.error()) << "Full payment computation failed unexpectedly.";
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    // -------------------------------------------------------------
    // compute the periodic payment info that will be needed whether the
    // payment is late or regular
    detail::ExtendedPaymentComponents periodic{
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
        JLOG(j.error()) << "Late payment computation failed unexpectedly.";
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

    // Keep a running total of the actual parts paid
    LoanPaymentParts totalParts;
    Number totalPaid;
    std::size_t numPayments = 0;

    while ((amount >= (totalPaid + periodic.totalDue)) &&
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

        periodic = detail::ExtendedPaymentComponents{
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

        detail::ExtendedPaymentComponents const overpaymentComponents =
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
            // Can't just use `periodicPayment` here, because it might
            // change
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
                // error() will be the TER returned if a payment is not
                // made. It will only evaluate to true if it's unsuccessful.
                // Otherwise, tesSUCCESS means nothing was done, so
                // continue.
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
