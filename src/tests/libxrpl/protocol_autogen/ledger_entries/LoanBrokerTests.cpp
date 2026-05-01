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
    };

    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);
    builder.setSequence(sequenceValue);
    builder.setOwnerNode(ownerNodeValue);
    builder.setVaultNode(vaultNodeValue);
    builder.setVaultID(vaultIDValue);
    builder.setAccount(accountValue);
    builder.setOwner(ownerValue);
    builder.setLoanSequence(loanSequenceValue);
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
        auto const actualOpt = entry.getPreviousTxnID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnID");
        EXPECT_TRUE(entry.hasPreviousTxnID());
    }

    {
        auto const& expected = previousTxnLgrSeqValue;
        auto const actualOpt = entry.getPreviousTxnLgrSeq();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPreviousTxnLgrSeq");
        EXPECT_TRUE(entry.hasPreviousTxnLgrSeq());
    }

    {
        auto const& expected = sequenceValue;
        auto const actualOpt = entry.getSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSequence");
        EXPECT_TRUE(entry.hasSequence());
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actualOpt = entry.getOwnerNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerNode");
        EXPECT_TRUE(entry.hasOwnerNode());
    }

    {
        auto const& expected = vaultNodeValue;
        auto const actualOpt = entry.getVaultNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultNode");
        EXPECT_TRUE(entry.hasVaultNode());
    }

    {
        auto const& expected = vaultIDValue;
        auto const actualOpt = entry.getVaultID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultID");
        EXPECT_TRUE(entry.hasVaultID());
    }

    {
        auto const& expected = accountValue;
        auto const actualOpt = entry.getAccount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAccount");
        EXPECT_TRUE(entry.hasAccount());
    }

    {
        auto const& expected = ownerValue;
        auto const actualOpt = entry.getOwner();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwner");
        EXPECT_TRUE(entry.hasOwner());
    }

    {
        auto const& expected = loanSequenceValue;
        auto const actualOpt = entry.getLoanSequence();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLoanSequence");
        EXPECT_TRUE(entry.hasLoanSequence());
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

        auto const fromSleOpt = entryFromSle.getPreviousTxnID();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousTxnID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnID");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnID");
    }

    {
        auto const& expected = previousTxnLgrSeqValue;

        auto const fromSleOpt = entryFromSle.getPreviousTxnLgrSeq();
        auto const fromBuilderOpt = entryFromBuilder.getPreviousTxnLgrSeq();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPreviousTxnLgrSeq");
        expectEqualField(expected, *fromBuilderOpt, "sfPreviousTxnLgrSeq");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSleOpt = entryFromSle.getSequence();
        auto const fromBuilderOpt = entryFromBuilder.getSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfSequence");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSleOpt = entryFromSle.getOwnerNode();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerNode");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerNode");
    }

    {
        auto const& expected = vaultNodeValue;

        auto const fromSleOpt = entryFromSle.getVaultNode();
        auto const fromBuilderOpt = entryFromBuilder.getVaultNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultNode");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultNode");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSleOpt = entryFromSle.getVaultID();
        auto const fromBuilderOpt = entryFromBuilder.getVaultID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultID");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultID");
    }

    {
        auto const& expected = accountValue;

        auto const fromSleOpt = entryFromSle.getAccount();
        auto const fromBuilderOpt = entryFromBuilder.getAccount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAccount");
        expectEqualField(expected, *fromBuilderOpt, "sfAccount");
    }

    {
        auto const& expected = ownerValue;

        auto const fromSleOpt = entryFromSle.getOwner();
        auto const fromBuilderOpt = entryFromBuilder.getOwner();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwner");
        expectEqualField(expected, *fromBuilderOpt, "sfOwner");
    }

    {
        auto const& expected = loanSequenceValue;

        auto const fromSleOpt = entryFromSle.getLoanSequence();
        auto const fromBuilderOpt = entryFromBuilder.getLoanSequence();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLoanSequence");
        expectEqualField(expected, *fromBuilderOpt, "sfLoanSequence");
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


    LoanBrokerBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
    EXPECT_FALSE(entry.hasSequence());
    EXPECT_FALSE(entry.getSequence().has_value());
    EXPECT_FALSE(entry.hasOwnerNode());
    EXPECT_FALSE(entry.getOwnerNode().has_value());
    EXPECT_FALSE(entry.hasVaultNode());
    EXPECT_FALSE(entry.getVaultNode().has_value());
    EXPECT_FALSE(entry.hasVaultID());
    EXPECT_FALSE(entry.getVaultID().has_value());
    EXPECT_FALSE(entry.hasAccount());
    EXPECT_FALSE(entry.getAccount().has_value());
    EXPECT_FALSE(entry.hasOwner());
    EXPECT_FALSE(entry.getOwner().has_value());
    EXPECT_FALSE(entry.hasLoanSequence());
    EXPECT_FALSE(entry.getLoanSequence().has_value());
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
