#include <xrpld/app/misc/LendingHelpers.h>
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
        (365 * 24 * 60 * 60);
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
    if (parentCloseTime.time_since_epoch().count() <= nextPaymentDueDate)
        return 0;

    // Equation (3) from XLS-66 spec, Section A-2 Equation Glossary
    auto const secondsOverdue =
        parentCloseTime.time_since_epoch().count() - nextPaymentDueDate;

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
    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);

    // No accrued interest if loan is paid ahead
    if (parentCloseTime.time_since_epoch().count() <= lastPaymentDate)
        return 0;

    // Equation (4) from XLS-66 spec, Section A-2 Equation Glossary
    auto const secondsSinceLastPayment =
        parentCloseTime.time_since_epoch().count() - lastPaymentDate;

    return principalOutstanding * periodicRate *
        (secondsSinceLastPayment / paymentInterval);
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
    auto const raw = calculateRawLoanState(
        periodicPayment, periodicRate, paymentRemaining, managementFeeRate);

    // Get the actual loan state (with accumulated rounding from past payments)
    auto const rounded = constructRoundedLoanState(
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
    auto const newLoanProperties = computeLoanProperties(
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
    auto const newRaw = calculateRawLoanState(
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

    auto const newRounded = constructRoundedLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

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

/** Handle possible late payments.
 *
 * If this function processed a late payment, the return value will be
 * a LoanPaymentParts object. If the loan is not late, the return will be an
 * Unexpected(tesSUCCESS). Otherwise, it'll be an Unexpected with the error code
 * the caller is expected to return.
 *
 * Equation (15) from XLS-66 spec, Section A-2 Equation Glossary
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
    if (!hasExpired(view, nextDueDate))
        return Unexpected(tecTOO_SOON);

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
    ExtendedPaymentComponents const late = [&]() {
        auto inner = periodic;

        return ExtendedPaymentComponents{
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
 * an ExtendedPaymentComponents object. Otherwise, it'll be an Unexpected with
 * the error code the caller is expected to return. It should NEVER return
 * tesSUCCESS
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
    if (paymentRemaining <= 1)
    {
        // If this is the last payment, it has to be a regular payment
        JLOG(j.warn()) << "Full payment requested when only final "
                       << "payment remains.";
        return Unexpected(tecKILLED);
    }

    // Calculate the raw principal outstanding from the periodic payment
    // rawPrincipalOutstanding will be used to compute the full payment interest
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

    ExtendedPaymentComponents const full{
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
    if (principal < beast::zero)
        principal = numZero;
    if (interest < beast::zero)
        interest = numZero;
    if (managementFee < beast::zero)
        managementFee = numZero;
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
    LoanState const currentLedgerState = constructRoundedLoanState(
        totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    LoanDeltas deltas = currentLedgerState - roundedTarget;
    deltas.nonNegative();

    // Adjust the deltas if necessary for data integrity
    XRPL_ASSERT_PARTS(
        deltas.principal <= currentLedgerState.principalOutstanding,
        "ripple::detail::computePaymentComponents",
        "principal delta not greater than outstanding");

    deltas.principal =
        std::min(deltas.principal, currentLedgerState.principalOutstanding);

    XRPL_ASSERT_PARTS(
        deltas.interest <= currentLedgerState.interestDue,
        "ripple::detail::computePaymentComponents",
        "interest due delta not greater than outstanding");

    deltas.interest = std::min(
        {deltas.interest,
         std::max(numZero, roundedPeriodicPayment - deltas.principal),
         currentLedgerState.interestDue});

    XRPL_ASSERT_PARTS(
        deltas.managementFee <= currentLedgerState.managementFeeDue,
        "ripple::detail::computePaymentComponents",
        "management fee due delta not greater than outstanding");

    deltas.managementFee = std::min(
        {deltas.managementFee,
         roundedPeriodicPayment - (deltas.principal + deltas.interest),
         currentLedgerState.managementFeeDue});

    if (paymentRemaining == 1 ||
        totalValueOutstanding <= roundedPeriodicPayment)
    {
        // If there's only one payment left, we need to pay off each of the loan
        // parts.

        XRPL_ASSERT_PARTS(
            deltas.total() <= totalValueOutstanding,
            "ripple::detail::computePaymentComponents",
            "last payment total value agrees");
        XRPL_ASSERT_PARTS(
            deltas.principal <= principalOutstanding,
            "ripple::detail::computePaymentComponents",
            "last payment principal agrees");
        XRPL_ASSERT_PARTS(
            deltas.managementFee <= managementFeeOutstanding,
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
        takeFrom(deltas.interest, excess);
        takeFrom(deltas.managementFee, excess);
        takeFrom(deltas.principal, excess);
    };
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

    // Make sure the parts don't add up to too much
    Number shortage = roundedPeriodicPayment - deltas.total();

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

    return PaymentComponents{
        // As a final safety check, ensure the value is non-negative, and won't
        // make the corresponding item negative
        .trackedValueDelta = std::clamp(
            deltas.total(), numZero, currentLedgerState.valueOutstanding),
        .trackedPrincipalDelta = std::clamp(
            deltas.principal, numZero, currentLedgerState.principalOutstanding),
        .trackedManagementFeeDelta = std::clamp(
            deltas.managementFee, numZero, currentLedgerState.managementFeeDue),
    };
}

/*
 * Compute the payment components for an overpayment scenario. This includes
 * computing the fee on the overpayment, and splitting the interest and
 * management fee parts.
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

    // Equation (22) from XLS-66 spec, Section A-2 Equation Glossary
    Number const fee = roundToAsset(
        asset, tenthBipsOfValue(overpayment, overpaymentFeeRate), loanScale);

    // Equation (20) and (21) from XLS-66 spec, Section A-2 Equation
    // Glossary
    auto const [rawOverpaymentInterest, _] = [&]() {
        Number const interest =
            tenthBipsOfValue(overpayment, overpaymentInterestRate);
        return detail::computeInterestAndFeeParts(interest, managementFeeRate);
    }();

    auto const [roundedOverpaymentInterest, roundedOverpaymentManagementFee] =
        [&]() {
            Number const interest =
                roundToAsset(asset, rawOverpaymentInterest, loanScale);
            return detail::computeInterestAndFeeParts(
                asset, interest, managementFeeRate, loanScale);
        }();

    auto const result = detail::ExtendedPaymentComponents{
        detail::PaymentComponents{
            .trackedValueDelta = overpayment - fee,
            .trackedPrincipalDelta = overpayment - roundedOverpaymentInterest -
                roundedOverpaymentManagementFee - fee,
            .trackedManagementFeeDelta = roundedOverpaymentManagementFee,
            .specialCase = detail::PaymentSpecialCase::extra},
        fee,
        roundedOverpaymentInterest};
    XRPL_ASSERT_PARTS(
        result.trackedInterestPart() == roundedOverpaymentInterest,
        "ripple::detail::computeOverpaymentComponents",
        "valid interest computation");
    return result;
}

}  // namespace detail

detail::LoanDeltas
operator-(LoanState const& lhs, LoanState const& rhs)
{
    detail::LoanDeltas result{
        .principal = lhs.principalOutstanding - rhs.principalOutstanding,
        .interest = lhs.interestDue - rhs.interestDue,
        .managementFee = lhs.managementFeeDue - rhs.managementFeeDue,
    };

    return result;
}

LoanState
operator-(LoanState const& lhs, detail::LoanDeltas const& rhs)
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
operator+(LoanState const& lhs, detail::LoanDeltas const& rhs)
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
calculateFullPaymentInterest(
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
        "ripple::detail::computeFullPaymentInterest : valid accrued interest");

    // Equation (28) from XLS-66 spec, Section A-2 Equation Glossary
    auto const prepaymentPenalty =
        tenthBipsOfValue(rawPrincipalOutstanding, closeInterestRate);
    XRPL_ASSERT(
        prepaymentPenalty >= 0,
        "ripple::detail::computeFullPaymentInterest : valid prepayment "
        "interest");

    // Part of equation (27) from XLS-66 spec, Section A-2 Equation Glossary
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
constructRoundedLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding)
{
    // This implementation is pretty trivial, but ensures the calculations are
    // consistent everywhere, and reduces copy/paste errors.
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
    return constructRoundedLoanState(
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

/*
 * This is the main function to make a loan payment.
 * This function handles regular, late, full, and overpayments.
 * It is an implementation of the make_payment function from the XLS-66 spec.
 * Section 3.2.4.4
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

    // Compute the periodic rate that will be used for calculations throughout
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

        LoanState const roundedLoanState = constructRoundedLoanState(
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
    // compute the periodic payment info that will be needed whether the payment
    // is late or regular
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
