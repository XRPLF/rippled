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
    /// Interest still due to be paid TO the Vault.
    // This is a portion of interestOutstanding
    Number interestDue;
    /// Management fee still due to be paid TO the broker.
    // This is a portion of interestOutstanding
    Number managementFeeDue;

    /// Interest still due to be paid by the borrower.
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

/// This structure is used internally to compute the breakdown of a
/// single loan payment
struct PaymentComponents
{
    // tracked values are rounded to the asset and loan scale, and correspond to
    // fields in the Loan ledger object.
    // trackedValueDelta modifies sfTotalValueOutstanding.
    Number trackedValueDelta;
    // trackedPrincipalDelta modifies sfPrincipalOutstanding.
    Number trackedPrincipalDelta;
    // trackedManagementFeeDelta modifies sfManagementFeeOutstanding. It will
    // not include any "extra" fees that go directly to the broker, such as late
    // fees.
    Number trackedManagementFeeDelta;

    PaymentSpecialCase specialCase = PaymentSpecialCase::none;

    Number
    trackedInterestPart() const;
};

// This structure describes the difference between two LoanState objects so that
// the differences between components don't have to be tracked individually,
// risking more errors. How that difference is used depends on the context.
struct LoanStateDeltas
{
    Number principal;
    Number interest;
    Number managementFee;

    Number
    total() const
    {
        return principal + interest + managementFee;
    }

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
