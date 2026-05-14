#pragma once

/** @file
 *  Computational core of the XLS-66 lending protocol.
 *
 *  Defines the data structures and mathematical primitives that model every
 *  stage of an amortized loan's life cycle: computing periodic payments,
 *  splitting each payment into principal / interest / management-fee
 *  components, and handling regular, late, full (early-closure), and
 *  overpayment scenarios.  The top-level entry point `loanMakePayment()`
 *  implements `make_payment` from XLS-66 §3.2.4.4.  Every lending transactor
 *  (`LoanSet`, `LoanPay`, `LoanDelete`, `LoanBroker*`) either calls these
 *  functions or operates on the structures defined here.
 */

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/st.h>

namespace xrpl {

/** Verify that all amendment prerequisites for the lending protocol are active.
 *
 *  Every lending transactor calls this in `checkExtraFeatures()`. Adding a new
 *  prerequisite here gates all lending transactions atomically, so callers do
 *  not need to replicate the dependency list.
 *
 *  @param rules  Active amendment rules for the current ledger.
 *  @param tx     The transaction being validated; inspected for optional fields
 *      (e.g., `sfDomainID`) that require additional amendments.
 *  @return `true` if all required amendments are enabled and the transaction is
 *      consistent with them; `false` if the caller must reject the transaction.
 */
bool
checkLendingProtocolDependencies(Rules const& rules, STTx const& tx);

/** Number of seconds in a 365-day year; used to prorate annual rates. */
static constexpr std::uint32_t kSECONDS_IN_YEAR = 365 * 24 * 60 * 60;

/** Convert an annualized interest rate to a per-payment-period rate.
 *
 *  Prorates the annual rate by the fraction `paymentInterval / kSECONDS_IN_YEAR`.
 *  Implements Equation (1) from XLS-66, Section A-2 Equation Glossary.  All
 *  amortization math in this module flows from this single conversion.
 *
 *  @param interestRate     Annual interest rate in tenth-of-a-basis-point units.
 *  @param paymentInterval  Length of one payment period in seconds.
 *  @return The per-period rate as a `Number` at full floating-point precision.
 */
Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

/** Round a periodic payment amount upward to the asset's representable precision.
 *
 *  Borrowers must never benefit from rounding, so periodic payments are always
 *  rounded upward.  Delegates to `roundToAsset(..., Number::RoundingMode::Upward)`.
 *
 *  @param asset           Asset whose representable precision constrains rounding.
 *  @param periodicPayment Unrounded installment amount.
 *  @param scale           Exponent that defines the target precision.
 *  @return The smallest representable value >= `periodicPayment` at `scale`.
 */
inline Number
roundPeriodicPayment(Asset const& asset, Number const& periodicPayment, std::int32_t scale)
{
    return roundToAsset(asset, periodicPayment, scale, Number::RoundingMode::Upward);
}

/** Breakdown of amounts paid and loan-object changes produced by one payment.
 *
 *  Returned by `loanMakePayment()` after a payment is processed.  The three
 *  non-negative fields (`principalPaid`, `interestPaid`, `feePaid`) are the
 *  amounts disbursed to the loan principal, the vault, and the broker
 *  respectively.  `valueChange` records unexpected movement in the total
 *  outstanding balance:
 *
 *  - **0** for a well-timed regular payment.
 *  - **negative** for an overpayment (extra principal reduces future interest).
 *  - **positive** for a late payment (penalty interest increased the debt).
 *
 *  `operator+=` accumulates results across multiple payment rounds within a
 *  single transaction.
 *
 *  @note Defined in XLS-66 §3.2.4.2 (Payment Processing).
 */
struct LoanPaymentParts
{
    /** Amount that reduces `sfPrincipalOutstanding`; paid to the vault. */
    Number principalPaid = kNUM_ZERO;

    /** Total interest paid to the vault, including both tracked amortization
     *  interest and any untracked late-payment penalty interest.  Always >= 0.
     */
    Number interestPaid = kNUM_ZERO;

    /** Net change in the loan's total outstanding value.
     *
     *  Zero for on-time regular payments.  Negative when an overpayment
     *  reduces the principal beyond the scheduled amount.  Positive when a
     *  late-payment penalty interest increases the debt.
     */
    Number valueChange = kNUM_ZERO;

    /** Total fees paid to the broker, including tracked management fees and any
     *  untracked late or service fees.  Always >= 0.
     */
    Number feePaid = kNUM_ZERO;

    /** Accumulate payment parts from a subsequent payment round.
     *
     *  Used by `loanMakePayment()` when a single transaction covers more than
     *  one installment.  Asserts that all fields of `other` are non-negative.
     *
     *  @param other  Parts from the next completed round.
     *  @return Reference to `*this` with all fields incremented.
     */
    LoanPaymentParts&
    operator+=(LoanPaymentParts const& other);

    /** Compare two `LoanPaymentParts` for exact equality across all four fields.
     *
     *  @param other  The instance to compare against.
     *  @return `true` if every field compares equal.
     */
    bool
    operator==(LoanPaymentParts const& other) const;
};

/** Snapshot of the financial state of a loan at a point in time.
 *
 *  Holds the four numbers that describe where a loan stands.  Values may be
 *  theoretical (unrounded, from `computeTheoreticalLoanState()`) or rounded to
 *  ledger precision (from `constructRoundedLoanState()`), depending on context.
 *
 *  @invariant `interestDue + managementFeeDue == valueOutstanding - principalOutstanding`.
 *      This is enforced at runtime by `XRPL_ASSERT_PARTS` inside `interestOutstanding()`.
 *
 *  Many fields are derivable from each other; they are all stored explicitly to
 *  reduce duplication and the risk of sign/orientation mistakes.
 */
struct LoanState
{
    /** Total value still owed by the borrower (`sfTotalValueOutstanding`). */
    Number valueOutstanding;

    /** Principal component still outstanding (`sfPrincipalOutstanding`). */
    Number principalOutstanding;

    /** Net interest due to the vault; equals
     *  `valueOutstanding - principalOutstanding - managementFeeDue`.
     */
    Number interestDue;

    /** Management fee due to the broker (`sfManagementFeeOutstanding`);
     *  a sub-portion of the gross interest outstanding.
     */
    Number managementFeeDue;

    /** Sum of `interestDue` and `managementFeeDue`.
     *
     *  Asserts the `LoanState` invariant before returning.
     *
     *  @return `interestDue + managementFeeDue`, i.e., the gross interest still owed.
     */
    [[nodiscard]] Number
    interestOutstanding() const
    {
        XRPL_ASSERT_PARTS(
            interestDue + managementFeeDue == valueOutstanding - principalOutstanding,
            "xrpl::LoanState::interestOutstanding",
            "other values add up correctly");
        return interestDue + managementFeeDue;
    }
};

/** Fully-initialized description of a loan's payment structure.
 *
 *  Computed by `computeLoanProperties()` at loan creation (`LoanSet`) and
 *  after each overpayment re-amortization.  Passed to `checkLoanGuards()` for
 *  validation before being written to the Loan ledger entry.
 *
 *  The `loanScale` is derived dynamically from the `STAmount` exponent of the
 *  rounded total value outstanding and clamped to `minimumScale`.  Using a
 *  consistent scale for all subsequent rounding prevents dust-accumulation bugs
 *  where tiny remainders can never be fully cleared.
 */
struct LoanProperties
{
    /** Unrounded fixed installment amount computed from the amortization formula.
     *
     *  The amount actually required from the borrower each period is this value
     *  rounded upward via `roundPeriodicPayment()`.
     */
    Number periodicPayment;

    /** Current loan state with all fields rounded to `loanScale`. */
    LoanState loanState;

    /** Decimal exponent used for rounding all loan amounts.
     *
     *  Set to the maximum of the asset's native scale and a minimum scale
     *  sufficient to represent the periodic payment accurately.  All principal,
     *  interest, and fee values are rounded to this exponent.
     */
    std::int32_t loanScale{};

    /** Unrounded principal portion of the very first periodic payment.
     *
     *  Checked by `checkLoanGuards()` to ensure it is > 0: the first payment
     *  pays the least principal in an amortized schedule, so if it is positive
     *  then all subsequent payments will also reduce principal.
     */
    Number firstPaymentPrincipal;
};

/** Apply an adjustment to a loan value, re-round to vault scale, and clamp to zero.
 *
 *  Certain loan values are re-rounded to the vault scale every time they are
 *  adjusted, preventing the accumulation of rounding dust across many payment
 *  cycles.  If the result would be negative (possible due to sub-scale rounding
 *  errors), it is clamped to zero.
 *
 *  @tparam NumberProxy  A proxy type that supports assignment and dereferencing
 *      to `Number` (e.g., `STObject::ValueProxy<Number>`).
 *  @param value       Proxy to the value being adjusted; updated in place.
 *  @param adjustment  The signed delta to apply before re-rounding.
 *  @param asset       Asset that constrains representable precision.
 *  @param vaultScale  Exponent for re-rounding the result.
 */
template <class NumberProxy>
void
adjustImpreciseNumber(
    NumberProxy value,
    Number const& adjustment,
    Asset const& asset,
    int vaultScale)
{
    value = roundToAsset(asset, value + adjustment, vaultScale);

    if (*value < beast::kZERO)
        value = 0;
}

/** Extract the decimal scale of a vault's `sfAssetsTotal` field.
 *
 *  Returns `Number::kMIN_EXPONENT - 1` (an unusably small sentinel) when
 *  `vaultSle` is null, so callers can detect the invalid case without a
 *  separate null check.
 *
 *  @param vaultSle  Const reference to the Vault SLE; may be null.
 *  @return The scale of `sfAssetsTotal` relative to `sfAsset`, or a sentinel
 *      value if `vaultSle` is null.
 */
inline int
getAssetsTotalScale(SLE::const_ref vaultSle)
{
    if (!vaultSle)
        return Number::kMIN_EXPONENT - 1;  // LCOV_EXCL_LINE
    return scale(vaultSle->at(sfAssetsTotal), vaultSle->at(sfAsset));
}

/** Validate that a set of computed loan properties is self-consistent and payable.
 *
 *  Enforces four guards derived from XLS-66 to reject loans that would fail
 *  to amortize correctly under the spec's rounding rules:
 *
 *  1. If `expectInterest` is `true`, total lifetime interest must be > 0.
 *  2. `firstPaymentPrincipal` must be > 0 (ensures every payment reduces principal).
 *  3. The rounded periodic payment must not round to zero.
 *  4. `floor(valueOutstanding / roundedPayment)` must equal `paymentTotal`
 *     (ensures the loan closes in exactly the specified number of installments).
 *
 *  Called from loan creation (`LoanSet::doApply()`) and after each overpayment
 *  re-amortization inside `detail::tryOverpayment()`.
 *
 *  @param vaultAsset         Asset used for rounding the periodic payment.
 *  @param principalRequested Loan principal; used to derive total interest outstanding.
 *  @param expectInterest     `true` when the loan's interest rate is non-zero.
 *  @param paymentTotal       Total number of scheduled payments.
 *  @param properties         Computed loan properties to validate.
 *  @param j                  Journal for diagnostic log messages.
 *  @return `tesSUCCESS` if all guards pass; `tecPRECISION_LOSS` when the loan
 *      cannot be amortized accurately at the given principal / rate / scale
 *      combination; `tecINTERNAL` for unexpected internal inconsistencies.
 */
TER
checkLoanGuards(
    Asset const& vaultAsset,
    Number const& principalRequested,
    bool expectInterest,
    std::uint32_t paymentTotal,
    LoanProperties const& properties,
    beast::Journal j);

/** Compute the theoretically correct loan state at full arithmetic precision.
 *
 *  Derives what each outstanding balance *should* be purely from the payment
 *  schedule, with no ledger-rounding effects.  Used as a target in
 *  `computePaymentComponents()` and `detail::tryOverpayment()` to measure and
 *  correct accumulated rounding drift.  Implements `calculate_true_loan_state`
 *  from XLS-66 §3.2.4.4 (Equations 30-33 from Section A-2).
 *
 *  @param rules             Active amendment rules (passed to the internal
 *      `loanPrincipalFromPeriodicPayment()` call).
 *  @param periodicPayment   Fixed installment amount.
 *  @param periodicRate      Pre-computed per-period interest rate.
 *  @param paymentRemaining  Number of payments still remaining after this point.
 *  @param managementFeeRate Broker fee rate in tenth-bips.
 *  @return Unrounded `LoanState` derived from the schedule, or a fully-zeroed
 *      state if `paymentRemaining == 0`.
 */
LoanState
computeTheoreticalLoanState(
    Rules const& rules,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

/** Build a `LoanState` from the three directly-tracked loan balances.
 *
 *  Derives `interestDue = totalValueOutstanding - principalOutstanding -
 *  managementFeeOutstanding` rather than accepting it as a parameter, ensuring
 *  the `LoanState` invariant always holds.  Prefer this over constructing
 *  `LoanState` directly to avoid sign/ordering mistakes.
 *
 *  @param totalValueOutstanding    Total value still owed by the borrower.
 *  @param principalOutstanding     Principal component still outstanding.
 *  @param managementFeeOutstanding Management fee component still outstanding.
 *  @return `LoanState` with `interestDue` derived from the other three fields.
 */
LoanState
constructLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding);

/** Build a `LoanState` from a Loan ledger entry's current rounded values.
 *
 *  Convenience wrapper that reads `sfTotalValueOutstanding`,
 *  `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` from the SLE
 *  and delegates to `constructLoanState()`.
 *
 *  @param loan  Const reference to the Loan SLE.
 *  @return `LoanState` reflecting the current rounded ledger values.
 */
LoanState
constructRoundedLoanState(SLE::const_ref loan);

/** Compute the broker's management fee on a given interest amount.
 *
 *  Calculates `roundDown(tenthBipsOfValue(value, managementFeeRate), scale)`.
 *  Downward rounding ensures the vault always receives its full share.
 *  Implements Equation (32) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param asset             Asset used to constrain rounding.
 *  @param value             Gross interest amount from which the fee is taken.
 *  @param managementFeeRate Broker's rate in tenth-of-a-basis-point units.
 *  @param scale             Exponent for rounding the result downward.
 *  @return Broker fee rounded down to `scale`.
 */
Number
computeManagementFee(
    Asset const& asset,
    Number const& interest,
    TenthBips32 managementFeeRate,
    std::int32_t scale);

/** Compute the total interest charge for an early full payment.
 *
 *  Sums two components (Equations 27-28 from XLS-66, Section A-2):
 *  - Accrued interest since the last payment (`detail::loanAccruedInterest()`).
 *  - Prepayment penalty (`closeInterestRate` applied to the theoretical
 *    principal outstanding); zero when `closeInterestRate == 0`.
 *
 *  @param theoreticalPrincipalOutstanding Unrounded principal derived from the
 *      payment schedule (not the rounded ledger value).
 *  @param periodicRate       Pre-computed per-period interest rate.
 *  @param parentCloseTime    Close time of the parent ledger (the "now" for the
 *      accrued-interest calculation).
 *  @param paymentInterval    Payment period length in seconds.
 *  @param prevPaymentDate    Due date of the most recently completed payment.
 *  @param startDate          Loan start date.
 *  @param closeInterestRate  Prepayment penalty rate in tenth-of-a-basis-point
 *      units; 0 means no prepayment penalty.
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
    TenthBips32 closeInterestRate);

namespace detail {
// These classes and functions should only be accessed by LendingHelper
// functions and unit tests

/** Classification of a single payment's scheduling role. */
enum class PaymentSpecialCase {
    None,   /**< Regular scheduled installment. */
    Final,  /**< Last payment that closes out the loan. */
    Extra   /**< Overpayment beyond the regular schedule. */
};

/** Tracked delta values that will be applied to the Loan ledger entry on payment.
 *
 *  Each field represents the amount by which the corresponding `sf*` field in
 *  the Loan object will be reduced.  These are "tracked" because they alter the
 *  loan's amortization schedule; contrast with the untracked amounts in
 *  `ExtendedPaymentComponents`.
 *
 *  The relationship `trackedValueDelta == trackedPrincipalDelta +
 *  trackedInterestPart() + trackedManagementFeeDelta` must hold at all times.
 */
struct PaymentComponents
{
    /** Amount subtracted from `sfTotalValueOutstanding`.
     *
     *  Equals `trackedPrincipalDelta + trackedInterestPart() +
     *  trackedManagementFeeDelta`.
     */
    Number trackedValueDelta;

    /** Amount subtracted from `sfPrincipalOutstanding`. */
    Number trackedPrincipalDelta;

    /** Amount subtracted from `sfManagementFeeOutstanding`.
     *
     *  Covers only scheduled management fees from the amortization table;
     *  unscheduled fees (e.g., late fees) live in `ExtendedPaymentComponents`.
     */
    Number trackedManagementFeeDelta;

    /** Scheduling classification of this payment. */
    PaymentSpecialCase specialCase = PaymentSpecialCase::None;

    /** Net interest portion of this payment, paid to the vault.
     *
     *  Derived as `trackedValueDelta - trackedPrincipalDelta -
     *  trackedManagementFeeDelta`.
     *
     *  @return Scheduled interest component; always >= 0 for well-formed input.
     */
    [[nodiscard]] Number
    trackedInterestPart() const;
};

/** `PaymentComponents` extended with untracked fees and interest.
 *
 *  Untracked amounts are paid out to the broker (`untrackedManagementFee`) and
 *  vault (`untrackedInterest`) without altering the Loan object's amortization
 *  state.  They arise from charges outside the regular schedule — late payment
 *  penalties, service fees, origination fees.
 *
 *  `totalDue` is computed eagerly in the constructor as
 *  `trackedValueDelta + untrackedInterest + untrackedManagementFee`.
 *
 *  @note `untrackedManagementFee` and `untrackedInterest` may individually be
 *      negative (e.g., adjustments), but the corresponding fields in the
 *      returned `LoanPaymentParts` are always clamped to >= 0.
 */
struct ExtendedPaymentComponents : public PaymentComponents
{
    /** Unscheduled fee paid directly to the broker (e.g., late fee, service fee). */
    Number untrackedManagementFee;

    /** Unscheduled interest paid directly to the vault (e.g., late penalty). */
    Number untrackedInterest;

    /** Total amount due from the borrower for this payment.
     *
     *  Equals `trackedValueDelta + untrackedInterest + untrackedManagementFee`.
     *  Used to verify the borrower's supplied amount is sufficient.
     */
    Number totalDue;

    /** Construct from a base `PaymentComponents` plus the two untracked amounts.
     *
     *  @param p        Base tracked components.
     *  @param fee      Untracked management fee for the broker.
     *  @param interest Untracked interest for the vault; defaults to zero.
     */
    ExtendedPaymentComponents(PaymentComponents const& p, Number fee, Number interest = kNUM_ZERO)
        : PaymentComponents(p)
        , untrackedManagementFee(fee)
        , untrackedInterest(interest)
        , totalDue(trackedValueDelta + untrackedInterest + untrackedManagementFee)
    {
    }
};

/** Component-wise difference between two `LoanState` objects.
 *
 *  Produced by `operator-(LoanState, LoanState)`.  Used in `tryOverpayment()`
 *  to measure the gap between the theoretical (unrounded) loan state and the
 *  rounded ledger state, allowing accumulated rounding error to be preserved
 *  across re-amortization.
 */
struct LoanStateDeltas
{
    /** Change in principal outstanding. */
    Number principal;

    /** Change in interest due. */
    Number interest;

    /** Change in management fee outstanding. */
    Number managementFee;

    /** Sum of all three delta components.
     *
     *  @return `principal + interest + managementFee`.
     */
    [[nodiscard]] Number
    total() const
    {
        return principal + interest + managementFee;
    }

    /** Clamp all fields to zero from below.
     *
     *  Rounding can occasionally produce tiny negative deltas when the
     *  theoretical target exceeds the current rounded state by a sub-scale
     *  amount.  This method eliminates those artifacts before the deltas are
     *  used as payment amounts.
     */
    void
    nonNegative();
};

/** Simulate a principal overpayment and re-amortize the loan in a local sandbox.
 *
 *  Cannot simply recalculate from scratch because accumulated rounding errors
 *  from the loan's history must be preserved.  The algorithm:
 *  1. Computes the theoretical (unrounded) current state from the schedule.
 *  2. Measures the rounding error gap against the current ledger state.
 *  3. Reduces the theoretical principal by the overpayment amount.
 *  4. Calls `computeLoanProperties()` for the new schedule.
 *  5. Re-applies the preserved rounding errors before final rounding.
 *  6. Validates the result with `checkLoanGuards()`.
 *
 *  Returns `Unexpected(tesSUCCESS)` (no error, but no commit) when the
 *  overpayment would leave the loan in an invalid state — the caller treats
 *  this as a silent no-op rather than a transaction failure.
 *
 *  @param rules                   Active amendment rules.
 *  @param asset                   Loan asset.
 *  @param loanScale               Current loan rounding exponent.
 *  @param overpaymentComponents   Breakdown of the overpayment (tracked + untracked).
 *  @param roundedLoanState        Current rounded loan state from the ledger.
 *  @param periodicPayment         Current fixed installment amount.
 *  @param periodicRate            Per-period interest rate.
 *  @param paymentRemaining        Payments remaining before the overpayment.
 *  @param managementFeeRate       Broker fee rate in tenth-bips.
 *  @param j                       Journal for diagnostic logging.
 *  @return Pair of (`LoanPaymentParts`, re-amortized `LoanProperties`) on success;
 *      `Unexpected(TER)` on a hard failure, or `Unexpected(tesSUCCESS)` when the
 *      overpayment is silently suppressed.
 */
Expected<std::pair<LoanPaymentParts, LoanProperties>, TER>
tryOverpayment(
    Rules const& rules,
    Asset const& asset,
    std::int32_t loanScale,
    ExtendedPaymentComponents const& overpaymentComponents,
    LoanState const& roundedLoanState,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    TenthBips16 const managementFeeRate,
    beast::Journal j);

/** Compute `(1 + r)^n - 1` accurately for near-zero `r` via binomial expansion.
 *
 *  Direct subtraction `power(1 + r, n) - 1` suffers catastrophic cancellation
 *  when `r` is small: the result `~r*n` sits far below the leading `1` in
 *  `(1+r)^n`, consuming most of `Number`'s 19-digit mantissa.  The binomial
 *  expansion avoids this:
 *
 *  `(1 + r)^n - 1 = nr + C(n,2) r^2 + ... + r^n`
 *
 *  Each term is derived from the previous as
 *  `term_{k+1} = term_k * r * (n - k) / (k + 1)`.  The loop terminates early
 *  when adding the next term leaves the running sum unchanged (below `Number`'s
 *  precision floor).
 *
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of periods `n`.
 *  @return `(1 + r)^n - 1`, or 0 if `r == 0` or `n == 0`.
 *  @note For `r * n >= 1e-9` the closed-form path in `computePowerMinusOneHybrid()`
 *      is ~30-500x faster and equally accurate; prefer the hybrid for production use.
 */
[[nodiscard]] Number
computePowerMinusOne(Number const& periodicRate, std::uint32_t paymentsRemaining);

/** Compute `(1 + r)^n - 1`, selecting the numerically stable path automatically.
 *
 *  When `r * n >= 1e-9` the closed-form `power(1 + r, n) - 1` retains enough
 *  precision and is ~30-500x faster than the binomial expansion.  Below that
 *  threshold, catastrophic cancellation degrades the result to fewer than ~10
 *  significant digits, so the call is forwarded to `computePowerMinusOne()`.
 *
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of periods `n`.
 *  @return `(1 + r)^n - 1`, or 0 if `r == 0` or `n == 0`.
 *  @note The 1e-9 threshold is verified by `testComputePowerMinusOneHybrid` to
 *      yield agreement between both paths to within `Number`'s post-subtraction
 *      precision (~10 significant digits) at the crossover.
 */
[[nodiscard]] Number
computePowerMinusOneHybrid(Number const& periodicRate, std::uint32_t paymentsRemaining);

/** Compute the standard amortization payment factor `r(1+r)^n / ((1+r)^n - 1)`.
 *
 *  Multiplying this factor by the outstanding principal yields the fixed periodic
 *  payment.  Implements Equation (6) from XLS-66, Section A-2.
 *
 *  When `fixCleanup3_2_0` is active the denominator is evaluated via
 *  `computePowerMinusOneHybrid()` to avoid catastrophic cancellation at near-zero
 *  rates.  The pre-amendment path uses `power(1+r, n) - 1` directly and is
 *  preserved for historic replay.
 *
 *  @param rules             Active amendment rules (gates the hybrid path).
 *  @param periodicRate      Per-period rate `r`; must be >= 0.
 *  @param paymentsRemaining Number of remaining payments `n`.
 *  @return The payment factor; `1/n` when `r == 0`; 0 when `n == 0`.
 */
[[nodiscard]] Number
computePaymentFactor(
    Rules const& rules,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining);

/** Split a gross interest amount into net vault interest and broker management fee.
 *
 *  Computes `fee = computeManagementFee(interest, managementFeeRate)` and
 *  returns `(interest - fee, fee)`.  Implements Equation (33) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param asset             Asset used for rounding the fee.
 *  @param interest          Gross interest amount to split.
 *  @param managementFeeRate Broker's share of gross interest in tenth-bips.
 *  @param loanScale         Exponent for rounding the fee downward.
 *  @return Pair `(netInterest, fee)` where `netInterest + fee == interest`.
 */
std::pair<Number, Number>
computeInterestAndFeeParts(
    Asset const& asset,
    Number const& interest,
    TenthBips16 managementFeeRate,
    std::int32_t loanScale);

/** Compute the fixed installment amount for a standard amortized loan.
 *
 *  Implements `principal * paymentFactor(r, n)`.  For zero-interest loans
 *  the formula degenerates to equal principal slices (`principal / n`).
 *  Implements Equation (7) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param rules                Active amendment rules (passed to `computePaymentFactor`).
 *  @param principalOutstanding Current outstanding principal.
 *  @param periodicRate         Per-period interest rate.
 *  @param paymentsRemaining    Number of payments remaining.
 *  @return Unrounded periodic payment; 0 if `principalOutstanding == 0`
 *      or `paymentsRemaining == 0`.
 */
Number
loanPeriodicPayment(
    Rules const& rules,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining);

/** Reverse-calculate the outstanding principal implied by a given periodic payment.
 *
 *  The inverse of `loanPeriodicPayment()`: recovers what the principal should be
 *  at a given point in the amortization schedule.  Used by
 *  `computeTheoreticalLoanState()` and the early-closure path.  Implements
 *  Equation (10) from XLS-66, Section A-2 Equation Glossary.
 *
 *  @param rules             Active amendment rules (passed to `computePaymentFactor`).
 *  @param periodicPayment   Fixed installment amount.
 *  @param periodicRate      Per-period interest rate.
 *  @param paymentsRemaining Number of payments remaining.
 *  @return Theoretical outstanding principal; 0 if `paymentsRemaining == 0`;
 *      `periodicPayment * paymentsRemaining` when `periodicRate == 0`.
 */
Number
loanPrincipalFromPeriodicPayment(
    Rules const& rules,
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining);

/** Compute the penalty interest accrued on an overdue payment.
 *
 *  Calculates `principal * loanPeriodicRate(lateInterestRate, secondsOverdue)`.
 *  Returns 0 if the payment is on time or early, if `principalOutstanding == 0`,
 *  or if `lateInterestRate == 0`.  Implements Equation (16) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param principalOutstanding Current outstanding principal.
 *  @param lateInterestRate     Annualized penalty rate in tenth-of-a-basis-point units.
 *  @param parentCloseTime      Close time of the parent ledger (the "now").
 *  @param nextPaymentDueDate   Timestamp when the payment was originally due.
 *  @return Unrounded late penalty interest; 0 if the payment is not overdue.
 */
Number
loanLatePaymentInterest(
    Number const& principalOutstanding,
    TenthBips32 lateInterestRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t nextPaymentDueDate);

/** Compute the interest that has accrued since the last payment.
 *
 *  Prorates the periodic interest by the fraction of the payment interval that
 *  has elapsed since `prevPaymentDate` (or `startDate` for the first payment).
 *  Returns 0 if `principalOutstanding == 0`, `periodicRate == 0`, or the current
 *  time is before `startDate`.  Implements Equation (27) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param principalOutstanding Current outstanding principal.
 *  @param periodicRate         Pre-computed per-period interest rate.
 *  @param parentCloseTime      Close time of the parent ledger (the "now").
 *  @param startDate            Loan start date (epoch seconds).
 *  @param prevPaymentDate      Due date of the most recently completed payment.
 *  @param paymentInterval      Payment period length in seconds.
 *  @return Unrounded accrued interest; always >= 0.
 */
Number
loanAccruedInterest(
    Number const& principalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t startDate,
    std::uint32_t prevPaymentDate,
    std::uint32_t paymentInterval);

/** Compute payment components for a principal overpayment.
 *
 *  Splits the overpayment into a tracked value delta (principal reduction),
 *  an untracked management fee for the broker, and an optional untracked
 *  interest charge for the vault.  Implements Equations (20-22) from XLS-66,
 *  Section A-2 Equation Glossary.
 *
 *  @param asset                   Loan asset for rounding.
 *  @param loanScale               Rounding exponent.
 *  @param overpayment             Extra principal amount paid above schedule.
 *  @param overpaymentInterestRate Rate applied to the overpayment as an interest
 *      charge; 0 means no interest on the extra principal.
 *  @param overpaymentFeeRate      Fee rate applied to the overpayment for the broker.
 *  @param managementFeeRate       Standard broker fee rate used to split interest.
 *  @return `ExtendedPaymentComponents` with `specialCase == Extra`.
 */
ExtendedPaymentComponents
computeOverpaymentComponents(
    Asset const& asset,
    int32_t const loanScale,
    Number const& overpayment,
    TenthBips32 const overpaymentInterestRate,
    TenthBips32 const overpaymentFeeRate,
    TenthBips16 const managementFeeRate);

/** Compute how a single scheduled installment splits into tracked components.
 *
 *  Uses the theoretical loan state (from `computeTheoreticalLoanState()`) as a
 *  target and caps the deltas at the available balances and the periodic payment
 *  amount to avoid over-reducing the ledger fields.  Implements
 *  `compute_payment_due()` from XLS-66 §3.2.4.4.
 *
 *  @param rules                  Active amendment rules.
 *  @param asset                  Loan asset for rounding.
 *  @param scale                  Rounding exponent.
 *  @param totalValueOutstanding  Current total value outstanding.
 *  @param principalOutstanding   Current principal outstanding.
 *  @param managementFeeOutstanding Current management fee outstanding.
 *  @param periodicPayment        Fixed unrounded installment amount.
 *  @param periodicRate           Pre-computed per-period interest rate.
 *  @param paymentRemaining       Number of payments still remaining.
 *  @param managementFeeRate      Broker fee rate in tenth-bips.
 *  @return `PaymentComponents` with all tracked deltas set.
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
    TenthBips16 managementFeeRate);

}  // namespace detail

/** Compute the component-wise difference between two loan states.
 *
 *  @return `LoanStateDeltas` with each field equal to the corresponding
 *      `lhs` field minus the `rhs` field.
 */
detail::LoanStateDeltas
operator-(LoanState const& lhs, LoanState const& rhs);

/** Subtract a set of deltas from a loan state.
 *
 *  @return New `LoanState` with each field reduced by the corresponding delta.
 */
LoanState
operator-(LoanState const& lhs, detail::LoanStateDeltas const& rhs);

/** Add a set of deltas to a loan state.
 *
 *  @return New `LoanState` with each field increased by the corresponding delta.
 */
LoanState
operator+(LoanState const& lhs, detail::LoanStateDeltas const& rhs);

/** Compute all derived loan properties from raw interest-rate parameters.
 *
 *  Convenience overload that converts `interestRate` and `paymentInterval` to a
 *  per-period rate via `loanPeriodicRate()` and delegates to the `periodicRate`
 *  overload.  See that overload's documentation for the full algorithm.
 *
 *  @param rules              Active amendment rules.
 *  @param asset              Loan asset.
 *  @param principalOutstanding Requested or remaining principal.
 *  @param interestRate       Annual interest rate in tenth-of-a-basis-point units.
 *  @param paymentInterval    Payment period length in seconds.
 *  @param paymentsRemaining  Total number of scheduled payments.
 *  @param managementFeeRate  Broker fee rate in tenth-bips.
 *  @param minimumScale       Floor on the derived `loanScale`.
 *  @return `LoanProperties` suitable for `checkLoanGuards()`.
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
    std::int32_t minimumScale);

/** Compute all derived loan properties from a pre-converted periodic rate.
 *
 *  Calculates `periodicPayment`, the rounded total value outstanding, the
 *  `loanScale` (derived from the `STAmount` exponent of the total value,
 *  clamped to `minimumScale`), and `firstPaymentPrincipal`.  Implements
 *  concepts from XLS-66 §3.2.4.3 and Equations 30-33 from Section A-2.
 *
 *  The `loanScale` is not fixed at loan creation — it is derived dynamically so
 *  that all subsequent rounding of principal, interest, and fees uses a
 *  consistent number of decimal places, preventing dust-accumulation bugs.
 *
 *  Called at loan creation (`LoanSet::doApply()`) and after each overpayment
 *  re-amortization inside `detail::tryOverpayment()`.
 *
 *  @param rules              Active amendment rules.
 *  @param asset              Loan asset.
 *  @param principalOutstanding Requested or remaining principal.
 *  @param periodicRate       Pre-computed per-period interest rate.
 *  @param paymentsRemaining  Total number of scheduled payments.
 *  @param managementFeeRate  Broker fee rate in tenth-bips.
 *  @param minimumScale       Floor on the derived `loanScale`.
 *  @return `LoanProperties` with all fields computed and ready for
 *      `checkLoanGuards()`.
 */
LoanProperties
computeLoanProperties(
    Rules const& rules,
    Asset const& asset,
    Number const& principalOutstanding,
    Number const& periodicRate,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate,
    std::int32_t minimumScale);

/** Check whether a value is already rounded to the given scale.
 *
 *  Compares the downward- and upward-rounded forms; equality means no
 *  sub-scale precision remains.  Used as a precondition guard and
 *  post-condition assertion throughout the payment pipeline.
 *
 *  @param asset  Asset whose representable precision constrains rounding.
 *  @param value  The value to test.
 *  @param scale  Exponent that defines the target precision.
 *  @return `true` if `roundDown(value) == roundUp(value)` at `scale`.
 */
bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale);

/** Classification of the payment being made in a `LoanPay` transaction.
 *
 *  The values are mutually exclusive for `Regular`, `Late`, and `Full`.
 *  `Overpayment` is an add-on to a `Regular` payment: it follows the regular
 *  path and may trigger a re-amortization step at the end if excess funds remain.
 */
enum class LoanPaymentType {
    Regular = 0,  /**< Scheduled installment, paid on time. */
    Late,         /**< Overdue installment, carries penalty interest. */
    Full,         /**< Early full closure, may carry a prepayment penalty. */
    Overpayment   /**< Regular payment with additional principal reduction. */
};

/** Execute a loan payment and return the breakdown of amounts disbursed.
 *
 *  Top-level entry point called by `LoanPay::doApply()`.  Dispatches to the
 *  appropriate internal calculation path based on `paymentType`:
 *
 *  - **Regular / Overpayment**: loops up to `kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION`
 *    times applying `computePaymentComponents()`.  When `paymentType == Overpayment`
 *    and funds remain after all regular installments,
 *    `computeOverpaymentComponents()` + `detail::tryOverpayment()` handle the
 *    re-amortization step.
 *  - **Late**: calls `detail::computeLatePayment()` then commits.
 *  - **Full**: calls `detail::computeFullPayment()` then commits.
 *
 *  Any overdue payment not flagged `Late` is rejected with `tecEXPIRED`.  Loan
 *  completion (all balances zeroed) and schedule advancement are handled as part
 *  of committing each payment round.  Implements `make_payment` from XLS-66
 *  §3.2.4.4.
 *
 *  @param asset       Loan asset (for rounding and balance operations).
 *  @param view        Apply view providing rules, parent close time, and SLE mutation.
 *  @param loan        Mutable reference to the Loan SLE; updated in place.
 *  @param brokerSle   Const reference to the LoanBroker SLE; supplies
 *      `sfManagementFeeRate`.
 *  @param amount      Amount the borrower is supplying for this payment.
 *  @param paymentType One of `Regular`, `Late`, `Full`, or `Overpayment`.
 *  @param j           Journal for diagnostic log messages.
 *  @return `Expected<LoanPaymentParts, TER>` with the payment breakdown on
 *      success; an error TER (`tecEXPIRED`, `tecINSUFFICIENT_PAYMENT`,
 *      `tecKILLED`, etc.) on failure.
 */
Expected<LoanPaymentParts, TER>
loanMakePayment(
    Asset const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    LoanPaymentType const paymentType,
    beast::Journal j);

}  // namespace xrpl
