// Auto-generated unit tests for ledger entry Loan


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Loan.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(LoanTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const loanBrokerNodeValue = canonical_UINT64();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const loanSequenceValue = canonical_UINT32();
    auto const borrowerValue = canonical_ACCOUNT();
    auto const loanOriginationFeeValue = canonical_NUMBER();
    auto const loanServiceFeeValue = canonical_NUMBER();
    auto const latePaymentFeeValue = canonical_NUMBER();
    auto const closePaymentFeeValue = canonical_NUMBER();
    auto const overpaymentFeeValue = canonical_UINT32();
    auto const interestRateValue = canonical_UINT32();
    auto const lateInterestRateValue = canonical_UINT32();
    auto const closeInterestRateValue = canonical_UINT32();
    auto const overpaymentInterestRateValue = canonical_UINT32();
    auto const startDateValue = canonical_UINT32();
    auto const paymentIntervalValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const previousPaymentDueDateValue = canonical_UINT32();
    auto const nextPaymentDueDateValue = canonical_UINT32();
    auto const paymentRemainingValue = canonical_UINT32();
    auto const periodicPaymentValue = canonical_NUMBER();
    auto const principalOutstandingValue = canonical_NUMBER();
    auto const totalValueOutstandingValue = canonical_NUMBER();
    auto const managementFeeOutstandingValue = canonical_NUMBER();
    auto const loanScaleValue = canonical_INT32();

    LoanBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        loanBrokerNodeValue,
        loanBrokerIDValue,
        loanSequenceValue,
        borrowerValue,
        startDateValue,
        paymentIntervalValue,
        periodicPaymentValue
    };

    builder.setLoanOriginationFee(loanOriginationFeeValue);
    builder.setLoanServiceFee(loanServiceFeeValue);
    builder.setLatePaymentFee(latePaymentFeeValue);
    builder.setClosePaymentFee(closePaymentFeeValue);
    builder.setOverpaymentFee(overpaymentFeeValue);
    builder.setInterestRate(interestRateValue);
    builder.setLateInterestRate(lateInterestRateValue);
    builder.setCloseInterestRate(closeInterestRateValue);
    builder.setOverpaymentInterestRate(overpaymentInterestRateValue);
    builder.setGracePeriod(gracePeriodValue);
    builder.setPreviousPaymentDueDate(previousPaymentDueDateValue);
    builder.setNextPaymentDueDate(nextPaymentDueDateValue);
    builder.setPaymentRemaining(paymentRemainingValue);
    builder.setPrincipalOutstanding(principalOutstandingValue);
    builder.setTotalValueOutstanding(totalValueOutstandingValue);
    builder.setManagementFeeOutstanding(managementFeeOutstandingValue);
    builder.setLoanScale(loanScaleValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = previousTxnIDValue;
        auto const actual = entry.getPreviousTxnID();
        expectEqualField(expected, actual, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actual = entry.getPreviousTxnLgrSeq();
        expectEqualField(expected, actual, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = loanBrokerNodeValue;
        auto const actual = entry.getLoanBrokerNode();
        expectEqualField(expected, actual, "sfLoanBrokerNode");
    }

    {
        auto const& expected = loanBrokerIDValue;
        auto const actual = entry.getLoanBrokerID();
        expectEqualField(expected, actual, "sfLoanBrokerID");
    }

    {
        auto const& expected = loanSequenceValue;
        auto const actual = entry.getLoanSequence();
        expectEqualField(expected, actual, "sfLoanSequence");
    }

    {
        auto const& expected = borrowerValue;
        auto const actual = entry.getBorrower();
        expectEqualField(expected, actual, "sfBorrower");
    }

    {
        auto const& expected = startDateValue;
        auto const actual = entry.getStartDate();
        expectEqualField(expected, actual, "sfStartDate");
    }

    {
        auto const& expected = paymentIntervalValue;
        auto const actual = entry.getPaymentInterval();
        expectEqualField(expected, actual, "sfPaymentInterval");
    }

    {
        auto const& expected = periodicPaymentValue;
        auto const actual = entry.getPeriodicPayment();
        expectEqualField(expected, actual, "sfPeriodicPayment");
    }

    {
        auto const& expected = loanOriginationFeeValue;
        auto const actualOpt = entry.getLoanOriginationFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLoanOriginationFee");
        EXPECT_TRUE(entry.hasLoanOriginationFee());
    }

    {
        auto const& expected = loanServiceFeeValue;
        auto const actualOpt = entry.getLoanServiceFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLoanServiceFee");
        EXPECT_TRUE(entry.hasLoanServiceFee());
    }

    {
        auto const& expected = latePaymentFeeValue;
        auto const actualOpt = entry.getLatePaymentFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLatePaymentFee");
        EXPECT_TRUE(entry.hasLatePaymentFee());
    }

    {
        auto const& expected = closePaymentFeeValue;
        auto const actualOpt = entry.getClosePaymentFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfClosePaymentFee");
        EXPECT_TRUE(entry.hasClosePaymentFee());
    }

    {
        auto const& expected = overpaymentFeeValue;
        auto const actualOpt = entry.getOverpaymentFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOverpaymentFee");
        EXPECT_TRUE(entry.hasOverpaymentFee());
    }

    {
        auto const& expected = interestRateValue;
        auto const actualOpt = entry.getInterestRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfInterestRate");
        EXPECT_TRUE(entry.hasInterestRate());
    }

    {
        auto const& expected = lateInterestRateValue;
        auto const actualOpt = entry.getLateInterestRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLateInterestRate");
        EXPECT_TRUE(entry.hasLateInterestRate());
    }

    {
        auto const& expected = closeInterestRateValue;
        auto const actualOpt = entry.getCloseInterestRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCloseInterestRate");
        EXPECT_TRUE(entry.hasCloseInterestRate());
    }

    {
        auto const& expected = overpaymentInterestRateValue;
        auto const actualOpt = entry.getOverpaymentInterestRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOverpaymentInterestRate");
        EXPECT_TRUE(entry.hasOverpaymentInterestRate());
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actualOpt = entry.getGracePeriod();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfGracePeriod");
        EXPECT_TRUE(entry.hasGracePeriod());
    }

    {
        auto const& expected = previousPaymentDueDateValue;
        auto const actualOpt = entry.getPreviousPaymentDueDate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousPaymentDueDate");
        EXPECT_TRUE(entry.hasPreviousPaymentDueDate());
    }

    {
        auto const& expected = nextPaymentDueDateValue;
        auto const actualOpt = entry.getNextPaymentDueDate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfNextPaymentDueDate");
        EXPECT_TRUE(entry.hasNextPaymentDueDate());
    }

    {
        auto const& expected = paymentRemainingValue;
        auto const actualOpt = entry.getPaymentRemaining();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPaymentRemaining");
        EXPECT_TRUE(entry.hasPaymentRemaining());
    }

    {
        auto const& expected = principalOutstandingValue;
        auto const actualOpt = entry.getPrincipalOutstanding();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPrincipalOutstanding");
        EXPECT_TRUE(entry.hasPrincipalOutstanding());
    }

    {
        auto const& expected = totalValueOutstandingValue;
        auto const actualOpt = entry.getTotalValueOutstanding();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTotalValueOutstanding");
        EXPECT_TRUE(entry.hasTotalValueOutstanding());
    }

    {
        auto const& expected = managementFeeOutstandingValue;
        auto const actualOpt = entry.getManagementFeeOutstanding();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfManagementFeeOutstanding");
        EXPECT_TRUE(entry.hasManagementFeeOutstanding());
    }

    {
        auto const& expected = loanScaleValue;
        auto const actualOpt = entry.getLoanScale();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLoanScale");
        EXPECT_TRUE(entry.hasLoanScale());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(LoanTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const loanBrokerNodeValue = canonical_UINT64();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const loanSequenceValue = canonical_UINT32();
    auto const borrowerValue = canonical_ACCOUNT();
    auto const loanOriginationFeeValue = canonical_NUMBER();
    auto const loanServiceFeeValue = canonical_NUMBER();
    auto const latePaymentFeeValue = canonical_NUMBER();
    auto const closePaymentFeeValue = canonical_NUMBER();
    auto const overpaymentFeeValue = canonical_UINT32();
    auto const interestRateValue = canonical_UINT32();
    auto const lateInterestRateValue = canonical_UINT32();
    auto const closeInterestRateValue = canonical_UINT32();
    auto const overpaymentInterestRateValue = canonical_UINT32();
    auto const startDateValue = canonical_UINT32();
    auto const paymentIntervalValue = canonical_UINT32();
    auto const gracePeriodValue = canonical_UINT32();
    auto const previousPaymentDueDateValue = canonical_UINT32();
    auto const nextPaymentDueDateValue = canonical_UINT32();
    auto const paymentRemainingValue = canonical_UINT32();
    auto const periodicPaymentValue = canonical_NUMBER();
    auto const principalOutstandingValue = canonical_NUMBER();
    auto const totalValueOutstandingValue = canonical_NUMBER();
    auto const managementFeeOutstandingValue = canonical_NUMBER();
    auto const loanScaleValue = canonical_INT32();

    auto sle = std::make_shared<SLE>(Loan::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfLoanBrokerNode) = loanBrokerNodeValue;
    sle->at(sfLoanBrokerID) = loanBrokerIDValue;
    sle->at(sfLoanSequence) = loanSequenceValue;
    sle->at(sfBorrower) = borrowerValue;
    sle->at(sfLoanOriginationFee) = loanOriginationFeeValue;
    sle->at(sfLoanServiceFee) = loanServiceFeeValue;
    sle->at(sfLatePaymentFee) = latePaymentFeeValue;
    sle->at(sfClosePaymentFee) = closePaymentFeeValue;
    sle->at(sfOverpaymentFee) = overpaymentFeeValue;
    sle->at(sfInterestRate) = interestRateValue;
    sle->at(sfLateInterestRate) = lateInterestRateValue;
    sle->at(sfCloseInterestRate) = closeInterestRateValue;
    sle->at(sfOverpaymentInterestRate) = overpaymentInterestRateValue;
    sle->at(sfStartDate) = startDateValue;
    sle->at(sfPaymentInterval) = paymentIntervalValue;
    sle->at(sfGracePeriod) = gracePeriodValue;
    sle->at(sfPreviousPaymentDueDate) = previousPaymentDueDateValue;
    sle->at(sfNextPaymentDueDate) = nextPaymentDueDateValue;
    sle->at(sfPaymentRemaining) = paymentRemainingValue;
    sle->at(sfPeriodicPayment) = periodicPaymentValue;
    sle->at(sfPrincipalOutstanding) = principalOutstandingValue;
    sle->at(sfTotalValueOutstanding) = totalValueOutstandingValue;
    sle->at(sfManagementFeeOutstanding) = managementFeeOutstandingValue;
    sle->at(sfLoanScale) = loanScaleValue;

    LoanBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Loan entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = previousTxnIDValue;

        auto const fromSle = entryFromSle.getPreviousTxnID();
        auto const fromBuilder = entryFromBuilder.getPreviousTxnID();

        expectEqualField(expected, fromSle, "sfPreviousTxnID");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSle = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilder = entryFromBuilder.getPreviousTxnLgrSeq();

        expectEqualField(expected, fromSle, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, fromBuilder, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = loanBrokerNodeValue;

        auto const fromSle = entryFromSle.getLoanBrokerNode();
        auto const fromBuilder = entryFromBuilder.getLoanBrokerNode();

        expectEqualField(expected, fromSle, "sfLoanBrokerNode");
        expectEqualField(expected, fromBuilder, "sfLoanBrokerNode");
    }

    {
        auto const& expected = loanBrokerIDValue;

        auto const fromSle = entryFromSle.getLoanBrokerID();
        auto const fromBuilder = entryFromBuilder.getLoanBrokerID();

        expectEqualField(expected, fromSle, "sfLoanBrokerID");
        expectEqualField(expected, fromBuilder, "sfLoanBrokerID");
    }

    {
        auto const& expected = loanSequenceValue;

        auto const fromSle = entryFromSle.getLoanSequence();
        auto const fromBuilder = entryFromBuilder.getLoanSequence();

        expectEqualField(expected, fromSle, "sfLoanSequence");
        expectEqualField(expected, fromBuilder, "sfLoanSequence");
    }

    {
        auto const& expected = borrowerValue;

        auto const fromSle = entryFromSle.getBorrower();
        auto const fromBuilder = entryFromBuilder.getBorrower();

        expectEqualField(expected, fromSle, "sfBorrower");
        expectEqualField(expected, fromBuilder, "sfBorrower");
    }

    {
        auto const& expected = startDateValue;

        auto const fromSle = entryFromSle.getStartDate();
        auto const fromBuilder = entryFromBuilder.getStartDate();

        expectEqualField(expected, fromSle, "sfStartDate");
        expectEqualField(expected, fromBuilder, "sfStartDate");
    }

    {
        auto const& expected = paymentIntervalValue;

        auto const fromSle = entryFromSle.getPaymentInterval();
        auto const fromBuilder = entryFromBuilder.getPaymentInterval();

        expectEqualField(expected, fromSle, "sfPaymentInterval");
        expectEqualField(expected, fromBuilder, "sfPaymentInterval");
    }

    {
        auto const& expected = periodicPaymentValue;

        auto const fromSle = entryFromSle.getPeriodicPayment();
        auto const fromBuilder = entryFromBuilder.getPeriodicPayment();

        expectEqualField(expected, fromSle, "sfPeriodicPayment");
        expectEqualField(expected, fromBuilder, "sfPeriodicPayment");
    }

    {
        auto const& expected = loanOriginationFeeValue;

        auto const fromSleOpt = entryFromSle.getLoanOriginationFee();
        auto const fromBuilderOpt = entryFromBuilder.getLoanOriginationFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLoanOriginationFee");
        expectEqualField(expected, *fromBuilderOpt, "sfLoanOriginationFee");
    }

    {
        auto const& expected = loanServiceFeeValue;

        auto const fromSleOpt = entryFromSle.getLoanServiceFee();
        auto const fromBuilderOpt = entryFromBuilder.getLoanServiceFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLoanServiceFee");
        expectEqualField(expected, *fromBuilderOpt, "sfLoanServiceFee");
    }

    {
        auto const& expected = latePaymentFeeValue;

        auto const fromSleOpt = entryFromSle.getLatePaymentFee();
        auto const fromBuilderOpt = entryFromBuilder.getLatePaymentFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLatePaymentFee");
        expectEqualField(expected, *fromBuilderOpt, "sfLatePaymentFee");
    }

    {
        auto const& expected = closePaymentFeeValue;

        auto const fromSleOpt = entryFromSle.getClosePaymentFee();
        auto const fromBuilderOpt = entryFromBuilder.getClosePaymentFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfClosePaymentFee");
        expectEqualField(expected, *fromBuilderOpt, "sfClosePaymentFee");
    }

    {
        auto const& expected = overpaymentFeeValue;

        auto const fromSleOpt = entryFromSle.getOverpaymentFee();
        auto const fromBuilderOpt = entryFromBuilder.getOverpaymentFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOverpaymentFee");
        expectEqualField(expected, *fromBuilderOpt, "sfOverpaymentFee");
    }

    {
        auto const& expected = interestRateValue;

        auto const fromSleOpt = entryFromSle.getInterestRate();
        auto const fromBuilderOpt = entryFromBuilder.getInterestRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfInterestRate");
        expectEqualField(expected, *fromBuilderOpt, "sfInterestRate");
    }

    {
        auto const& expected = lateInterestRateValue;

        auto const fromSleOpt = entryFromSle.getLateInterestRate();
        auto const fromBuilderOpt = entryFromBuilder.getLateInterestRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLateInterestRate");
        expectEqualField(expected, *fromBuilderOpt, "sfLateInterestRate");
    }

    {
        auto const& expected = closeInterestRateValue;

        auto const fromSleOpt = entryFromSle.getCloseInterestRate();
        auto const fromBuilderOpt = entryFromBuilder.getCloseInterestRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCloseInterestRate");
        expectEqualField(expected, *fromBuilderOpt, "sfCloseInterestRate");
    }

    {
        auto const& expected = overpaymentInterestRateValue;

        auto const fromSleOpt = entryFromSle.getOverpaymentInterestRate();
        auto const fromBuilderOpt = entryFromBuilder.getOverpaymentInterestRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOverpaymentInterestRate");
        expectEqualField(expected, *fromBuilderOpt, "sfOverpaymentInterestRate");
    }

    {
        auto const& expected = gracePeriodValue;

        auto const fromSleOpt = entryFromSle.getGracePeriod();
        auto const fromBuilderOpt = entryFromBuilder.getGracePeriod();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfGracePeriod");
        expectEqualField(expected, *fromBuilderOpt, "sfGracePeriod");
    }

    {
        auto const& expected = previousPaymentDueDateValue;

        auto const fromSleOpt = entryFromSle.getPreviousPaymentDueDate();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousPaymentDueDate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousPaymentDueDate");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousPaymentDueDate");
    }

    {
        auto const& expected = nextPaymentDueDateValue;

        auto const fromSleOpt = entryFromSle.getNextPaymentDueDate();
        auto const fromBuilderOpt = entryFromBuilder.getNextPaymentDueDate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfNextPaymentDueDate");
        expectEqualField(expected, *fromBuilderOpt, "sfNextPaymentDueDate");
    }

    {
        auto const& expected = paymentRemainingValue;

        auto const fromSleOpt = entryFromSle.getPaymentRemaining();
        auto const fromBuilderOpt = entryFromBuilder.getPaymentRemaining();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPaymentRemaining");
        expectEqualField(expected, *fromBuilderOpt, "sfPaymentRemaining");
    }

    {
        auto const& expected = principalOutstandingValue;

        auto const fromSleOpt = entryFromSle.getPrincipalOutstanding();
        auto const fromBuilderOpt = entryFromBuilder.getPrincipalOutstanding();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPrincipalOutstanding");
        expectEqualField(expected, *fromBuilderOpt, "sfPrincipalOutstanding");
    }

    {
        auto const& expected = totalValueOutstandingValue;

        auto const fromSleOpt = entryFromSle.getTotalValueOutstanding();
        auto const fromBuilderOpt = entryFromBuilder.getTotalValueOutstanding();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTotalValueOutstanding");
        expectEqualField(expected, *fromBuilderOpt, "sfTotalValueOutstanding");
    }

    {
        auto const& expected = managementFeeOutstandingValue;

        auto const fromSleOpt = entryFromSle.getManagementFeeOutstanding();
        auto const fromBuilderOpt = entryFromBuilder.getManagementFeeOutstanding();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfManagementFeeOutstanding");
        expectEqualField(expected, *fromBuilderOpt, "sfManagementFeeOutstanding");
    }

    {
        auto const& expected = loanScaleValue;

        auto const fromSleOpt = entryFromSle.getLoanScale();
        auto const fromBuilderOpt = entryFromBuilder.getLoanScale();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLoanScale");
        expectEqualField(expected, *fromBuilderOpt, "sfLoanScale");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(LoanTests, WrapperThrowsOnWrongEntryType)
{
    uint256 const index{3u};

    // Build a valid ledger entry of a different type
    // Ticket requires: Account, OwnerNode, TicketSequence, PreviousTxnID, PreviousTxnLgrSeq
    // Check requires: Account, Destination, SendMax, Sequence, OwnerNode, DestinationNode, PreviousTxnID, PreviousTxnLgrSeq
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(Loan{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(LoanTests, BuilderThrowsOnWrongEntryType)
{
    uint256 const index{4u};

    // Build a valid ledger entry of a different type
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(LoanBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(LoanTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const loanBrokerNodeValue = canonical_UINT64();
    auto const loanBrokerIDValue = canonical_UINT256();
    auto const loanSequenceValue = canonical_UINT32();
    auto const borrowerValue = canonical_ACCOUNT();
    auto const startDateValue = canonical_UINT32();
    auto const paymentIntervalValue = canonical_UINT32();
    auto const periodicPaymentValue = canonical_NUMBER();

    LoanBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        loanBrokerNodeValue,
        loanBrokerIDValue,
        loanSequenceValue,
        borrowerValue,
        startDateValue,
        paymentIntervalValue,
        periodicPaymentValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasLoanOriginationFee());
    EXPECT_FALSE(entry.getLoanOriginationFee().has_value());
    EXPECT_FALSE(entry.hasLoanServiceFee());
    EXPECT_FALSE(entry.getLoanServiceFee().has_value());
    EXPECT_FALSE(entry.hasLatePaymentFee());
    EXPECT_FALSE(entry.getLatePaymentFee().has_value());
    EXPECT_FALSE(entry.hasClosePaymentFee());
    EXPECT_FALSE(entry.getClosePaymentFee().has_value());
    EXPECT_FALSE(entry.hasOverpaymentFee());
    EXPECT_FALSE(entry.getOverpaymentFee().has_value());
    EXPECT_FALSE(entry.hasInterestRate());
    EXPECT_FALSE(entry.getInterestRate().has_value());
    EXPECT_FALSE(entry.hasLateInterestRate());
    EXPECT_FALSE(entry.getLateInterestRate().has_value());
    EXPECT_FALSE(entry.hasCloseInterestRate());
    EXPECT_FALSE(entry.getCloseInterestRate().has_value());
    EXPECT_FALSE(entry.hasOverpaymentInterestRate());
    EXPECT_FALSE(entry.getOverpaymentInterestRate().has_value());
    EXPECT_FALSE(entry.hasGracePeriod());
    EXPECT_FALSE(entry.getGracePeriod().has_value());
    EXPECT_FALSE(entry.hasPreviousPaymentDueDate());
    EXPECT_FALSE(entry.getPreviousPaymentDueDate().has_value());
    EXPECT_FALSE(entry.hasNextPaymentDueDate());
    EXPECT_FALSE(entry.getNextPaymentDueDate().has_value());
    EXPECT_FALSE(entry.hasPaymentRemaining());
    EXPECT_FALSE(entry.getPaymentRemaining().has_value());
    EXPECT_FALSE(entry.hasPrincipalOutstanding());
    EXPECT_FALSE(entry.getPrincipalOutstanding().has_value());
    EXPECT_FALSE(entry.hasTotalValueOutstanding());
    EXPECT_FALSE(entry.getTotalValueOutstanding().has_value());
    EXPECT_FALSE(entry.hasManagementFeeOutstanding());
    EXPECT_FALSE(entry.getManagementFeeOutstanding().has_value());
    EXPECT_FALSE(entry.hasLoanScale());
    EXPECT_FALSE(entry.getLoanScale().has_value());
}
}
