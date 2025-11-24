#ifndef XRPL_APP_MISC_LENDINGHELPERS_H_INCLUDED
#define XRPL_APP_MISC_LENDINGHELPERS_H_INCLUDED

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/st.h>

namespace ripple {

struct PreflightContext;

// Lending protocol has dependencies, so capture them here.
bool
checkLendingProtocolDependencies(PreflightContext const& ctx);

static constexpr std::uint32_t secondsInYear = 365 * 24 * 60 * 60;

Number
loanPeriodicRate(TenthBips32 interestRate, std::uint32_t paymentInterval);

/// Ensure the periodic payment is always rounded consistently
inline Number
roundPeriodicPayment(
    Asset const& asset,
    Number const& periodicPayment,
    std::int32_t scale)
{
    return roundToAsset(asset, periodicPayment, scale, Number::upward);
}

/* Represents the breakdown of amounts to be paid and changes applied to the
 * Loan object while processing a loan payment.
 *
 * This structure is returned after processing a loan payment transaction and
 * captures the amounts that need to be paid. The actual ledger entry changes
 * are made in LoanPay based on this structure values.
 *
 * The sum of principalPaid, interestPaid, and feePaid represents the total
 * amount to be deducted from the borrower's account. The valueChange field
 * tracks whether the loan's total value increased or decreased beyond normal
 * amortization.
 *
 * This structure is explained in the XLS-66 spec, section 3.2.4.2 (Payment
 * Processing).
 */
struct LoanPaymentParts
{
    // The amount of principal paid that reduces the loan balance.
    // This amount is subtracted from sfPrincipalOutstanding in the Loan object
    // and paid to the Vault
    Number principalPaid = numZero;

    // The total amount of interest paid to the Vault.
    // This includes:
    // - Tracked interest from the amortization schedule
    // - Untracked interest (e.g., late payment penalty interest)
    // This value is always non-negative.
    Number interestPaid = numZero;

    // The change in the loan's total value outstanding.
    // - If valueChange < 0: Loan value decreased
    // - If valueChange > 0: Loan value increased
    // - If valueChange = 0: No value adjustment
    //
    // For regular on-time payments, this is always 0. Non-zero values occur
    // when:
    // - Overpayments reduce the loan balance beyond the scheduled amount
    // - Late payments add penalty interest to the loan value
    // - Early full payment may increase or decrease the loan value based on
    // terms
    Number valueChange = numZero;

    /* The total amount of fees paid to the Broker.
     * This includes:
     * - Tracked management fees from the amortization schedule
     * - Untracked fees (e.g., late payment fees, service fees, origination
     * fees) This value is always non-negative.
     */
    Number feePaid = numZero;

    LoanPaymentParts&
    operator+=(LoanPaymentParts const& other);

    bool
    operator==(LoanPaymentParts const& other) const;
};

/* Describes the initial computed properties of a loan.
 *
 * This structure contains the fundamental calculated values that define a
 * loan's payment structure and amortization schedule. These properties are
 * computed:
 * - At loan creation (LoanSet transaction)
 * - When loan terms change (e.g., after an overpayment that reduces the loan
 * balance)
 */
struct LoanProperties
{
    // The unrounded amount to be paid at each regular payment period.
    // Calculated using the standard amortization formula based on principal,
    // interest rate, and number of payments.
    // The actual amount paid in the LoanPay transaction must be rounded up to
    // the precision of the asset and loan.
    Number periodicPayment;

    // The total amount the borrower will pay over the life of the loan.
    // Equal to periodicPayment * paymentsRemaining.
    // This includes principal, interest, and management fees.
    Number totalValueOutstanding;

    // The total management fee that will be paid to the broker over the
    // loan's lifetime. This is a percentage of the total interest (gross)
    // as specified by the broker's management fee rate.
    Number managementFeeOwedToBroker;

    // The scale (decimal places) used for rounding all loan amounts.
    // This is the maximum of:
    // - The asset's native scale
    // - A minimum scale required to represent the periodic payment accurately
    // All loan state values (principal, interest, fees) are rounded to this
    // scale.
    std::int32_t loanScale;

    // The principal portion of the first payment.
    Number firstPaymentPrincipal;
};

/** This structure captures the parts of a loan state.
 *
 *  Whether the values are raw (unrounded) or rounded will depend on how it was
 * computed.
 *
 *  Many of the fields can be derived from each other, but they're all provided
 *  here to reduce code duplication and possible mistakes.
 *   e.g.
 *     * interestOutstanding = valueOutstanding - principalOutstanding
 *     * interestDue = interestOutstanding - managementFeeDue
 */
struct LoanState
{
    // Total value still due to be paid by the borrower.
    Number valueOutstanding;
    // Principal still due to be paid by the borrower.
    Number principalOutstanding;
    // Interest still due to be paid to the Vault.
    // This is a portion of interestOutstanding
    Number interestDue;
    // Management fee still due to be paid to the broker.
    // This is a portion of interestOutstanding
    Number managementFeeDue;

    // Interest still due to be paid by the borrower.
    Number
    interestOutstanding() const
    {
        XRPL_ASSERT_PARTS(
            interestDue + managementFeeDue ==
                valueOutstanding - principalOutstanding,
            "ripple::LoanState::interestOutstanding",
            "other values add up correctly");
        return interestDue + managementFeeDue;
    }
};

// Some values get re-rounded to the vault scale any time they are adjusted. In
// addition, they are prevented from ever going below zero. This helps avoid
// accumulated rounding errors and leftover dust amounts.
template <class NumberProxy>
void
adjustImpreciseNumber(
    NumberProxy value,
    Number const& adjustment,
    Asset const& asset,
    int vaultScale)
{
    value = roundToAsset(asset, value + adjustment, vaultScale);

    if (*value < beast::zero)
        value = 0;
}

inline int
getVaultScale(SLE::const_ref vaultSle)
{
    if (!vaultSle)
        return Number::minExponent - 1;  // LCOV_EXCL_LINE
    return vaultSle->at(sfAssetsTotal).exponent();
}

TER
checkLoanGuards(
    Asset const& vaultAsset,
    Number const& principalRequested,
    bool expectInterest,
    std::uint32_t paymentTotal,
    LoanProperties const& properties,
    beast::Journal j);

LoanState
computeRawLoanState(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

LoanState
computeRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

// Constructs a valid LoanState object from arbitrary inputs
LoanState
constructLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding);

// Constructs a valid LoanState object from a Loan object, which always has
// rounded values
LoanState
constructRoundedLoanState(SLE::const_ref loan);

Number
computeManagementFee(
    Asset const& asset,
    Number const& interest,
    TenthBips32 managementFeeRate,
    std::int32_t scale);

Number
computeFullPaymentInterest(
    Number const& rawPrincipalOutstanding,
    Number const& periodicRate,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate);

Number
computeFullPaymentInterest(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t paymentRemaining,
    NetClock::time_point parentCloseTime,
    std::uint32_t paymentInterval,
    std::uint32_t prevPaymentDate,
    std::uint32_t startDate,
    TenthBips32 closeInterestRate);

namespace detail {
// These classes and functions should only be accessed by LendingHelper
// functions and unit tests

enum class PaymentSpecialCase { none, final, extra };

/* Represents a single loan payment component parts.

* This structure captures the "delta" (change) values that will be applied to
* the tracked fields in the Loan ledger object when a payment is processed.
*
* These are called "deltas" because they represent the amount by which each
* corresponding field in the Loan object will be reduced.
* They are "tracked" as they change tracked loan values.
*/
struct PaymentComponents
{
    // The change in total value outstanding for this payment.
    // This amount will be subtracted from sfTotalValueOutstanding in the Loan
    // object. Equal to the sum of trackedPrincipalDelta,
    // trackedInterestPart(), and trackedManagementFeeDelta.
    Number trackedValueDelta;

    // The change in principal outstanding for this payment.
    // This amount will be subtracted from sfPrincipalOutstanding in the Loan
    // object, representing the portion of the payment that reduces the
    // original loan amount.
    Number trackedPrincipalDelta;

    // The change in management fee outstanding for this payment.
    // This amount will be subtracted from sfManagementFeeOutstanding in the
    // Loan object. This represents only the tracked management fees from the
    // amortization schedule and does not include additional untracked fees
    // (such as late payment fees) that go directly to the broker.
    Number trackedManagementFeeDelta;

    // Indicates if this payment has special handling requirements.
    // - none: Regular scheduled payment
    // - final: The last payment that closes out the loan
    // - extra: An additional payment beyond the regular schedule (overpayment)
    PaymentSpecialCase specialCase = PaymentSpecialCase::none;

    // Calculates the tracked interest portion of this payment.
    // This is derived from the other components as:
    // trackedValueDelta - trackedPrincipalDelta - trackedManagementFeeDelta
    //
    // @return The amount of tracked interest included in this payment that
    //         will be paid to the vault.
    Number
    trackedInterestPart() const;
};

/* Extends PaymentComponents with untracked payment amounts.
 *
 * This structure adds untracked fees and interest to the base
 * PaymentComponents, representing amounts that don't affect the Loan object's
 * tracked state but are still part of the total payment due from the borrower.
 *
 * Untracked amounts include:
 * - Late payment fees that go directly to the Broker
 * - Late payment penalty interest that goes directly to the Vault
 * - Service fees
 *
 * The key distinction is that tracked amounts reduce the Loan object's state
 * (sfTotalValueOutstanding, sfPrincipalOutstanding,
 * sfManagementFeeOutstanding), while untracked amounts are paid directly to the
 * recipient without affecting the loan's amortization schedule.
 */
struct ExtendedPaymentComponents : public PaymentComponents
{
    // Additional management fees that go directly to the Broker.
    // This includes fees not part of the standard amortization schedule
    // (e.g., late fees, service fees, origination fees).
    // This value may be negative, though the final value returned in
    // LoanPaymentParts.feePaid will never be negative.
    Number untrackedManagementFee;

    // Additional interest that goes directly to the Vault.
    // This includes interest not part of the standard amortization schedule
    // (e.g., late payment penalty interest).
    // This value may be negative, though the final value returned in
    // LoanPaymentParts.interestPaid will never be negative.
    Number untrackedInterest;

    // The complete amount due from the borrower for this payment.
    // Calculated as: trackedValueDelta + untrackedInterest +
    // untrackedManagementFee
    //
    // This value is used to validate that the payment amount provided by the
    // borrower is sufficient to cover all components of the payment.
    Number totalDue;

    ExtendedPaymentComponents(
        PaymentComponents const& p,
        Number fee,
        Number interest = numZero)
        : PaymentComponents(p)
        , untrackedManagementFee(fee)
        , untrackedInterest(interest)
        , totalDue(
              trackedValueDelta + untrackedInterest + untrackedManagementFee)
    {
    }
};

/* Represents the differences between two loan states.
 *
 * This structure is used to capture the change in each component of a loan's
 * state, typically when computing the difference between two LoanState objects
 * (e.g., before and after a payment). It is a convenient way to capture changes
 * in each component. How that difference is used depends on the context.
 */
struct LoanStateDeltas
{
    // The difference in principal outstanding between two loan states.
    Number principal;

    // The difference in interest due between two loan states.
    Number interest;

    // The difference in management fee outstanding between two loan states.
    Number managementFee;

    /* Calculates the total change across all components.
     * @return The sum of principal, interest, and management fee deltas.
     */
    Number
    total() const
    {
        return principal + interest + managementFee;
    }

    // Ensures all delta values are non-negative.
    void
    nonNegative();
};

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
    TenthBips16 managementFeeRate);

}  // namespace detail

detail::LoanStateDeltas
operator-(LoanState const& lhs, LoanState const& rhs);

LoanState
operator-(LoanState const& lhs, detail::LoanStateDeltas const& rhs);

LoanState
operator+(LoanState const& lhs, detail::LoanStateDeltas const& rhs);

LoanProperties
computeLoanProperties(
    Asset const& asset,
    Number principalOutstanding,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t paymentsRemaining,
    TenthBips32 managementFeeRate,
    std::int32_t minimumScale);

bool
isRounded(Asset const& asset, Number const& value, std::int32_t scale);

// Indicates what type of payment is being made.
// regular, late, and full are mutually exclusive.
// overpayment is an "add on" to a regular payment, and follows that path with
// potential extra work at the end.
enum class LoanPaymentType { regular = 0, late, full, overpayment };

Expected<LoanPaymentParts, TER>
loanMakePayment(
    Asset const& asset,
    ApplyView& view,
    SLE::ref loan,
    SLE::const_ref brokerSle,
    STAmount const& amount,
    LoanPaymentType const paymentType,
    beast::Journal j);

}  // namespace ripple

#endif  // XRPL_APP_MISC_LENDINGHELPERS_H_INCLUDED
