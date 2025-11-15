#ifndef XRPL_APP_MISC_LENDINGHELPERS_H_INCLUDED
#define XRPL_APP_MISC_LENDINGHELPERS_H_INCLUDED

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
inline Number
roundPeriodicPayment(
    Asset const& asset,
    Number const& periodicPayment,
    std::int32_t scale)
{
    return roundToAsset(asset, periodicPayment, scale, Number::upward);
}

/// This structure is explained in the XLS-66 spec, section 3.2.4.4 (Failure
/// Conditions)
struct LoanPaymentParts
{
    /// principal_paid is the amount of principal that the payment covered.
    Number principalPaid = numZero;
    /// interest_paid is the amount of interest that the payment covered.
    Number interestPaid = numZero;
    /**
     * value_change is the amount by which the total value of the Loan changed.
     *  If value_change < 0, Loan value decreased.
     *  If value_change > 0, Loan value increased.
     * This is 0 for regular payments.
     */
    Number valueChange = numZero;
    /// feePaid is amount of fee that is paid to the broker
    Number feePaid = numZero;

    LoanPaymentParts&
    operator+=(LoanPaymentParts const& other);

    bool
    operator==(LoanPaymentParts const& other) const;
};

/** This structure describes the initial computed properties of a loan.
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

TER
checkLoanGuards(
    Asset const& vaultAsset,
    Number const& principalRequested,
    bool expectInterest,
    std::uint32_t paymentTotal,
    LoanProperties const& properties,
    beast::Journal j);

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    Number const& periodicRate,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

LoanState
calculateRawLoanState(
    Number const& periodicPayment,
    TenthBips32 interestRate,
    std::uint32_t paymentInterval,
    std::uint32_t const paymentRemaining,
    TenthBips32 const managementFeeRate);

LoanState
constructRoundedLoanState(
    Number const& totalValueOutstanding,
    Number const& principalOutstanding,
    Number const& managementFeeOutstanding);

LoanState
constructRoundedLoanState(SLE::const_ref loan);

Number
computeManagementFee(
    Asset const& asset,
    Number const& interest,
    TenthBips32 managementFeeRate,
    std::int32_t scale);

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
 * - Origination fees (on first payment)
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
 * in each component.
 *
 * LoanDeltas is primarily used for:
 * - Computing the actual amounts paid during a payment transaction
 * - Validating that loan state changes are correct
 * - Applying incremental changes to a loan state
 *
 */
struct LoanDeltas
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

detail::LoanDeltas
operator-(LoanState const& lhs, LoanState const& rhs);

LoanState
operator-(LoanState const& lhs, detail::LoanDeltas const& rhs);

LoanState
operator+(LoanState const& lhs, detail::LoanDeltas const& rhs);

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
