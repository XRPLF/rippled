// Auto-generated unit tests for ledger entry LoanBroker


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/LoanBroker.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(LoanBrokerTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const accountValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const loanSequenceValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const ownerCountValue = canonical_UINT32();
    auto const debtTotalValue = canonical_NUMBER();
    auto const debtMaximumValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();

    LoanBrokerBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        vaultNodeValue,
        vaultIDValue,
        accountValue,
        ownerValue,
        loanSequenceValue
    };

    builder.setData(dataValue);
    builder.setManagementFeeRate(managementFeeRateValue);
    builder.setOwnerCount(ownerCountValue);
    builder.setDebtTotal(debtTotalValue);
    builder.setDebtMaximum(debtMaximumValue);
    builder.setCoverAvailable(coverAvailableValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setCoverRateLiquidation(coverRateLiquidationValue);

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
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = vaultNodeValue;
        auto const actual = entry.getVaultNode();
        expectEqualField(expected, actual, "sfVaultNode");
    }

    {
        auto const& expected = vaultIDValue;
        auto const actual = entry.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = loanSequenceValue;
        auto const actual = entry.getLoanSequence();
        expectEqualField(expected, actual, "sfLoanSequence");
    }

    {
        auto const& expected = dataValue;
        auto const actualOpt = entry.getData();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(entry.hasData());
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = entry.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
        EXPECT_TRUE(entry.hasManagementFeeRate());
    }

    {
        auto const& expected = ownerCountValue;
        auto const actualOpt = entry.getOwnerCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerCount");
        EXPECT_TRUE(entry.hasOwnerCount());
    }

    {
        auto const& expected = debtTotalValue;
        auto const actualOpt = entry.getDebtTotal();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDebtTotal");
        EXPECT_TRUE(entry.hasDebtTotal());
    }

    {
        auto const& expected = debtMaximumValue;
        auto const actualOpt = entry.getDebtMaximum();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDebtMaximum");
        EXPECT_TRUE(entry.hasDebtMaximum());
    }

    {
        auto const& expected = coverAvailableValue;
        auto const actualOpt = entry.getCoverAvailable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverAvailable");
        EXPECT_TRUE(entry.hasCoverAvailable());
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = entry.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
        EXPECT_TRUE(entry.hasCoverRateMinimum());
    }

    {
        auto const& expected = coverRateLiquidationValue;
        auto const actualOpt = entry.getCoverRateLiquidation();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverRateLiquidation");
        EXPECT_TRUE(entry.hasCoverRateLiquidation());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(LoanBrokerTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const accountValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const loanSequenceValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const ownerCountValue = canonical_UINT32();
    auto const debtTotalValue = canonical_NUMBER();
    auto const debtMaximumValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(LoanBroker::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfVaultNode) = vaultNodeValue;
    sle->at(sfVaultID) = vaultIDValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfLoanSequence) = loanSequenceValue;
    sle->at(sfData) = dataValue;
    sle->at(sfManagementFeeRate) = managementFeeRateValue;
    sle->at(sfOwnerCount) = ownerCountValue;
    sle->at(sfDebtTotal) = debtTotalValue;
    sle->at(sfDebtMaximum) = debtMaximumValue;
    sle->at(sfCoverAvailable) = coverAvailableValue;
    sle->at(sfCoverRateMinimum) = coverRateMinimumValue;
    sle->at(sfCoverRateLiquidation) = coverRateLiquidationValue;

    LoanBrokerBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    LoanBroker entryFromSle{sle};
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
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = vaultNodeValue;

        auto const fromSle = entryFromSle.getVaultNode();
        auto const fromBuilder = entryFromBuilder.getVaultNode();

        expectEqualField(expected, fromSle, "sfVaultNode");
        expectEqualField(expected, fromBuilder, "sfVaultNode");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSle = entryFromSle.getVaultID();
        auto const fromBuilder = entryFromBuilder.getVaultID();

        expectEqualField(expected, fromSle, "sfVaultID");
        expectEqualField(expected, fromBuilder, "sfVaultID");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = loanSequenceValue;

        auto const fromSle = entryFromSle.getLoanSequence();
        auto const fromBuilder = entryFromBuilder.getLoanSequence();

        expectEqualField(expected, fromSle, "sfLoanSequence");
        expectEqualField(expected, fromBuilder, "sfLoanSequence");
    }

    {
        auto const& expected = dataValue;

        auto const fromSleOpt = entryFromSle.getData();
        auto const fromBuilderOpt = entryFromBuilder.getData();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfData");
        expectEqualField(expected, *fromBuilderOpt, "sfData");
    }

    {
        auto const& expected = managementFeeRateValue;

        auto const fromSleOpt = entryFromSle.getManagementFeeRate();
        auto const fromBuilderOpt = entryFromBuilder.getManagementFeeRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfManagementFeeRate");
        expectEqualField(expected, *fromBuilderOpt, "sfManagementFeeRate");
    }

    {
        auto const& expected = ownerCountValue;

        auto const fromSleOpt = entryFromSle.getOwnerCount();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerCount");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerCount");
    }

    {
        auto const& expected = debtTotalValue;

        auto const fromSleOpt = entryFromSle.getDebtTotal();
        auto const fromBuilderOpt = entryFromBuilder.getDebtTotal();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDebtTotal");
        expectEqualField(expected, *fromBuilderOpt, "sfDebtTotal");
    }

    {
        auto const& expected = debtMaximumValue;

        auto const fromSleOpt = entryFromSle.getDebtMaximum();
        auto const fromBuilderOpt = entryFromBuilder.getDebtMaximum();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDebtMaximum");
        expectEqualField(expected, *fromBuilderOpt, "sfDebtMaximum");
    }

    {
        auto const& expected = coverAvailableValue;

        auto const fromSleOpt = entryFromSle.getCoverAvailable();
        auto const fromBuilderOpt = entryFromBuilder.getCoverAvailable();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverAvailable");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverAvailable");
    }

    {
        auto const& expected = coverRateMinimumValue;

        auto const fromSleOpt = entryFromSle.getCoverRateMinimum();
        auto const fromBuilderOpt = entryFromBuilder.getCoverRateMinimum();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverRateMinimum");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverRateMinimum");
    }

    {
        auto const& expected = coverRateLiquidationValue;

        auto const fromSleOpt = entryFromSle.getCoverRateLiquidation();
        auto const fromBuilderOpt = entryFromBuilder.getCoverRateLiquidation();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverRateLiquidation");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverRateLiquidation");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(LoanBrokerTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(LoanBroker{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(LoanBrokerTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(LoanBrokerBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(LoanBrokerTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const accountValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const loanSequenceValue = canonical_UINT32();

    LoanBrokerBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        vaultNodeValue,
        vaultIDValue,
        accountValue,
        ownerValue,
        loanSequenceValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasData());
    EXPECT_FALSE(entry.getData().has_value());
    EXPECT_FALSE(entry.hasManagementFeeRate());
    EXPECT_FALSE(entry.getManagementFeeRate().has_value());
    EXPECT_FALSE(entry.hasOwnerCount());
    EXPECT_FALSE(entry.getOwnerCount().has_value());
    EXPECT_FALSE(entry.hasDebtTotal());
    EXPECT_FALSE(entry.getDebtTotal().has_value());
    EXPECT_FALSE(entry.hasDebtMaximum());
    EXPECT_FALSE(entry.getDebtMaximum().has_value());
    EXPECT_FALSE(entry.hasCoverAvailable());
    EXPECT_FALSE(entry.getCoverAvailable().has_value());
    EXPECT_FALSE(entry.hasCoverRateMinimum());
    EXPECT_FALSE(entry.getCoverRateMinimum().has_value());
    EXPECT_FALSE(entry.hasCoverRateLiquidation());
    EXPECT_FALSE(entry.getCoverRateLiquidation().has_value());
}
}
