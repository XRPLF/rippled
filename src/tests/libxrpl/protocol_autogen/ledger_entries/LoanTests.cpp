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
        auto const actual = entry.getLoanOriginationFee();
        expectEqualField(expected, actual, "sfLoanOriginationFee");
    }

    {
        auto const& expected = loanServiceFeeValue;
        auto const actual = entry.getLoanServiceFee();
        expectEqualField(expected, actual, "sfLoanServiceFee");
    }

    {
        auto const& expected = latePaymentFeeValue;
        auto const actual = entry.getLatePaymentFee();
        expectEqualField(expected, actual, "sfLatePaymentFee");
    }

    {
        auto const& expected = closePaymentFeeValue;
        auto const actual = entry.getClosePaymentFee();
        expectEqualField(expected, actual, "sfClosePaymentFee");
    }

    {
        auto const& expected = overpaymentFeeValue;
        auto const actual = entry.getOverpaymentFee();
        expectEqualField(expected, actual, "sfOverpaymentFee");
    }

    {
        auto const& expected = interestRateValue;
        auto const actual = entry.getInterestRate();
        expectEqualField(expected, actual, "sfInterestRate");
    }

    {
        auto const& expected = lateInterestRateValue;
        auto const actual = entry.getLateInterestRate();
        expectEqualField(expected, actual, "sfLateInterestRate");
    }

    {
        auto const& expected = closeInterestRateValue;
        auto const actual = entry.getCloseInterestRate();
        expectEqualField(expected, actual, "sfCloseInterestRate");
    }

    {
        auto const& expected = overpaymentInterestRateValue;
        auto const actual = entry.getOverpaymentInterestRate();
        expectEqualField(expected, actual, "sfOverpaymentInterestRate");
    }

    {
        auto const& expected = gracePeriodValue;
        auto const actual = entry.getGracePeriod();
        expectEqualField(expected, actual, "sfGracePeriod");
    }

    {
        auto const& expected = previousPaymentDueDateValue;
        auto const actual = entry.getPreviousPaymentDueDate();
        expectEqualField(expected, actual, "sfPreviousPaymentDueDate");
    }

    {
        auto const& expected = nextPaymentDueDateValue;
        auto const actual = entry.getNextPaymentDueDate();
        expectEqualField(expected, actual, "sfNextPaymentDueDate");
    }

    {
        auto const& expected = paymentRemainingValue;
        auto const actual = entry.getPaymentRemaining();
        expectEqualField(expected, actual, "sfPaymentRemaining");
    }

    {
        auto const& expected = principalOutstandingValue;
        auto const actual = entry.getPrincipalOutstanding();
        expectEqualField(expected, actual, "sfPrincipalOutstanding");
    }

    {
        auto const& expected = totalValueOutstandingValue;
        auto const actual = entry.getTotalValueOutstanding();
        expectEqualField(expected, actual, "sfTotalValueOutstanding");
    }

    {
        auto const& expected = managementFeeOutstandingValue;
        auto const actual = entry.getManagementFeeOutstanding();
        expectEqualField(expected, actual, "sfManagementFeeOutstanding");
    }

    {
        auto const& expected = loanScaleValue;
        auto const actual = entry.getLoanScale();
        expectEqualField(expected, actual, "sfLoanScale");
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

        auto const fromSle = entryFromSle.getLoanOriginationFee();
        auto const fromBuilder = entryFromBuilder.getLoanOriginationFee();

        expectEqualField(expected, fromSle, "sfLoanOriginationFee");
        expectEqualField(expected, fromBuilder, "sfLoanOriginationFee");
    }

    {
        auto const& expected = loanServiceFeeValue;

        auto const fromSle = entryFromSle.getLoanServiceFee();
        auto const fromBuilder = entryFromBuilder.getLoanServiceFee();

        expectEqualField(expected, fromSle, "sfLoanServiceFee");
        expectEqualField(expected, fromBuilder, "sfLoanServiceFee");
    }

    {
        auto const& expected = latePaymentFeeValue;

        auto const fromSle = entryFromSle.getLatePaymentFee();
        auto const fromBuilder = entryFromBuilder.getLatePaymentFee();

        expectEqualField(expected, fromSle, "sfLatePaymentFee");
        expectEqualField(expected, fromBuilder, "sfLatePaymentFee");
    }

    {
        auto const& expected = closePaymentFeeValue;

        auto const fromSle = entryFromSle.getClosePaymentFee();
        auto const fromBuilder = entryFromBuilder.getClosePaymentFee();

        expectEqualField(expected, fromSle, "sfClosePaymentFee");
        expectEqualField(expected, fromBuilder, "sfClosePaymentFee");
    }

    {
        auto const& expected = overpaymentFeeValue;

        auto const fromSle = entryFromSle.getOverpaymentFee();
        auto const fromBuilder = entryFromBuilder.getOverpaymentFee();

        expectEqualField(expected, fromSle, "sfOverpaymentFee");
        expectEqualField(expected, fromBuilder, "sfOverpaymentFee");
    }

    {
        auto const& expected = interestRateValue;

        auto const fromSle = entryFromSle.getInterestRate();
        auto const fromBuilder = entryFromBuilder.getInterestRate();

        expectEqualField(expected, fromSle, "sfInterestRate");
        expectEqualField(expected, fromBuilder, "sfInterestRate");
    }

    {
        auto const& expected = lateInterestRateValue;

        auto const fromSle = entryFromSle.getLateInterestRate();
        auto const fromBuilder = entryFromBuilder.getLateInterestRate();

        expectEqualField(expected, fromSle, "sfLateInterestRate");
        expectEqualField(expected, fromBuilder, "sfLateInterestRate");
    }

    {
        auto const& expected = closeInterestRateValue;

        auto const fromSle = entryFromSle.getCloseInterestRate();
        auto const fromBuilder = entryFromBuilder.getCloseInterestRate();

        expectEqualField(expected, fromSle, "sfCloseInterestRate");
        expectEqualField(expected, fromBuilder, "sfCloseInterestRate");
    }

    {
        auto const& expected = overpaymentInterestRateValue;

        auto const fromSle = entryFromSle.getOverpaymentInterestRate();
        auto const fromBuilder = entryFromBuilder.getOverpaymentInterestRate();

        expectEqualField(expected, fromSle, "sfOverpaymentInterestRate");
        expectEqualField(expected, fromBuilder, "sfOverpaymentInterestRate");
    }

    {
        auto const& expected = gracePeriodValue;

        auto const fromSle = entryFromSle.getGracePeriod();
        auto const fromBuilder = entryFromBuilder.getGracePeriod();

        expectEqualField(expected, fromSle, "sfGracePeriod");
        expectEqualField(expected, fromBuilder, "sfGracePeriod");
    }

    {
        auto const& expected = previousPaymentDueDateValue;

        auto const fromSle = entryFromSle.getPreviousPaymentDueDate();
        auto const fromBuilder = entryFromBuilder.getPreviousPaymentDueDate();

        expectEqualField(expected, fromSle, "sfPreviousPaymentDueDate");
        expectEqualField(expected, fromBuilder, "sfPreviousPaymentDueDate");
    }

    {
        auto const& expected = nextPaymentDueDateValue;

        auto const fromSle = entryFromSle.getNextPaymentDueDate();
        auto const fromBuilder = entryFromBuilder.getNextPaymentDueDate();

        expectEqualField(expected, fromSle, "sfNextPaymentDueDate");
        expectEqualField(expected, fromBuilder, "sfNextPaymentDueDate");
    }

    {
        auto const& expected = paymentRemainingValue;

        auto const fromSle = entryFromSle.getPaymentRemaining();
        auto const fromBuilder = entryFromBuilder.getPaymentRemaining();

        expectEqualField(expected, fromSle, "sfPaymentRemaining");
        expectEqualField(expected, fromBuilder, "sfPaymentRemaining");
    }

    {
        auto const& expected = principalOutstandingValue;

        auto const fromSle = entryFromSle.getPrincipalOutstanding();
        auto const fromBuilder = entryFromBuilder.getPrincipalOutstanding();

        expectEqualField(expected, fromSle, "sfPrincipalOutstanding");
        expectEqualField(expected, fromBuilder, "sfPrincipalOutstanding");
    }

    {
        auto const& expected = totalValueOutstandingValue;

        auto const fromSle = entryFromSle.getTotalValueOutstanding();
        auto const fromBuilder = entryFromBuilder.getTotalValueOutstanding();

        expectEqualField(expected, fromSle, "sfTotalValueOutstanding");
        expectEqualField(expected, fromBuilder, "sfTotalValueOutstanding");
    }

    {
        auto const& expected = managementFeeOutstandingValue;

        auto const fromSle = entryFromSle.getManagementFeeOutstanding();
        auto const fromBuilder = entryFromBuilder.getManagementFeeOutstanding();

        expectEqualField(expected, fromSle, "sfManagementFeeOutstanding");
        expectEqualField(expected, fromBuilder, "sfManagementFeeOutstanding");
    }

    {
        auto const& expected = loanScaleValue;

        auto const fromSle = entryFromSle.getLoanScale();
        auto const fromBuilder = entryFromBuilder.getLoanScale();

        expectEqualField(expected, fromSle, "sfLoanScale");
        expectEqualField(expected, fromBuilder, "sfLoanScale");
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


// 6) Default fields return the type default when unset, and the assigned value
// after being set.
TEST(LoanTests, DefaultFieldsRoundTrip)
{
    uint256 const index{4u};

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

    // Unset: default fields return the type default.
    LoanBuilder defaultBuilder{
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
    auto const defaultEntry = defaultBuilder.build(index);
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getLoanOriginationFee(), "sfLoanOriginationFee");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getLoanServiceFee(), "sfLoanServiceFee");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getLatePaymentFee(), "sfLatePaymentFee");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getClosePaymentFee(), "sfClosePaymentFee");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getOverpaymentFee(), "sfOverpaymentFee");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getInterestRate(), "sfInterestRate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getLateInterestRate(), "sfLateInterestRate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getCloseInterestRate(), "sfCloseInterestRate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getOverpaymentInterestRate(), "sfOverpaymentInterestRate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getGracePeriod(), "sfGracePeriod");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getPreviousPaymentDueDate(), "sfPreviousPaymentDueDate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getNextPaymentDueDate(), "sfNextPaymentDueDate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getPaymentRemaining(), "sfPaymentRemaining");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getPrincipalOutstanding(), "sfPrincipalOutstanding");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getTotalValueOutstanding(), "sfTotalValueOutstanding");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getManagementFeeOutstanding(), "sfManagementFeeOutstanding");
    }
    {
        auto const expected = SF_INT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getLoanScale(), "sfLoanScale");
    }

    // Set: default fields return the assigned value.
    LoanBuilder setBuilder{
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
    setBuilder.setLoanOriginationFee(canonical_NUMBER());
    setBuilder.setLoanServiceFee(canonical_NUMBER());
    setBuilder.setLatePaymentFee(canonical_NUMBER());
    setBuilder.setClosePaymentFee(canonical_NUMBER());
    setBuilder.setOverpaymentFee(canonical_UINT32());
    setBuilder.setInterestRate(canonical_UINT32());
    setBuilder.setLateInterestRate(canonical_UINT32());
    setBuilder.setCloseInterestRate(canonical_UINT32());
    setBuilder.setOverpaymentInterestRate(canonical_UINT32());
    setBuilder.setGracePeriod(canonical_UINT32());
    setBuilder.setPreviousPaymentDueDate(canonical_UINT32());
    setBuilder.setNextPaymentDueDate(canonical_UINT32());
    setBuilder.setPaymentRemaining(canonical_UINT32());
    setBuilder.setPrincipalOutstanding(canonical_NUMBER());
    setBuilder.setTotalValueOutstanding(canonical_NUMBER());
    setBuilder.setManagementFeeOutstanding(canonical_NUMBER());
    setBuilder.setLoanScale(canonical_INT32());
    auto const setEntry = setBuilder.build(index);
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getLoanOriginationFee(), "sfLoanOriginationFee");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getLoanServiceFee(), "sfLoanServiceFee");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getLatePaymentFee(), "sfLatePaymentFee");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getClosePaymentFee(), "sfClosePaymentFee");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getOverpaymentFee(), "sfOverpaymentFee");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getInterestRate(), "sfInterestRate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getLateInterestRate(), "sfLateInterestRate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getCloseInterestRate(), "sfCloseInterestRate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getOverpaymentInterestRate(), "sfOverpaymentInterestRate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getGracePeriod(), "sfGracePeriod");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getPreviousPaymentDueDate(), "sfPreviousPaymentDueDate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getNextPaymentDueDate(), "sfNextPaymentDueDate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getPaymentRemaining(), "sfPaymentRemaining");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getPrincipalOutstanding(), "sfPrincipalOutstanding");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getTotalValueOutstanding(), "sfTotalValueOutstanding");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getManagementFeeOutstanding(), "sfManagementFeeOutstanding");
    }
    {
        auto const expected = canonical_INT32();
        expectEqualField(expected, setEntry.getLoanScale(), "sfLoanScale");
    }
}
}
