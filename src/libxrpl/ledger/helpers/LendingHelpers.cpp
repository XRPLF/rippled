/** @file
 *  Numerical core of the XRPL lending protocol (XLS-66).
 *
 *  Implements every mathematical operation in a loan's life cycle: computing
 *  amortized periodic payments, splitting each payment into principal,
 *  interest, and management-fee components, and handling late, full (early-
 *  closure), and overpayment scenarios. The top-level entry point
 *  `loanMakePayment()` implements the `make_payment` function from XLS-66
 *  §3.2.4.4. All equation references below are to Section A-2 of that spec.
 */
#include <xrpl/ledger/helpers/LendingHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace xrpl {

/** Verify all amendment prerequisites for the lending protocol are active.
 *
 *  Every lending transactor calls this in `checkExtraFeatures()`. Adding a
 *  new prerequisite here gates all lending transactions atomically.
 *
 *  @param rules  Active amendment rules for the current ledger.
 *  @param tx     The transaction being validated.
 *  @return `true` if all required amendments are enabled and the transaction
 *      is consistent with them; `false` if the transaction must be rejected.
 */
bool
checkLendingProtocolDependencies(Rules const& rules, STTx const& tx)
{
    if (!rules.enabled(featureSingleAssetVault))
        return false;

    if (!rules.enabled(featureMPTokensV1))
        return false;

    if (tx.isFieldPresent(sfDomainID) && !rules.enabled(featurePermissionedDomains))
        return false;

    return true;
}

/** Accumulate payment parts from multiple consecutive payment rounds.
 *
 *  Used by `loanMakePayment()` to sum regular payments made in a single
 *  transaction when the borrower supplies enough funds to cover more than
 *  one installment. All component fields of `other` must be non-negative.
 *
 *  @param other  Payment parts from the next completed payment round.
 *  @return Reference to `*this` with accumulated totals.
 */
LoanPaymentParts&
LoanPaymentParts::operator+=(LoanPaymentParts const& other)
{
    XRPL_ASSERT(

        other.principalPaid >= beast::kZERO,
        "xrpl::LoanPaymentParts::operator+= : other principal "
        "non-negative");
    XRPL_ASSERT(
        other.interestPaid >= beast::kZERO,
        "xrpl::LoanPaymentParts::operator+= : other interest paid "
        "non-negative");
    XRPL_ASSERT(
        other.feePaid >= beast::kZERO,
        "xrpl::LoanPaymentParts::operator+= : other fee paid "
        "non-negative");

    principalPaid += other.principalPaid;
    interestPaid += other.interestPaid;
    valueChange += other.valueChange;
    feePaid += other.feePaid;
    return *this;
}

/** Compare two `LoanPaymentParts` for exact equality across all fields.
 *
 *  @param other  The parts to compare against.
 *  @return `true` if all four fields are equal.
 */
bool
LoanPaymentParts::operator==(LoanPaymentParts const& other) const
{
    return principalPaid == other.principalPaid && interestPaid == other.interestPaid &&
        valueChange == other.valueChange && feePaid == other.feePaid;
}

/** Convert an annualized interest rate to a per-payment-period rate.
 *
 *  Prorates the annual rate by the fraction `paymentInterval / secondsInYear`.
 *  Implements Equation (1) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param interestRate     Annual interest rate in tenth-of-a-basis-point units.
 *  @param paymentInterval  Length of one payment period in seconds.
 *  @return The per-period rate as a `Number` at full floating-point precision.
 */
Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval)
{
    // Need floating point math, since we're dividing by a large number
    return tenthBipsOfValue(Number(paymentInterval), interestRate) / kSECONDS_IN_YEAR;
}

/** Check whether a value is already rounded to the given scale.
 *
 *  Compares the downward- and upward-rounded forms; equality means no
 *  sub-scale precision remains. Used as a precondition guard and post-
 *  condition assertion throughout the payment pipeline.
 *
 *  @param asset  Asset whose representable precision constrains rounding.
 *  @param value  The value to test.
 *  @param scale  Exponent that defines the target precision.
 *  @return `true` if `roundDown(value) == roundUp(value)` at `scale`.
 */
bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale)
{
    return roundToAsset(asset, value, scale, Number::RoundingMode::Downward) ==
        roundToAsset(asset, value, scale, Number::RoundingMode::Upward);
}

namespace detail {

/** Clamp all delta fields to zero from below.
 *
 *  Rounding can occasionally produce tiny negative deltas when the theoretical
 *  target exceeds the current rounded state by a sub-scale amount. This method
 *  eliminates those artifacts before the deltas are used as payment amounts.
 */
void
LoanStateDeltas::nonNegative()
{
    if (principal < beast::kZERO)
        principal = kNUM_ZERO;
    if (interest < beast::kZERO)
        interest = kNUM_ZERO;
    if (managementFee < beast::kZERO)
        managementFee = kNUM_ZERO;
}

/** Compute `(1 + r)^n - 1` accurately for near-zero `r` via binomial expansion.
 *
 *  Direct subtraction `power(1 + r, n) - 1` suffers catastrophic cancellation
 *  when `r` is small: the result `~r*n` sits far below the leading `1` in
 *  `(1+r)^n`, consuming most of Number's 19-digit mantissa. The binomial
 *  expansion avoids this:
 *
 *  @code
 *    (1 + r)^n - 1 = nr + C(n,2) r^2 + ... + r^n
 *  @endcode
 *
 *  Each term is derived from the previous as `term_{k+1} = term_k * r * (n-k) / (k+1)`.
 *  The loop terminates early once adding the next term leaves the running sum
 *  unchanged (below Number's precision floor).
 *
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of periods `n`.
 *  @return `(1 + r)^n - 1`, or 0 if `r == 0` or `n == 0`.
 *  @note For `r * n >= 1e-9` the closed-form path in `computePowerMinusOneHybrid`
 *      is ~30-500x faster and equally accurate; prefer the hybrid for production use.
 */
Number
computePowerMinusOne(Number const& periodicRate, std::uint32_t paymentsRemaining)
{
    XRPL_ASSERT_PARTS(
        periodicRate >= beast::kZERO,
        "xrpl::detail::computePowerMinusOne",
        "periodicRate is non-negative");

    if (paymentsRemaining == 0 || periodicRate == beast::kZERO)
        return kNUM_ZERO;

    // k = 1 term: C(n, 1) * r = n * r
    Number term = paymentsRemaining * periodicRate;
    Number sum = term;
    for (std::uint32_t k = 1; k < paymentsRemaining; ++k)
    {
        // term_{k+1} from term_k: multiply by r * (n - k) / (k + 1)
        term = term * periodicRate * (paymentsRemaining - k) / (k + 1);
        Number const next = sum + term;
        // adding this term fell below Number's precision
        if (next == sum)
            break;
        sum = next;
    }
    return sum;
}

/** Compute `(1 + r)^n - 1`, selecting the numerically stable path automatically.
 *
 *  When `r * n >= 1e-9` the closed-form `power(1 + r, n) - 1` retains enough
 *  precision and is ~30-500x faster than the binomial expansion. Below that
 *  threshold cancellation becomes severe — the `~r*n` result sits well below
 *  the `1` consumed by the leading term of `(1+r)^n` — so the call is
 *  forwarded to `computePowerMinusOne()`.
 *
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of periods `n`.
 *  @return `(1 + r)^n - 1`, or 0 if `r == 0` or `n == 0`.
 *  @note The threshold `1e-9` is chosen so that both paths agree to within
 *      Number's post-subtraction precision (~10 significant digits) at the
 *      crossover, verified by `testComputePowerMinusOneHybrid`.
 */
Number
computePowerMinusOneHybrid(Number const& periodicRate, std::uint32_t paymentsRemaining)
{
    XRPL_ASSERT_PARTS(
        periodicRate >= beast::kZERO,
        "xrpl::detail::computePowerMinusOneHybrid",
        "periodicRate is non-negative");

    if (paymentsRemaining == 0 || periodicRate == beast::kZERO)
        return kNUM_ZERO;

    // Threshold 1e-9 retains ~10 sig digits of (1+r)^n - 1 against
    // Number's 19-digit mantissa: the leading "1" of (1+r)^n consumes
    // ~log10(1/(r*n)) digits before the subtraction. Above this point
    // closed form is accurate and ~30-500x faster than the binomial
    // expansion.
    Number const cancellationThreshold{1, -9};
    if (paymentsRemaining * periodicRate >= cancellationThreshold)
        return power(1 + periodicRate, paymentsRemaining) - 1;

    return computePowerMinusOne(periodicRate, paymentsRemaining);
}

/** Compute the standard amortization payment factor `r(1+r)^n / ((1+r)^n - 1)`.
 *
 *  Multiplying this factor by the outstanding principal yields the fixed
 *  periodic payment. Implements Equation (6) from XLS-66, Section A-2.
 *
 *  When `fixCleanup3_2_0` is enabled the denominator `(1+r)^n - 1` is
 *  evaluated via `computePowerMinusOneHybrid()` to avoid catastrophic
 *  cancellation at near-zero rates. The pre-amendment path uses the direct
 *  `power(1+r, n) - 1` form and is preserved for historic replay.
 *
 *  @param rules             Active amendment rules (gates the hybrid path).
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of remaining payments `n`.
 *  @return The payment factor, or `1/n` when `r == 0`, or 0 when `n == 0`.
 */
Number
computePaymentFactor(
    Rules const& rules,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (paymentsRemaining == 0)
        return kNUM_ZERO;

    // For zero interest, payment factor is simply 1/paymentsRemaining
    if (periodicRate == beast::kZERO)
        return Number{1} / paymentsRemaining;

    if (rules.enabled(fixCleanup3_2_0))
    {
        Number const raisedRateMinusOne =
            computePowerMinusOneHybrid(periodicRate, paymentsRemaining);
        Number const raisedRate = 1 + raisedRateMinusOne;

        return (periodicRate * raisedRate) / raisedRateMinusOne;
    }

    // Pre-fixCleanup3_2_0: direct subtraction `(1+r)^n - 1` suffers
    // catastrophic cancellation at near-zero rates. Retained for
    // amendment-gated bit-exact pre-fix behavior.
    Number const raisedRate = power(1 + periodicRate, paymentsRemaining);

    return (periodicRate * raisedRate) / (raisedRate - 1);
}

/** Compute the fixed installment amount for a standard amortized loan.
 *
 *  Implements `principal * paymentFactor(r, n)`. For zero-interest loans the
 *  formula degenerates to equal principal slices (`principal / n`). Implements
 *  Equation (7) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param rules                Active amendment rules (passed to `computePaymentFactor`).
 *  @param principalOutstanding Current outstanding principal.
 *  @param periodicRate         Per-period interest rate.
 *  @param paymentsRemaining    Number of payments left in the schedule.
 *  @return The unrounded periodic payment, or 0 if `principalOutstanding == 0`
 *      or `paymentsRemaining == 0`.
 */
Number
loanPeriodicPayment(
    Rules const& rules,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (principalOutstanding == 0 || paymentsRemaining == 0)
        return 0;

    // Interest-free loans: equal principal payments
    if (periodicRate == beast::kZERO)
        return principalOutstanding / paymentsRemaining;

    return principalOutstanding * computePaymentFactor(rules, periodicRate, paymentsRemaining);
}

/** Reverse-calculate the outstanding principal implied by a given periodic payment.
 *
 *  The inverse of `loanPeriodicPayment()`: recovers what the principal should be
 *  at a given point in the amortization schedule, used by `computeTheoreticalLoanState()`
 *  and the early-closure path. Implements Equation (10) from XLS-66, Section A-2.
 *
 *  @param rules             Active amendment rules (passed to `computePaymentFactor`).
 *  @param periodicPayment   Fixed installment amount.
 *  @param periodicRate      Per-period interest rate.
 *  @param paymentsRemaining Number of payments remaining.
 *  @return Theoretical outstanding principal, or 0 if `paymentsRemaining == 0`, or
 *      `periodicPayment * paymentsRemaining` when `periodicRate == 0`.
 */
Number
loanPrincipalFromPeriodicPayment(
    Rules const& rules,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining)
{
    if (paymentsRemaining == 0)
        return kNUM_ZERO;

    if (periodicRate == 0)
        return periodicPayment * paymentsRemaining;

    return periodicPayment / computePaymentFactor(rules, periodicRate, paymentsRemaining);
}

/** Split a gross interest amount into net interest (vault) and management fee (broker).
 *
 *  Computes `fee = computeManagementFee(interest, managementFeeRate)` and
 *  returns `(interest - fee, fee)`. Implements Equation (33) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param asset            Asset used for rounding the fee.
 *  @param interest         Gross interest amount to split.
 *  @param managementFeeRate Broker's share of gross interest in tenth-bips.
 *  @param loanScale        Exponent for rounding the fee.
 *  @return Pair `(netInterest, fee)` where `netInterest + fee == interest`.
 */
std::pair<Number, Number>
computeInterestAndFeeParts(
    Asset const& asset,
    Number const& interest,
    TenthBips16 managementFeeRate,
    std::int32_t loanScale)
{
    auto const fee = computeManagementFee(asset, interest, managementFeeRate, loanScale);

    return std::make_pair(interest - fee, fee);
}

/** Compute penalty interest that has accrued on an overdue payment.
 *
 *  Calculates `principal * loanPeriodicRate(lateInterestRate, secondsOverdue)`.
 *  Returns 0 if the payment is on time or early, if `principalOutstanding == 0`,
 *  or if `lateInterestRate == 0`. Implements Equation (16) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param principalOutstanding Current outstanding principal.
 *  @param lateInterestRate     Annualized penalty rate in tenth-of-a-basis-point units.
 *  @param parentCloseTime      Close time of the parent ledger (the "now" for overdue calc).
 *  @param nextPaymentDueDate   The timestamp when the payment was originally due.
 *  @return Unrounded late penalty interest, or 0 if the payment is not overdue.
 */
Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t nextPaymentDueDate)
{
    if (principalOutstanding == beast::kZERO)
        return kNUM_ZERO;

    if (lateInterestRate == TenthBips32{0})
        return kNUM_ZERO;

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

/** Compute interest accrued since the last payment, prorated by elapsed time.
 *
 *  Computes `principal * periodicRate * secondsSinceLastPayment / paymentInterval`,
 *  where `lastPaymentDate = max(prevPaymentDate, startDate)`. Multiplication
 *  is performed before division to minimise rounding amplification. Returns 0
 *  if the loan is paid ahead of schedule (i.e. `now <= lastPaymentDate`).
 *  Implements Equation (27) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param principalOutstanding Current outstanding principal.
 *  @param periodicRate         Per-period interest rate.
 *  @param parentCloseTime      Close time of the parent ledger (current time).
 *  @param startDate            Unix timestamp when the loan started accruing.
 *  @param prevPaymentDate      Due date of the most recently completed payment.
 *  @param paymentInterval      Length of one payment period in seconds.
 *  @return Unrounded accrued interest, or 0 if `periodicRate == 0`, `paymentInterval == 0`,
 *      or the loan is ahead of schedule.
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
    if (periodicRate == beast::kZERO)
        return kNUM_ZERO;

    if (paymentInterval == 0)
        return kNUM_ZERO;

    auto const lastPaymentDate = std::max(prevPaymentDate, startDate);
    auto const now = parentCloseTime.time_since_epoch().count();

    // If the loan has been paid ahead, then "lastPaymentDate" is in the future,
    // and no interest has accrued.
    if (now <= lastPaymentDate)
        return kNUM_ZERO;

    // Equation (4) from XLS-66 spec, Section A-2 Equation Glossary
    auto const secondsSinceLastPayment = now - lastPaymentDate;

    // Division is more likely to introduce rounding errors, which will then get
    // amplified by multiplication. Therefore, we first multiply, and only then
    // divide.
    return principalOutstanding * periodicRate * secondsSinceLastPayment / paymentInterval;
}

/** Apply a fully-computed payment to the loan state and return the payment breakdown.
 *
 *  The core commit step: subtracts the `payment` deltas from the three outstanding
 *  balance proxies and advances the payment schedule (`paymentRemaining`,
 *  `prevPaymentDate`, `nextDueDate`). For a `PaymentSpecialCase::Final` payment all
 *  balances are zeroed and `nextDueDate` is cleared, marking the loan as paid off.
 *  For a `PaymentSpecialCase::Extra` (overpayment) the schedule is not advanced.
 *
 *  Templated on proxy types so the same function can run against `ValueProxy<T>`
 *  objects (which write through to the Loan SLE) or plain value types for unit
 *  tests and simulation.
 *
 *  @tparam NumberProxy         Type exposing `Number` read/write semantics.
 *  @tparam UInt32Proxy         Type exposing `uint32_t` read/write semantics.
 *  @tparam UInt32OptionalProxy Type exposing optional-`uint32_t` read/write semantics.
 *  @param payment                      Fully-computed payment components to apply.
 *  @param totalValueOutstandingProxy   Proxy for `sfTotalValueOutstanding`.
 *  @param principalOutstandingProxy    Proxy for `sfPrincipalOutstanding`.
 *  @param managementFeeOutstandingProxy Proxy for `sfManagementFeeOutstanding`.
 *  @param paymentRemainingProxy        Proxy for `sfPaymentRemaining`.
 *  @param prevPaymentDateProxy         Proxy for `sfPreviousPaymentDueDate`.
 *  @param nextDueDateProxy             Proxy for `sfNextPaymentDueDate`; must be set.
 *  @param paymentInterval              Payment period length in seconds.
 *  @return Breakdown of amounts paid, suitable for return from `loanMakePayment()`.
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
    XRPL_ASSERT_PARTS(nextDueDateProxy, "xrpl::detail::doPayment", "Next due date proxy set");

    if (payment.specialCase == PaymentSpecialCase::Final)
    {
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy == payment.trackedPrincipalDelta,
            "xrpl::detail::doPayment",
            "Full principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy == payment.trackedValueDelta,
            "xrpl::detail::doPayment",
            "Full value payment");
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy == payment.trackedManagementFeeDelta,
            "xrpl::detail::doPayment",
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
        if (payment.specialCase != PaymentSpecialCase::Extra)
        {
            paymentRemainingProxy -= 1;

            prevPaymentDateProxy = nextDueDateProxy;
            nextDueDateProxy += paymentInterval;
        }
        XRPL_ASSERT_PARTS(
            principalOutstandingProxy > payment.trackedPrincipalDelta,
            "xrpl::detail::doPayment",
            "Partial principal payment");
        XRPL_ASSERT_PARTS(
            totalValueOutstandingProxy > payment.trackedValueDelta,
            "xrpl::detail::doPayment",
            "Partial value payment");
        // Management fees are expected to be relatively small, and could get to
        // zero before the loan is paid off
        XRPL_ASSERT_PARTS(
            managementFeeOutstandingProxy >= payment.trackedManagementFeeDelta,
            "xrpl::detail::doPayment",
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
        "xrpl::detail::doPayment",
        "principal does not exceed total");

    XRPL_ASSERT_PARTS(
        // Use an explicit cast because the template parameter can be
        // ValueProxy<Number> or Number
        static_cast<Number>(managementFeeOutstandingProxy) >= beast::kZERO,
        "xrpl::detail::doPayment",
        "fee outstanding stays valid");

    return LoanPaymentParts{
        // Principal paid is straightforward - it's the tracked delta
        .principalPaid = payment.trackedPrincipalDelta,

        // Interest paid combines:
        // 1. Tracked interest from the amortization schedule
        //    (derived from the tracked deltas)
        // 2. Untracked interest (e.g., late payment penalties)
        .interestPaid = payment.trackedInterestPart() + payment.untrackedInterest,

        // Value change represents how the loan's total value changed beyond
        // normal amortization.
        .valueChange = payment.untrackedInterest,

        // Fee paid combines:
        // 1. Tracked management fees from the amortization schedule
        // 2. Untracked fees (e.g., late payment fees, service fees)
        .feePaid = payment.trackedManagementFeeDelta + payment.untrackedManagementFee};
}

/** Simulate a principal overpayment and re-amortize the loan in a sandbox.
 *
 *  When a borrower pays more than the scheduled amount the remaining schedule
 *  must be re-amortized from a lower principal. This function:
 *  1. Computes the theoretical (unrounded) current state.
 *  2. Measures accumulated rounding error vs. the actual ledger state.
 *  3. Reduces the theoretical principal by `overpaymentComponents.trackedPrincipalDelta`.
 *  4. Calls `computeLoanProperties()` for the new schedule.
 *  5. Adds the preserved rounding errors back before re-rounding.
 *  6. Validates the result via `checkLoanGuards()`; rejects silently if invalid.
 *
 *  All mutations target local copies — no proxies are written. On success,
 *  `doOverpayment()` commits the result.
 *
 *  @param rules                   Active amendment rules.
 *  @param asset                   Loan asset for rounding.
 *  @param loanScale               Exponent for rounding all loan values.
 *  @param overpaymentComponents   Pre-computed payment components for the overpayment.
 *  @param roundedOldState         Current rounded loan state from the ledger.
 *  @param periodicPayment         Current periodic payment amount.
 *  @param periodicRate            Per-period interest rate.
 *  @param paymentRemaining        Number of payments still remaining.
 *  @param managementFeeRate       Broker fee rate in tenth-bips.
 *  @param j                       Journal for diagnostic logging.
 *  @return On success, a pair of `LoanPaymentParts` and the new `LoanProperties`.
 *      Returns `Unexpected(tesSUCCESS)` to signal "silently ignore the overpayment"
 *      (not an error), or `Unexpected(otherTER)` on a genuine failure.
 */
Expected<std::pair<LoanPaymentParts, LoanProperties>, TER>
tryOverpayment(
    Rules const& rules,
    Asset const& asset,
    std::int32_t loanScale,
    ExtendedPaymentComponents const& overpaymentComponents,
    LoanState const& roundedOldState,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    TenthBips16 const managementFeeRate,
    beast::Journal j)
{
    // Calculate what the loan state SHOULD be theoretically (at full precision)
    auto const theoreticalState = computeTheoreticalLoanState(
        rules, periodicPayment, periodicRate, paymentRemaining, managementFeeRate);

    // Calculate the accumulated rounding errors. These need to be preserved
    // across the re-amortization to maintain consistency with the loan's
    // payment history. Without preserving these errors, the loan could end
    // up with a different total value than what the borrower has actually paid.
    auto const errors = roundedOldState - theoreticalState;

    // Compute the new principal by applying the overpayment to the theoretical
    // principal. Use max with 0 to ensure we never go negative.
    auto const newTheoreticalPrincipal = std::max(
        theoreticalState.principalOutstanding - overpaymentComponents.trackedPrincipalDelta,
        Number{0});

    // Compute new loan properties based on the reduced principal. This
    // recalculates the periodic payment, total value, and management fees
    // for the remaining payment schedule.
    auto newLoanProperties = computeLoanProperties(
        rules,
        asset,
        newTheoreticalPrincipal,
        periodicRate,
        paymentRemaining,
        managementFeeRate,
        loanScale);

    JLOG(j.debug()) << "new periodic payment: " << newLoanProperties.periodicPayment
                    << ", new total value: " << newLoanProperties.loanState.valueOutstanding
                    << ", first payment principal: " << newLoanProperties.firstPaymentPrincipal;

    // Calculate what the new loan state should be with the new periodic payment
    // including rounding errors
    auto const newTheoreticalState = computeTheoreticalLoanState(
                                         rules,
                                         newLoanProperties.periodicPayment,
                                         periodicRate,
                                         paymentRemaining,
                                         managementFeeRate) +
        errors;

    JLOG(j.debug()) << "new theoretical value: " << newTheoreticalState.valueOutstanding
                    << ", principal: " << newTheoreticalState.principalOutstanding
                    << ", interest gross: " << newTheoreticalState.interestOutstanding();

    // Update the loan state variables with the new values that include the
    // preserved rounding errors. This ensures the loan's tracked state remains
    // consistent with its payment history.
    auto const principalOutstanding = std::clamp(
        roundToAsset(
            asset,
            newTheoreticalState.principalOutstanding,
            loanScale,
            Number::RoundingMode::Upward),
        kNUM_ZERO,
        roundedOldState.principalOutstanding);
    auto const totalValueOutstanding = std::clamp(
        roundToAsset(
            asset,
            principalOutstanding + newTheoreticalState.interestOutstanding(),
            loanScale,
            Number::RoundingMode::Upward),
        kNUM_ZERO,
        roundedOldState.valueOutstanding);
    auto const managementFeeOutstanding = std::clamp(
        roundToAsset(asset, newTheoreticalState.managementFeeDue, loanScale),
        kNUM_ZERO,
        roundedOldState.managementFeeDue);

    auto const roundedNewState =
        constructLoanState(totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    // Update newLoanProperties so that checkLoanGuards can make an accurate
    // evaluation.
    newLoanProperties.loanState = roundedNewState;

    JLOG(j.debug()) << "new rounded value: " << roundedNewState.valueOutstanding
                    << ", principal: " << roundedNewState.principalOutstanding
                    << ", interest gross: " << roundedNewState.interestOutstanding();

    // check that the loan is still valid
    if (auto const ter = checkLoanGuards(
            asset,
            principalOutstanding,
            // The loan may have been created with interest, but for
            // small interest amounts, that may have already been paid
            // off. Check what's still outstanding. This should
            // guarantee that the interest checks pass.
            roundedNewState.interestOutstanding() != beast::kZERO,
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
        newLoanProperties.loanState.valueOutstanding <= 0 ||
        newLoanProperties.loanState.managementFeeDue < 0)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Overpayment not allowed: Computed loan "
                          "properties are invalid. Does "
                          "not compute. TotalValueOutstanding: "
                       << newLoanProperties.loanState.valueOutstanding
                       << ", PeriodicPayment : " << newLoanProperties.periodicPayment
                       << ", ManagementFeeOwedToBroker: "
                       << newLoanProperties.loanState.managementFeeDue;
        return Unexpected(tesSUCCESS);
        // LCOV_EXCL_STOP
    }

    auto const deltas = roundedOldState - roundedNewState;

    // The change in loan management fee is equal to the change between the old
    // and the new outstanding management fees
    XRPL_ASSERT_PARTS(
        deltas.managementFee == roundedOldState.managementFeeDue - managementFeeOutstanding,
        "xrpl::detail::tryOverpayment",
        "no fee change");

    // Calculate how the loan's value changed due to the overpayment.
    // This should be negative (value decreased) or zero. A principal
    // overpayment should never increase the loan's value.
    // The value change is derived from the reduction in interest due to
    // the lower principal.
    // We do not consider the change in management fee here, since
    // management fees are excluded from the valueOutstanding.
    auto const valueChange = -deltas.interest;
    if (valueChange > 0)
    {
        JLOG(j.warn()) << "Principal overpayment would increase the value of "
                          "the loan. Ignore the overpayment";
        return Unexpected(tesSUCCESS);
    }

    return std::make_pair(
        LoanPaymentParts{
            // Principal paid is the reduction in principal outstanding
            .principalPaid = deltas.principal,
            // Interest paid is the reduction in interest due
            .interestPaid = overpaymentComponents.untrackedInterest,
            // Value change includes both the reduction from paying down
            // principal (negative) and any untracked interest penalties
            // (positive, e.g., if the overpayment itself incurs a fee)
            .valueChange = valueChange + overpaymentComponents.untrackedInterest,
            // Fee paid includes both the reduction in tracked management fees
            // and any untracked fees on the overpayment itself
            .feePaid = overpaymentComponents.untrackedManagementFee +
                overpaymentComponents.trackedManagementFeeDelta,
        },
        newLoanProperties);
}

/** Validate and commit a principal overpayment to the loan ledger object.
 *
 *  Wraps `tryOverpayment()` in a two-phase pattern: the sandbox calculation
 *  runs first against local copies of the loan state. Only after all guard
 *  conditions pass — including that the principal strictly decreased — are the
 *  proxy objects updated with the new balances and periodic payment.
 *
 *  Returns `Unexpected(tesSUCCESS)` when `tryOverpayment` rejects the overpayment
 *  silently (invalid state, zero principal reduction, etc.), propagating the
 *  signal up to `loanMakePayment()` which continues without the overpayment step.
 *
 *  @tparam NumberProxy                  Type exposing `Number` read/write semantics.
 *  @param rules                         Active amendment rules.
 *  @param asset                         Loan asset for rounding.
 *  @param loanScale                     Exponent for rounding.
 *  @param overpaymentComponents         Pre-computed overpayment components.
 *  @param totalValueOutstandingProxy    Proxy for `sfTotalValueOutstanding`.
 *  @param principalOutstandingProxy     Proxy for `sfPrincipalOutstanding`.
 *  @param managementFeeOutstandingProxy Proxy for `sfManagementFeeOutstanding`.
 *  @param periodicPaymentProxy          Proxy for `sfPeriodicPayment`.
 *  @param periodicRate                  Per-period interest rate.
 *  @param paymentRemaining              Remaining payment count.
 *  @param managementFeeRate             Broker fee rate in tenth-bips.
 *  @param j                             Journal for diagnostic logging.
 *  @return Payment parts on success; `Unexpected(TER)` on failure or silent skip.
 */
template <class NumberProxy>
Expected<LoanPaymentParts, TER>
doOverpayment(
    Rules const& rules,
    Asset const& asset,
    std::int32_t loanScale,
    ExtendedPaymentComponents const& overpaymentComponents,
    NumberProxy& totalValueOutstandingProxy,
    NumberProxy& principalOutstandingProxy,
    NumberProxy& managementFeeOutstandingProxy,
    NumberProxy& periodicPaymentProxy,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips16 const managementFeeRate,
    beast::Journal j)
{
    auto const loanState = constructLoanState(
        totalValueOutstandingProxy, principalOutstandingProxy, managementFeeOutstandingProxy);
    auto const periodicPayment = periodicPaymentProxy;
    JLOG(j.debug()) << "overpayment components:"
                    << ", totalValue before: " << *totalValueOutstandingProxy
                    << ", valueDelta: " << overpaymentComponents.trackedValueDelta
                    << ", principalDelta: " << overpaymentComponents.trackedPrincipalDelta
                    << ", managementFeeDelta: " << overpaymentComponents.trackedManagementFeeDelta
                    << ", interestPart: " << overpaymentComponents.trackedInterestPart()
                    << ", untrackedInterest: " << overpaymentComponents.untrackedInterest
                    << ", totalDue: " << overpaymentComponents.totalDue
                    << ", payments remaining :" << paymentRemaining;

    // Attempt to re-amortize the loan with the overpayment applied.
    // This modifies the temporary copies, leaving the proxies unchanged.
    auto const ret = tryOverpayment(
        rules,
        asset,
        loanScale,
        overpaymentComponents,
        loanState,
        periodicPayment,
        periodicRate,
        paymentRemaining,
        managementFeeRate,
        j);
    if (!ret)
        return Unexpected(ret.error());

    auto const& [loanPaymentParts, newLoanProperties] = *ret;
    auto const newRoundedLoanState = newLoanProperties.loanState;

    // Safety check: the principal must have decreased. If it didn't (or
    // increased!), something went wrong in the calculation and we should
    // reject the overpayment.
    if (principalOutstandingProxy <= newRoundedLoanState.principalOutstanding)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Overpayment not allowed: principal "
                       << "outstanding did not decrease. Before: " << *principalOutstandingProxy
                       << ". After: " << newRoundedLoanState.principalOutstanding;
        return Unexpected(tesSUCCESS);
        // LCOV_EXCL_STOP
    }

    // The proxies still hold the original (pre-overpayment) values, which
    // allows us to compute deltas and verify they match what we expect
    // from the overpaymentComponents and loanPaymentParts.

    XRPL_ASSERT_PARTS(
        overpaymentComponents.trackedPrincipalDelta ==
            principalOutstandingProxy - newRoundedLoanState.principalOutstanding,
        "xrpl::detail::doOverpayment",
        "principal change agrees");

    // I'm not 100% sure the following asserts are correct. If in doubt, and
    // everything else works, remove any that cause trouble.

    JLOG(j.debug()) << "valueChange: " << loanPaymentParts.valueChange
                    << ", totalValue before: " << *totalValueOutstandingProxy
                    << ", totalValue after: " << newRoundedLoanState.valueOutstanding
                    << ", totalValue delta: "
                    << (totalValueOutstandingProxy - newRoundedLoanState.valueOutstanding)
                    << ", principalDelta: " << overpaymentComponents.trackedPrincipalDelta
                    << ", principalPaid: " << loanPaymentParts.principalPaid
                    << ", Computed difference: "
                    << overpaymentComponents.trackedPrincipalDelta -
            (totalValueOutstandingProxy - newRoundedLoanState.valueOutstanding);

    XRPL_ASSERT_PARTS(
        loanPaymentParts.valueChange ==
            newRoundedLoanState.valueOutstanding -
                (totalValueOutstandingProxy - overpaymentComponents.trackedPrincipalDelta) +
                overpaymentComponents.trackedInterestPart(),
        "xrpl::detail::doOverpayment",
        "interest paid agrees");

    XRPL_ASSERT_PARTS(
        overpaymentComponents.trackedPrincipalDelta == loanPaymentParts.principalPaid,
        "xrpl::detail::doOverpayment",
        "principal payment matches");

    // All validations passed, so update the proxy objects (which will
    // modify the actual Loan ledger object)
    totalValueOutstandingProxy = newRoundedLoanState.valueOutstanding;
    principalOutstandingProxy = newRoundedLoanState.principalOutstanding;
    managementFeeOutstandingProxy = newRoundedLoanState.managementFeeDue;
    periodicPaymentProxy = newLoanProperties.periodicPayment;

    return loanPaymentParts;
}

/** Compute payment components for a payment made after the due date.
 *
 *  Extends the regular `periodic` components with two extra untracked amounts:
 *  - Late penalty interest (`loanLatePaymentInterest()`), which increases the
 *    loan's total value (`valueChange > 0`).
 *  - A fixed late payment fee charged by the broker.
 *
 *  Both are split by `computeInterestAndFeeParts()` before being added.
 *  Implements Equation (15) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param asset              Loan asset for rounding.
 *  @param view               Apply view supplying `parentCloseTime` and expiry check.
 *  @param principalOutstanding Current outstanding principal.
 *  @param nextDueDate        The payment's scheduled due date.
 *  @param periodic           Pre-computed regular periodic payment components.
 *  @param lateInterestRate   Annualized penalty rate in tenth-of-a-basis-point units.
 *  @param loanScale          Exponent for rounding.
 *  @param latePaymentFee     Fixed broker fee for a late payment.
 *  @param amount             Amount the borrower offered; must cover `late.totalDue`.
 *  @param managementFeeRate  Broker fee rate for splitting the late interest.
 *  @param j                  Journal for diagnostic logging.
 *  @return Extended components including late penalty on success;
 *      `Unexpected(tecTOO_SOON)` if the due date has not yet passed;
 *      `Unexpected(tecINSUFFICIENT_PAYMENT)` if `amount < late.totalDue`.
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
        principalOutstanding, lateInterestRate, view.parentCloseTime(), nextDueDate);

    // Round the late interest and split it between the vault (net interest)
    // and the broker (management fee portion). This lambda ensures we
    // round before splitting to maintain precision.
    auto const [roundedLateInterest, roundedLateManagementFee] = [&]() {
        auto const interest = roundToAsset(asset, latePaymentInterest, loanScale);
        return computeInterestAndFeeParts(asset, interest, managementFeeRate, loanScale);
    }();

    XRPL_ASSERT(roundedLateInterest >= 0, "xrpl::detail::computeLatePayment : valid late interest");
    XRPL_ASSERT_PARTS(
        periodic.specialCase != PaymentSpecialCase::Extra,
        "xrpl::detail::computeLatePayment",
        "no extra parts to this payment");

    // Create the late payment components by copying the regular periodic
    // payment and adding the late penalties. We use a lambda to construct
    // this to keep the logic clear. This preserves all the other fields without
    // having to enumerate them.

    ExtendedPaymentComponents const late{
        periodic,
        // Untracked management fee includes:
        // 1. Regular service fee (from periodic.untrackedManagementFee)
        // 2. Late payment fee (fixed penalty)
        // 3. Management fee portion of late interest
        periodic.untrackedManagementFee + latePaymentFee + roundedLateManagementFee,

        // Untracked interest includes:
        // 1. Any untracked interest from the regular payment (usually 0)
        // 2. Late penalty interest (increases loan value)
        // This positive value indicates the loan's value increased due
        // to the late payment.
        periodic.untrackedInterest + roundedLateInterest};

    XRPL_ASSERT_PARTS(
        isRounded(asset, late.totalDue, loanScale),
        "xrpl::detail::computeLatePayment",
        "total due is rounded");

    // Check that the borrower provided enough funds to cover the late payment.
    // The late payment is more expensive than a regular payment due to the
    // penalties.
    if (amount < late.totalDue)
    {
        JLOG(j.warn()) << "Late loan payment amount is insufficient. Due: " << late.totalDue
                       << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    return late;
}

/** Compute payment components for early loan closure (before the final scheduled payment).
 *
 *  Disallowed when only one payment remains — the final scheduled payment
 *  should follow the regular path instead. Pays off all remaining balances
 *  (`trackedValueDelta = principal + interest + fee`) marked `PaymentSpecialCase::Final`,
 *  plus two untracked charges:
 *  - Accrued interest since the last payment (`loanAccruedInterest()`), Eq. 27.
 *  - Prepayment penalty (`closeInterestRate` applied to theoretical principal), Eq. 28.
 *
 *  `untrackedInterest = roundedFullInterest - totalInterestOutstanding`; this
 *  drives `LoanPaymentParts::valueChange` and can be negative (early payoff saves
 *  more interest than the penalty costs). Implements Equation (26) from XLS-66,
 *  Section A-2.
 *
 *  @param asset                   Loan asset for rounding.
 *  @param view                    Apply view supplying `parentCloseTime` and `rules`.
 *  @param principalOutstanding    Current outstanding principal.
 *  @param managementFeeOutstanding Current outstanding management fee.
 *  @param periodicPayment         Current fixed installment amount.
 *  @param paymentRemaining        Remaining payment count; must be > 1.
 *  @param prevPaymentDate         Due date of the most recently completed payment.
 *  @param startDate               Loan start date (for accrued-interest calculation).
 *  @param paymentInterval         Payment period length in seconds.
 *  @param closeInterestRate       Prepayment penalty rate in tenth-bips.
 *  @param loanScale               Exponent for rounding.
 *  @param totalInterestOutstanding Total interest still due on the loan.
 *  @param periodicRate            Per-period interest rate.
 *  @param closePaymentFee         Fixed broker fee for early closure.
 *  @param amount                  Amount the borrower offered; must cover `full.totalDue`.
 *  @param managementFeeRate       Broker fee rate for splitting the full-payment interest.
 *  @param j                       Journal for diagnostic logging.
 *  @return Extended components on success;
 *      `Unexpected(tecKILLED)` if `paymentRemaining <= 1`;
 *      `Unexpected(tecINSUFFICIENT_PAYMENT)` if `amount < full.totalDue`.
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
    // This theoretical (unrounded) value is used to compute interest and
    // penalties accurately.
    Number const theoreticalPrincipalOutstanding = loanPrincipalFromPeriodicPayment(
        view.rules(), periodicPayment, periodicRate, paymentRemaining);

    // Full payment interest includes both accrued interest (time since last
    // payment) and prepayment penalty (for closing early).
    auto const fullPaymentInterest = computeFullPaymentInterest(
        theoreticalPrincipalOutstanding,
        periodicRate,
        view.parentCloseTime(),
        paymentInterval,
        prevPaymentDate,
        startDate,
        closeInterestRate);

    // Split the full payment interest into net interest (to vault) and
    // management fee (to broker), applying proper rounding.
    auto const [roundedFullInterest, roundedFullManagementFee] = [&]() {
        auto const interest =
            roundToAsset(asset, fullPaymentInterest, loanScale, Number::RoundingMode::Downward);
        return computeInterestAndFeeParts(asset, interest, managementFeeRate, loanScale);
    }();

    ExtendedPaymentComponents const full{
        PaymentComponents{
            // Pay off all tracked outstanding balances: principal, interest,
            // and fees.
            // This marks the loan as complete (final payment).
            .trackedValueDelta =
                principalOutstanding + totalInterestOutstanding + managementFeeOutstanding,
            .trackedPrincipalDelta = principalOutstanding,

            // All outstanding management fees are paid. This zeroes out the
            // tracked fee balance.
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::Final,
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
        "xrpl::detail::computeFullPayment",
        "total due is rounded");

    JLOG(j.trace()) << "computeFullPayment result: periodicPayment: " << periodicPayment
                    << ", periodicRate: " << periodicRate
                    << ", paymentRemaining: " << paymentRemaining
                    << ", theoreticalPrincipalOutstanding: " << theoreticalPrincipalOutstanding
                    << ", fullPaymentInterest: " << fullPaymentInterest
                    << ", roundedFullInterest: " << roundedFullInterest
                    << ", roundedFullManagementFee: " << roundedFullManagementFee
                    << ", untrackedInterest: " << full.untrackedInterest;

    if (amount < full.totalDue)
    {
        // If the payment is less than the full payment amount, it's not
        // sufficient to be a full payment.
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    return full;
}

/** Derive the tracked interest portion of this payment.
 *
 *  Computed as `trackedValueDelta - trackedPrincipalDelta - trackedManagementFeeDelta`,
 *  representing the net interest paid to the vault from the scheduled amortization.
 *  Untracked interest (e.g., late penalty interest) is not included here.
 *
 *  @return The tracked interest component as a `Number`.
 */
Number
PaymentComponents::trackedInterestPart() const
{
    return trackedValueDelta - (trackedPrincipalDelta + trackedManagementFeeDelta);
}

/** Compute how a single scheduled payment splits into principal, interest, and fee.
 *
 *  Rather than recomputing from the amortization formula, this function asks
 *  "what should the loan state be after this payment?" by calling
 *  `computeTheoreticalLoanState(paymentRemaining - 1)` and taking the delta
 *  between the current ledger state and that target. This naturally absorbs
 *  accumulated rounding errors. After computing raw deltas the function applies
 *  `nonNegative()` and a series of `std::min` caps to ensure no component
 *  exceeds its available balance or the rounded periodic payment. Excess is
 *  redistributed by the `addressExcess` lambda (interest first, then fee, then
 *  principal). Implements `compute_payment_due()` from XLS-66 §3.2.4.4.
 *
 *  @param rules                   Active amendment rules.
 *  @param asset                   Loan asset for rounding.
 *  @param scale                   Exponent for rounding.
 *  @param totalValueOutstanding   Current `sfTotalValueOutstanding`.
 *  @param principalOutstanding    Current `sfPrincipalOutstanding`.
 *  @param managementFeeOutstanding Current `sfManagementFeeOutstanding`.
 *  @param periodicPayment         Current scheduled installment (unrounded).
 *  @param periodicRate            Per-period interest rate.
 *  @param paymentRemaining        Number of payments remaining; must be > 0.
 *  @param managementFeeRate       Broker fee rate in tenth-bips.
 *  @return `PaymentComponents` with `specialCase = Final` if this is the last
 *      payment or if `totalValueOutstanding <= roundedPeriodicPayment`.
 */
PaymentComponents
computePaymentComponents(
    Rules const& rules,
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
        "xrpl::detail::computePaymentComponents",
        "Outstanding values are rounded");
    XRPL_ASSERT_PARTS(
        paymentRemaining > 0, "xrpl::detail::computePaymentComponents", "some payments remaining");

    auto const roundedPeriodicPayment = roundPeriodicPayment(asset, periodicPayment, scale);

    // Final payment: pay off everything remaining, ignoring the normal
    // periodic payment amount. This ensures the loan completes cleanly.
    if (paymentRemaining == 1 || totalValueOutstanding <= roundedPeriodicPayment)
    {
        // If there's only one payment left, we need to pay off each of the loan
        // parts.
        return PaymentComponents{
            .trackedValueDelta = totalValueOutstanding,
            .trackedPrincipalDelta = principalOutstanding,
            .trackedManagementFeeDelta = managementFeeOutstanding,
            .specialCase = PaymentSpecialCase::Final};
    }

    // Calculate what the loan state SHOULD be after this payment (the target).
    // This is computed at full precision using the theoretical amortization.
    LoanState const trueTarget = computeTheoreticalLoanState(
        rules, periodicPayment, periodicRate, paymentRemaining - 1, managementFeeRate);

    // Round the target to the loan's scale to match how actual loan values
    // are stored.
    LoanState const roundedTarget = LoanState{
        .valueOutstanding = roundToAsset(asset, trueTarget.valueOutstanding, scale),
        .principalOutstanding = roundToAsset(asset, trueTarget.principalOutstanding, scale),
        .interestDue = roundToAsset(asset, trueTarget.interestDue, scale),
        .managementFeeDue = roundToAsset(asset, trueTarget.managementFeeDue, scale)};

    // Get the current actual loan state from the ledger values
    LoanState const currentLedgerState =
        constructLoanState(totalValueOutstanding, principalOutstanding, managementFeeOutstanding);

    // The difference between current and target states gives us the payment
    // components. Any discrepancies from accumulated rounding are captured
    // here.

    LoanStateDeltas deltas = currentLedgerState - roundedTarget;

    // Rounding can occasionally produce negative deltas. Zero them out.
    deltas.nonNegative();

    XRPL_ASSERT_PARTS(
        deltas.principal <= currentLedgerState.principalOutstanding,
        "xrpl::detail::computePaymentComponents",
        "principal delta not greater than outstanding");

    // Cap each component to never exceed what's actually outstanding
    deltas.principal = std::min(deltas.principal, currentLedgerState.principalOutstanding);

    XRPL_ASSERT_PARTS(
        deltas.interest <= currentLedgerState.interestDue,
        "xrpl::detail::computePaymentComponents",
        "interest due delta not greater than outstanding");

    // Cap interest to both the outstanding amount AND what's left of the
    // periodic payment after principal is paid
    deltas.interest = std::min(
        {deltas.interest,
         std::max(kNUM_ZERO, roundedPeriodicPayment - deltas.principal),
         currentLedgerState.interestDue});

    XRPL_ASSERT_PARTS(
        deltas.managementFee <= currentLedgerState.managementFeeDue,
        "xrpl::detail::computePaymentComponents",
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
        if (excess > beast::kZERO)
        {
            auto part = std::min(component, excess);
            component -= part;
            excess -= part;
        }
        XRPL_ASSERT_PARTS(
            excess >= beast::kZERO,
            "xrpl::detail::computePaymentComponents",
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
    Number totalOverpayment = deltas.total() - currentLedgerState.valueOutstanding;

    if (totalOverpayment > beast::kZERO)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::detail::computePaymentComponents : payment exceeded loan "
            "state");
        addressExcess(deltas, totalOverpayment);
        // LCOV_EXCL_STOP
    }

    // Check if deltas exceed the periodic payment amount. Reduce if needed.
    Number shortage = roundedPeriodicPayment - deltas.total();

    XRPL_ASSERT_PARTS(
        isRounded(asset, shortage, scale),
        "xrpl::detail::computePaymentComponents",
        "shortage is rounded");

    if (shortage < beast::kZERO)
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
        shortage >= beast::kZERO,
        "xrpl::detail::computePaymentComponents",
        "no shortage or excess");

    // Final validation that all components are valid
    XRPL_ASSERT_PARTS(
        deltas.total() == deltas.principal + deltas.interest + deltas.managementFee,
        "xrpl::detail::computePaymentComponents",
        "total value adds up");

    XRPL_ASSERT_PARTS(
        deltas.principal >= beast::kZERO &&
            deltas.principal <= currentLedgerState.principalOutstanding,
        "xrpl::detail::computePaymentComponents",
        "valid principal result");
    XRPL_ASSERT_PARTS(
        deltas.interest >= beast::kZERO && deltas.interest <= currentLedgerState.interestDue,
        "xrpl::detail::computePaymentComponents",
        "valid interest result");
    XRPL_ASSERT_PARTS(
        deltas.managementFee >= beast::kZERO &&
            deltas.managementFee <= currentLedgerState.managementFeeDue,
        "xrpl::detail::computePaymentComponents",
        "valid fee result");

    XRPL_ASSERT_PARTS(
        deltas.principal + deltas.interest + deltas.managementFee > beast::kZERO,
        "xrpl::detail::computePaymentComponents",
        "payment parts add to payment");

    // Final safety clamp to ensure no value exceeds its outstanding balance
    return PaymentComponents{
        .trackedValueDelta =
            std::clamp(deltas.total(), kNUM_ZERO, currentLedgerState.valueOutstanding),
        .trackedPrincipalDelta =
            std::clamp(deltas.principal, kNUM_ZERO, currentLedgerState.principalOutstanding),
        .trackedManagementFeeDelta =
            std::clamp(deltas.managementFee, kNUM_ZERO, currentLedgerState.managementFeeDue),
    };
}

/** Compute payment components for a principal overpayment.
 *
 *  An overpayment pays more than the scheduled installment; the surplus reduces
 *  principal immediately but incurs a fixed fee and a one-time penalty interest
 *  charge. The decomposition (XLS-66 §3.2.4.2.3, Equations 20-22):
 *
 *  1. `overpaymentFee = round(overpayment * overpaymentFeeRate)` (Eq. 22).
 *  2. Gross penalty interest on the full overpayment, split into net interest
 *     and management fee via `computeInterestAndFeeParts()` (Eqs. 20-21).
 *  3. `trackedPrincipalDelta = overpayment - grossInterest - overpaymentFee`.
 *
 *  The result is tagged `PaymentSpecialCase::Extra` so `doPayment()` knows not
 *  to advance the payment schedule.
 *
 *  @param asset                  Loan asset for rounding.
 *  @param loanScale              Exponent for rounding all components.
 *  @param overpayment            Amount being overpaid; must be > 0 and already rounded.
 *  @param overpaymentInterestRate One-time penalty rate applied to the overpayment, in tenth-bips.
 *  @param overpaymentFeeRate     Fixed broker fee rate on the overpayment, in tenth-bips.
 *  @param managementFeeRate      Broker's share of the penalty interest, in tenth-bips.
 *  @return `ExtendedPaymentComponents` with `specialCase = Extra`.
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
        "xrpl::detail::computeOverpaymentComponents : valid overpayment "
        "amount");

    // First, deduct the fixed overpayment fee from the total amount.
    // This reduces the effective payment that will be applied to the loan.
    // Equation (22) from XLS-66 spec, Section A-2 Equation Glossary
    Number const overpaymentFee =
        roundToAsset(asset, tenthBipsOfValue(overpayment, overpaymentFeeRate), loanScale);

    // Calculate the penalty interest on the effective payment amount.
    // This interest doesn't follow the normal amortization schedule - it's
    // a one-time charge for paying early.
    // Equation (20) and (21) from XLS-66 spec, Section A-2 Equation Glossary
    auto const [roundedOverpaymentInterest, roundedOverpaymentManagementFee] = [&]() {
        auto const interest =
            roundToAsset(asset, tenthBipsOfValue(overpayment, overpaymentInterestRate), loanScale);
        return detail::computeInterestAndFeeParts(asset, interest, managementFeeRate, loanScale);
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
            .specialCase = detail::PaymentSpecialCase::Extra},
        // Untracked management fee is the fixed overpayment fee
        overpaymentFee,
        // Untracked interest is the penalty interest charged for  overpaying.
        // This is positive, representing a one-time cost, but it's typically
        // much smaller than the interest savings from reducing principal.
        // It is equal to the paymentComponents.trackedInterestPart()
        // but is kept separate for clarity.
        roundedOverpaymentInterest};
    XRPL_ASSERT_PARTS(
        result.trackedInterestPart() == roundedOverpaymentInterest,
        "xrpl::detail::computeOverpaymentComponents",
        "valid interest computation");
    return result;
}

}  // namespace detail

/** Compute the component-wise difference between two loan states.
 *
 *  Used to measure accumulated rounding error between the current rounded
 *  ledger state and the theoretical state, and to compute payment deltas during
 *  re-amortization. The resulting `LoanStateDeltas` does not include a
 *  `valueOutstanding` delta; callers derive it via `LoanStateDeltas::total()`.
 *
 *  @param lhs  The minuend loan state (typically the current rounded state).
 *  @param rhs  The subtrahend loan state (typically the theoretical target).
 *  @return Component-wise deltas `lhs - rhs`.
 */
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

/** Subtract `LoanStateDeltas` from a `LoanState`.
 *
 *  Used to apply payment deltas to a loan state, producing the post-payment
 *  state. `valueOutstanding` is adjusted by `rhs.total()`.
 *
 *  @param lhs  The base loan state.
 *  @param rhs  The deltas to subtract.
 *  @return New `LoanState` with each field reduced by the corresponding delta.
 */
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

/** Add `LoanStateDeltas` to a `LoanState`.
 *
 *  Used by `tryOverpayment()` to re-apply preserved rounding errors to the
 *  newly re-amortized theoretical state before rounding to the loan scale.
 *
 *  @param lhs  The base loan state.
 *  @param rhs  The deltas to add.
 *  @return New `LoanState` with each field increased by the corresponding delta.
 */
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

/** Validate that computed loan properties satisfy precision and amortization invariants.
 *
 *  Enforces four guards in sequence, each returning `tecPRECISION_LOSS` on violation:
 *  1. If `expectInterest`, total interest over the loan's life must be a measurable
 *     positive value; if not, the amortization table is meaningless.
 *  2. The first-payment's principal share (`properties.firstPaymentPrincipal`) must
 *     be positive at full precision — if it rounds to zero the principal can never
 *     be paid down.
 *  3. The rounded periodic payment must not be zero (prevents division-by-zero in
 *     downstream calculations).
 *  4. `floor(totalValue / roundedPayment)` must equal `paymentTotal`, ensuring the
 *     loan will complete in exactly the specified number of installments.
 *
 *  Called from loan creation (`LoanSet`) and after each overpayment re-amortization.
 *
 *  @param vaultAsset          Asset used for rounding the periodic payment.
 *  @param principalRequested  Loan principal, used to compute total interest outstanding.
 *  @param expectInterest      `true` if the loan has a non-zero interest rate.
 *  @param paymentTotal        Total number of scheduled payments.
 *  @param properties          Computed loan properties to validate.
 *  @param j                   Journal for diagnostic logging.
 *  @return `tesSUCCESS` if all guards pass; `tecPRECISION_LOSS` or `tecINTERNAL`
 *      on violation.
 */
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
        properties.loanState.valueOutstanding - principalRequested;
    // Guard 1: if there is no computed total interest over the life of the
    // loan for a non-zero interest rate, we cannot properly amortize the
    // loan
    if (expectInterest && totalInterestOutstanding <= 0)
    {
        // Unless this is a zero-interest loan, there must be some interest
        // due on the loan, even if it's (measurable) dust
        JLOG(j.warn()) << "Loan for " << principalRequested << " with interest has no interest due";
        return tecPRECISION_LOSS;
    }
    // Guard 1a: If there is any interest computed over the life of the
    // loan, for a zero interest rate, something went sideways.
    if (!expectInterest && totalInterestOutstanding > 0)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Loan for " << principalRequested << " with no interest has interest due";
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
    auto const roundedPayment =
        roundPeriodicPayment(vaultAsset, properties.periodicPayment, properties.loanScale);
    if (roundedPayment == beast::kZERO)
    {
        JLOG(j.warn()) << "Loan Periodic payment (" << properties.periodicPayment
                       << ") rounds to 0. ";
        return tecPRECISION_LOSS;
    }

    // Guard 4: if the rounded periodic payment is large enough that the
    // loan can't be amortized in the specified number of payments, raise an
    // error
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);

        if (std::int64_t const computedPayments{
                properties.loanState.valueOutstanding / roundedPayment};
            computedPayments != paymentTotal)
        {
            JLOG(j.warn()) << "Loan Periodic payment (" << properties.periodicPayment
                           << ") rounding (" << roundedPayment << ") on a total value of "
                           << properties.loanState.valueOutstanding
                           << " can not complete the loan in the specified "
                              "number of payments ("
                           << computedPayments << " != " << paymentTotal << ")";
            return tecPRECISION_LOSS;
        }
    }
    return tesSUCCESS;
}

/** Compute the total interest charge for an early full payment.
 *
 *  Sums two components:
 *  - Accrued interest since the last payment (`loanAccruedInterest()`), Eq. 27.
 *  - Prepayment penalty (`closeInterestRate` applied to the theoretical principal
 *    outstanding), Eq. 28. Zero when `closeInterestRate == 0`.
 *
 *  @param theoreticalPrincipalOutstanding Unrounded principal derived from the payment schedule.
 *  @param periodicRate      Per-period interest rate.
 *  @param parentCloseTime   Close time of the parent ledger.
 *  @param paymentInterval   Payment period length in seconds.
 *  @param prevPaymentDate   Due date of the most recently completed payment.
 *  @param startDate         Loan start date (for accrued-interest calculation).
 *  @param closeInterestRate Prepayment penalty rate in tenth-of-a-basis-point units.
 *  @return `accruedInterest + prepaymentPenalty`, both non-negative.
 */
Number
computeFullPaymentInterest(
    Number const& theoreticalPrincipalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate)
{
    auto const accruedInterest = detail::loanAccruedInterest(
        theoreticalPrincipalOutstanding,
        periodicRate,
        parentCloseTime,
        startDate,
        prevPaymentDate,
        paymentInterval);
    XRPL_ASSERT(
        accruedInterest >= 0,
        "xrpl::detail::computeFullPaymentInterest : valid accrued "
        "interest");

    // Equation (28) from XLS-66 spec, Section A-2 Equation Glossary
    auto const prepaymentPenalty = closeInterestRate == beast::kZERO
        ? Number{}
        : tenthBipsOfValue(theoreticalPrincipalOutstanding, closeInterestRate);

    XRPL_ASSERT(
        prepaymentPenalty >= 0,
        "xrpl::detail::computeFullPaymentInterest : valid prepayment "
        "interest");

    // Part of equation (27) from XLS-66 spec, Section A-2 Equation Glossary
    return accruedInterest + prepaymentPenalty;
}

/** Compute the theoretically correct loan state at full arithmetic precision.
 *
 *  Derives what each outstanding balance *should be* purely from the payment
 *  schedule, without any ledger-rounding effects. Used as a target state in
 *  `computePaymentComponents()` and `tryOverpayment()` to measure and correct
 *  accumulated rounding drift. Implements `calculate_true_loan_state` from
 *  XLS-66 §3.2.4.4. Equations 30-33 from Section A-2.
 *
 *  @param rules             Active amendment rules (passed to `loanPrincipalFromPeriodicPayment`).
 *  @param periodicPayment   Fixed installment amount.
 *  @param periodicRate      Per-period interest rate.
 *  @param paymentRemaining  Number of payments still remaining after this point.
 *  @param managementFeeRate Broker fee rate in tenth-bips.
 *  @return Unrounded `LoanState`, or a fully-zeroed state if `paymentRemaining == 0`.
 */
LoanState
computeTheoreticalLoanState(
    Rules const& rules,
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
    Number const totalValueOutstanding = periodicPayment * paymentRemaining;

    Number const principalOutstanding = detail::loanPrincipalFromPeriodicPayment(
        rules, periodicPayment, periodicRate, paymentRemaining);

    // Equation (31) from XLS-66 spec, Section A-2 Equation Glossary
    Number const interestOutstandingGross = totalValueOutstanding - principalOutstanding;

    // Equation (32) from XLS-66 spec, Section A-2 Equation Glossary
    Number const managementFeeOutstanding =
        tenthBipsOfValue(interestOutstandingGross, managementFeeRate);

    // Equation (33) from XLS-66 spec, Section A-2 Equation Glossary
    Number const interestOutstandingNet = interestOutstandingGross - managementFeeOutstanding;

    return LoanState{
        .valueOutstanding = totalValueOutstanding,
        .principalOutstanding = principalOutstanding,
        .interestDue = interestOutstandingNet,
        .managementFeeDue = managementFeeOutstanding,
    };
};

/** Build a `LoanState` from the three directly-tracked loan balances.
 *
 *  Derives `interestDue = totalValueOutstanding - principalOutstanding - managementFeeOutstanding`
 *  rather than accepting it as a parameter, ensuring the LoanState invariant
 *  `interestDue + managementFeeDue == valueOutstanding - principalOutstanding`
 *  always holds. Use `computeTheoreticalLoanState()` when working at full
 *  arithmetic precision; use this function when working from rounded ledger values.
 *
 *  @param totalValueOutstanding    Total value still owed by the borrower.
 *  @param principalOutstanding     Principal component still outstanding.
 *  @param managementFeeOutstanding Management fee component still outstanding.
 *  @return Consistent `LoanState` with `interestDue` derived from the other fields.
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
        .interestDue = totalValueOutstanding - principalOutstanding - managementFeeOutstanding,
        .managementFeeDue = managementFeeOutstanding};
}

/** Build a `LoanState` directly from a Loan ledger entry's stored fields.
 *
 *  Convenience wrapper that reads `sfTotalValueOutstanding`,
 *  `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` from the SLE
 *  and delegates to `constructLoanState()`.
 *
 *  @param loan  A const reference to the Loan SLE.
 *  @return `LoanState` reflecting the current rounded ledger values.
 */
LoanState
constructRoundedLoanState(SLE::const_ref loan)
{
    return constructLoanState(
        loan->at(sfTotalValueOutstanding),
        loan->at(sfPrincipalOutstanding),
        loan->at(sfManagementFeeOutstanding));
}

/** Compute the broker's management fee on a given interest amount.
 *
 *  Calculates `roundDown(tenthBipsOfValue(value, managementFeeRate), scale)`.
 *  Downward rounding ensures the vault never receives less than its share.
 *  Implements Equation (32) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param asset             Asset used to constrain rounding.
 *  @param value             Gross interest amount from which the fee is taken.
 *  @param managementFeeRate Broker's rate in tenth-of-a-basis-point units.
 *  @param scale             Exponent for rounding the result downward.
 *  @return Broker fee, rounded down to the loan scale.
 */
Number
computeManagementFee(
    Asset const& asset,
    Number const& value,
    TenthBips32 managementFeeRate,
    std::int32_t scale)
{
    return roundToAsset(
        asset, tenthBipsOfValue(value, managementFeeRate), scale, Number::RoundingMode::Downward);
}

/** Compute all derived loan properties from the raw input parameters.
 *
 *  Convenience overload that converts `interestRate` and `paymentInterval` to a
 *  periodic rate via `loanPeriodicRate()` and delegates to the `periodicRate`
 *  overload. See that overload's documentation for full details.
 *
 *  @param rules              Active amendment rules.
 *  @param asset              Loan asset.
 *  @param principalOutstanding Requested or remaining principal.
 *  @param interestRate       Annual interest rate in tenth-of-a-basis-point units.
 *  @param paymentInterval    Length of one payment period in seconds.
 *  @param paymentsRemaining  Total number of scheduled payments.
 *  @param managementFeeRate  Broker fee rate in tenth-bips.
 *  @param minimumScale       Floor on the derived `loanScale`.
 *  @return `LoanProperties` suitable for use in `checkLoanGuards()`.
 */
LoanProperties
computeLoanProperties(
    Rules const& rules,
    Asset const& asset,
    Number const& principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate,
    std::int32_t minimumScale)
{
    auto const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(interestRate == 0 || periodicRate > 0, "xrpl::computeLoanProperties : valid rate");
    return computeLoanProperties(
        rules,
        asset,
        principalOutstanding,
        periodicRate,
        paymentsRemaining,
        managementFeeRate,
        minimumScale);
}

/** Compute all derived loan properties from a pre-converted periodic rate.
 *
 *  Calculates `periodicPayment`, the rounded total value outstanding, the
 *  `loanScale` (derived from the `STAmount` exponent of the total value,
 *  clamped to `minimumScale`), and `firstPaymentPrincipal`. The results are
 *  intended for `checkLoanGuards()` and populate `LoanProperties` for storage
 *  in the Loan ledger object. Called at loan creation and after overpayment
 *  re-amortization. Implements concepts from XLS-66 §3.2.4.3 and equations
 *  30-33 from Section A-2.
 *
 *  @param rules              Active amendment rules.
 *  @param asset              Loan asset.
 *  @param principalOutstanding Requested or remaining principal.
 *  @param periodicRate       Pre-computed per-period interest rate.
 *  @param paymentsRemaining  Total number of scheduled payments.
 *  @param managementFeeRate  Broker fee rate in tenth-bips.
 *  @param minimumScale       Floor on the derived `loanScale`.
 *  @return `LoanProperties` with all fields computed and ready for validation.
 */
LoanProperties
computeLoanProperties(
    Rules const& rules,
    Asset const& asset,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate,
    std::int32_t minimumScale)
{
    auto const periodicPayment =
        detail::loanPeriodicPayment(rules, principalOutstanding, periodicRate, paymentsRemaining);

    auto const [totalValueOutstanding, loanScale] = [&]() {
        // only round up if there should be interest
        NumberRoundModeGuard const mg(
            periodicRate == 0 ? Number::RoundingMode::ToNearest : Number::RoundingMode::Upward);
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
                (!amount.integral() && loanScale >= static_cast<Number>(amount).exponent()),
            "xrpl::computeLoanProperties",
            "loanScale value fits expectations");

        // We may need to truncate the total value because of the minimum
        // scale
        amount = roundToAsset(asset, amount, loanScale);

        return std::make_pair(amount, loanScale);
    }();

    // Since we just figured out the loan scale, we haven't been able to
    // validate that the principal fits in it, so to allow this function to
    // succeed, round it here, and let the caller do the validation.
    auto const roundedPrincipalOutstanding =
        roundToAsset(asset, principalOutstanding, loanScale, Number::RoundingMode::ToNearest);

    // Equation (31) from XLS-66 spec, Section A-2 Equation Glossary
    auto const totalInterestOutstanding = totalValueOutstanding - roundedPrincipalOutstanding;
    auto const feeOwedToBroker =
        computeManagementFee(asset, totalInterestOutstanding, managementFeeRate, loanScale);

    // Compute the principal part of the first payment. This is needed
    // because the principal part may be rounded down to zero, which
    // would prevent the principal from ever being paid down.
    auto const firstPaymentPrincipal = [&]() {
        // Compute the parts for the first payment. Ensure that the
        // principal payment will actually change the principal.
        auto const startingState = computeTheoreticalLoanState(
            rules, periodicPayment, periodicRate, paymentsRemaining, managementFeeRate);

        auto const firstPaymentState = computeTheoreticalLoanState(
            rules, periodicPayment, periodicRate, paymentsRemaining - 1, managementFeeRate);

        // The unrounded principal part needs to be large enough to affect
        // the principal. What to do if not is left to the caller
        return startingState.principalOutstanding - firstPaymentState.principalOutstanding;
    }();

    return LoanProperties{
        .periodicPayment = periodicPayment,
        .loanState =
            constructLoanState(totalValueOutstanding, roundedPrincipalOutstanding, feeOwedToBroker),
        .loanScale = loanScale,
        .firstPaymentPrincipal = firstPaymentPrincipal,
    };
}

/** Execute a loan payment transaction and return the breakdown of amounts paid.
 *
 *  The top-level entry point called by `LoanPay::doApply()`. Reads all relevant
 *  fields from the Loan SLE via `ValueProxy` objects that write through on
 *  assignment, then dispatches to the appropriate calculation path based on
 *  `paymentType`:
 *
 *  - **Regular / Overpayment**: loops up to `kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION`
 *    times applying `computePaymentComponents()` + `doPayment()`. If
 *    `paymentType == Overpayment` and funds remain after all regular payments,
 *    `computeOverpaymentComponents()` + `doOverpayment()` handle re-amortization.
 *  - **Late**: calls `computeLatePayment()` then `doPayment()`.
 *  - **Full**: calls `computeFullPayment()` then `doPayment()`.
 *
 *  Any overdue payment not flagged `Late` is rejected with `tecEXPIRED`. Loan
 *  completion (all proxies zeroed) and schedule advancement are handled inside
 *  `doPayment()`. Implements `make_payment` from XLS-66 §3.2.4.4.
 *
 *  @param asset       Loan asset (for rounding and balance operations).
 *  @param view        Apply view providing rules, parent close time, and SLE mutation.
 *  @param loan        Mutable reference to the Loan SLE.
 *  @param brokerSle   Const reference to the LoanBroker SLE (supplies `sfManagementFeeRate`).
 *  @param amount      Amount the borrower is paying.
 *  @param paymentType One of `Regular`, `Late`, `Full`, or `Overpayment`.
 *  @param j           Journal for diagnostic logging.
 *  @return `Expected<LoanPaymentParts, TER>` with payment breakdown on success, or
 *      an error TER (e.g. `tecEXPIRED`, `tecINSUFFICIENT_PAYMENT`, `tecKILLED`).
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

    auto prevPaymentDateProxy = loan->at(sfPreviousPaymentDueDate);
    std::uint32_t const startDate = loan->at(sfStartDate);

    std::uint32_t const paymentInterval = loan->at(sfPaymentInterval);

    // Compute the periodic rate that will be used for calculations
    // throughout
    Number const periodicRate = loanPeriodicRate(interestRate, paymentInterval);
    XRPL_ASSERT(interestRate == 0 || periodicRate > 0, "xrpl::loanMakePayment : valid rate");

    XRPL_ASSERT(*totalValueOutstandingProxy > 0, "xrpl::loanMakePayment : valid total value");

    view.update(loan);

    // -------------------------------------------------------------
    // A late payment not flagged as late overrides all other options.
    if (paymentType != LoanPaymentType::Late && hasExpired(view, nextDueDateProxy))
    {
        // If the payment is late, and the late flag was not set, it's not
        // valid
        JLOG(j.warn()) << "Loan payment is overdue. Use the tfLoanLatePayment "
                          "transaction "
                          "flag to make a late payment. Loan was created on "
                       << startDate << ", prev payment due date is " << prevPaymentDateProxy
                       << ", next payment due date is " << nextDueDateProxy << ", ledger time is "
                       << view.parentCloseTime().time_since_epoch().count();
        return Unexpected(tecEXPIRED);
    }

    // -------------------------------------------------------------
    // full payment handling
    if (paymentType == LoanPaymentType::Full)
    {
        TenthBips32 const closeInterestRate{loan->at(sfCloseInterestRate)};
        Number const closePaymentFee = roundToAsset(asset, loan->at(sfClosePaymentFee), loanScale);

        LoanState const roundedLoanState = constructLoanState(
            totalValueOutstandingProxy, principalOutstandingProxy, managementFeeOutstandingProxy);

        auto const fullPaymentComponents = detail::computeFullPayment(
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
            j);

        if (fullPaymentComponents.has_value())
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

        if (fullPaymentComponents.error())
        {
            // error() will be the TER returned if a payment is not made. It
            // will only evaluate to true if it's unsuccessful. Otherwise,
            // tesSUCCESS means nothing was done, so continue.
            return Unexpected(fullPaymentComponents.error());
        }

        // LCOV_EXCL_START
        UNREACHABLE("xrpl::loanMakePayment : invalid full payment result");
        JLOG(j.error()) << "Full payment computation failed unexpectedly.";
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    // -------------------------------------------------------------
    // compute the periodic payment info that will be needed whether the
    // payment is late or regular
    detail::ExtendedPaymentComponents periodic{
        detail::computePaymentComponents(
            view.rules(),
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
        "xrpl::loanMakePayment",
        "regular payment valid principal");

    // -------------------------------------------------------------
    // late payment handling
    if (paymentType == LoanPaymentType::Late)
    {
        TenthBips32 const lateInterestRate{loan->at(sfLateInterestRate)};
        Number const latePaymentFee = loan->at(sfLatePaymentFee);

        auto const latePaymentComponents = detail::computeLatePayment(
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
            j);

        if (latePaymentComponents.has_value())
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

        if (latePaymentComponents.error())
        {
            // error() will be the TER returned if a payment is not made. It
            // will only evaluate to true if it's unsuccessful.
            return Unexpected(latePaymentComponents.error());
        }

        // LCOV_EXCL_START
        UNREACHABLE("xrpl::loanMakePayment : invalid late payment result");
        JLOG(j.error()) << "Late payment computation failed unexpectedly.";
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    // -------------------------------------------------------------
    // regular periodic payment handling

    XRPL_ASSERT_PARTS(
        paymentType == LoanPaymentType::Regular || paymentType == LoanPaymentType::Overpayment,
        "xrpl::loanMakePayment",
        "regular payment type");

    // Keep a running total of the actual parts paid
    LoanPaymentParts totalParts;
    Number totalPaid;
    std::size_t numPayments = 0;

    while ((amount >= (totalPaid + periodic.totalDue)) && paymentRemainingProxy > 0 &&
           numPayments < kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION)
    {
        // Try to make more payments
        XRPL_ASSERT_PARTS(
            periodic.trackedPrincipalDelta >= 0,
            "xrpl::loanMakePayment",
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
            (periodic.specialCase == detail::PaymentSpecialCase::Final) ==
                (paymentRemainingProxy == 0),
            "xrpl::loanMakePayment",
            "final payment is the final payment");

        // Don't compute the next payment if this was the last payment
        if (periodic.specialCase == detail::PaymentSpecialCase::Final)
            break;

        periodic = detail::ExtendedPaymentComponents{
            detail::computePaymentComponents(
                view.rules(),
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
        JLOG(j.warn()) << "Regular loan payment amount is insufficient. Due: " << periodic.totalDue
                       << ", paid: " << amount;
        return Unexpected(tecINSUFFICIENT_PAYMENT);
    }

    XRPL_ASSERT_PARTS(
        totalParts.principalPaid + totalParts.interestPaid + totalParts.feePaid == totalPaid,
        "xrpl::loanMakePayment",
        "payment parts add up");
    XRPL_ASSERT_PARTS(totalParts.valueChange == 0, "xrpl::loanMakePayment", "no value change");

    // -------------------------------------------------------------
    // overpayment handling
    if (paymentType == LoanPaymentType::Overpayment && loan->isFlag(lsfLoanOverpayment) &&
        paymentRemainingProxy > 0 && totalPaid < amount &&
        numPayments < kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION)
    {
        TenthBips32 const overpaymentInterestRate{loan->at(sfOverpaymentInterestRate)};
        TenthBips32 const overpaymentFeeRate{loan->at(sfOverpaymentFee)};

        // It shouldn't be possible for the overpayment to be greater than
        // totalValueOutstanding, because that would have been processed as
        // another normal payment. But cap it just in case.
        Number const overpayment = std::min(amount - totalPaid, *totalValueOutstandingProxy);

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
                overpaymentComponents.untrackedInterest >= beast::kZERO,
                "xrpl::loanMakePayment",
                "overpayment penalty did not reduce value of loan");
            // Can't just use `periodicPayment` here, because it might
            // change
            auto periodicPaymentProxy = loan->at(sfPeriodicPayment);
            if (auto const overResult = detail::doOverpayment(
                    view.rules(),
                    asset,
                    loanScale,
                    overpaymentComponents,
                    totalValueOutstandingProxy,
                    principalOutstandingProxy,
                    managementFeeOutstandingProxy,
                    periodicPaymentProxy,
                    periodicRate,
                    paymentRemainingProxy,
                    managementFeeRate,
                    j))
            {
                totalParts += *overResult;
            }
            else if (overResult.error())
            {
                // error() will be the TER returned if a payment is not
                // made. It will only evaluate to true if it's unsuccessful.
                // Otherwise, tesSUCCESS means nothing was done, so
                // continue.
                return Unexpected(overResult.error());
            }
        }
    }

    // Check the final results are rounded, to double-check that the
    // intermediate steps were rounded.
    XRPL_ASSERT(
        isRounded(asset, totalParts.principalPaid, loanScale) &&
            totalParts.principalPaid >= beast::kZERO,
        "xrpl::loanMakePayment : total principal paid is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.interestPaid, loanScale) &&
            totalParts.interestPaid >= beast::kZERO,
        "xrpl::loanMakePayment : total interest paid is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.valueChange, loanScale),
        "xrpl::loanMakePayment : loan value change is valid");
    XRPL_ASSERT(
        isRounded(asset, totalParts.feePaid, loanScale) && totalParts.feePaid >= beast::kZERO,
        "xrpl::loanMakePayment : fee paid is valid");
    return totalParts;
}
}  // namespace xrpl
