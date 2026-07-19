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
        auto const actual = entry.getData();
        expectEqualField(expected, actual, "sfData");
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actual = entry.getManagementFeeRate();
        expectEqualField(expected, actual, "sfManagementFeeRate");
    }

    {
        auto const& expected = ownerCountValue;
        auto const actual = entry.getOwnerCount();
        expectEqualField(expected, actual, "sfOwnerCount");
    }

    {
        auto const& expected = debtTotalValue;
        auto const actual = entry.getDebtTotal();
        expectEqualField(expected, actual, "sfDebtTotal");
    }

    {
        auto const& expected = debtMaximumValue;
        auto const actual = entry.getDebtMaximum();
        expectEqualField(expected, actual, "sfDebtMaximum");
    }

    {
        auto const& expected = coverAvailableValue;
        auto const actual = entry.getCoverAvailable();
        expectEqualField(expected, actual, "sfCoverAvailable");
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actual = entry.getCoverRateMinimum();
        expectEqualField(expected, actual, "sfCoverRateMinimum");
    }

    {
        auto const& expected = coverRateLiquidationValue;
        auto const actual = entry.getCoverRateLiquidation();
        expectEqualField(expected, actual, "sfCoverRateLiquidation");
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

        auto const fromSle = entryFromSle.getData();
        auto const fromBuilder = entryFromBuilder.getData();

        expectEqualField(expected, fromSle, "sfData");
        expectEqualField(expected, fromBuilder, "sfData");
    }

    {
        auto const& expected = managementFeeRateValue;

        auto const fromSle = entryFromSle.getManagementFeeRate();
        auto const fromBuilder = entryFromBuilder.getManagementFeeRate();

        expectEqualField(expected, fromSle, "sfManagementFeeRate");
        expectEqualField(expected, fromBuilder, "sfManagementFeeRate");
    }

    {
        auto const& expected = ownerCountValue;

        auto const fromSle = entryFromSle.getOwnerCount();
        auto const fromBuilder = entryFromBuilder.getOwnerCount();

        expectEqualField(expected, fromSle, "sfOwnerCount");
        expectEqualField(expected, fromBuilder, "sfOwnerCount");
    }

    {
        auto const& expected = debtTotalValue;

        auto const fromSle = entryFromSle.getDebtTotal();
        auto const fromBuilder = entryFromBuilder.getDebtTotal();

        expectEqualField(expected, fromSle, "sfDebtTotal");
        expectEqualField(expected, fromBuilder, "sfDebtTotal");
    }

    {
        auto const& expected = debtMaximumValue;

        auto const fromSle = entryFromSle.getDebtMaximum();
        auto const fromBuilder = entryFromBuilder.getDebtMaximum();

        expectEqualField(expected, fromSle, "sfDebtMaximum");
        expectEqualField(expected, fromBuilder, "sfDebtMaximum");
    }

    {
        auto const& expected = coverAvailableValue;

        auto const fromSle = entryFromSle.getCoverAvailable();
        auto const fromBuilder = entryFromBuilder.getCoverAvailable();

        expectEqualField(expected, fromSle, "sfCoverAvailable");
        expectEqualField(expected, fromBuilder, "sfCoverAvailable");
    }

    {
        auto const& expected = coverRateMinimumValue;

        auto const fromSle = entryFromSle.getCoverRateMinimum();
        auto const fromBuilder = entryFromBuilder.getCoverRateMinimum();

        expectEqualField(expected, fromSle, "sfCoverRateMinimum");
        expectEqualField(expected, fromBuilder, "sfCoverRateMinimum");
    }

    {
        auto const& expected = coverRateLiquidationValue;

        auto const fromSle = entryFromSle.getCoverRateLiquidation();
        auto const fromBuilder = entryFromBuilder.getCoverRateLiquidation();

        expectEqualField(expected, fromSle, "sfCoverRateLiquidation");
        expectEqualField(expected, fromBuilder, "sfCoverRateLiquidation");
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


// 6) Default fields return the type default when unset, and the assigned value
// after being set.
TEST(LoanBrokerTests, DefaultFieldsRoundTrip)
{
    uint256 const index{4u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const accountValue = canonical_ACCOUNT();
    auto const ownerValue = canonical_ACCOUNT();
    auto const loanSequenceValue = canonical_UINT32();

    // Unset: default fields return the type default.
    LoanBrokerBuilder defaultBuilder{
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
    auto const defaultEntry = defaultBuilder.build(index);
    {
        auto const expected = SF_VL::type::value_type{};
        expectEqualField(expected, defaultEntry.getData(), "sfData");
    }
    {
        auto const expected = SF_UINT16::type::value_type{};
        expectEqualField(expected, defaultEntry.getManagementFeeRate(), "sfManagementFeeRate");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getOwnerCount(), "sfOwnerCount");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getDebtTotal(), "sfDebtTotal");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getDebtMaximum(), "sfDebtMaximum");
    }
    {
        auto const expected = SF_NUMBER::type::value_type{};
        expectEqualField(expected, defaultEntry.getCoverAvailable(), "sfCoverAvailable");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getCoverRateMinimum(), "sfCoverRateMinimum");
    }
    {
        auto const expected = SF_UINT32::type::value_type{};
        expectEqualField(expected, defaultEntry.getCoverRateLiquidation(), "sfCoverRateLiquidation");
    }

    // Set: default fields return the assigned value.
    LoanBrokerBuilder setBuilder{
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
    setBuilder.setData(canonical_VL());
    setBuilder.setManagementFeeRate(canonical_UINT16());
    setBuilder.setOwnerCount(canonical_UINT32());
    setBuilder.setDebtTotal(canonical_NUMBER());
    setBuilder.setDebtMaximum(canonical_NUMBER());
    setBuilder.setCoverAvailable(canonical_NUMBER());
    setBuilder.setCoverRateMinimum(canonical_UINT32());
    setBuilder.setCoverRateLiquidation(canonical_UINT32());
    auto const setEntry = setBuilder.build(index);
    {
        auto const expected = canonical_VL();
        expectEqualField(expected, setEntry.getData(), "sfData");
    }
    {
        auto const expected = canonical_UINT16();
        expectEqualField(expected, setEntry.getManagementFeeRate(), "sfManagementFeeRate");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getOwnerCount(), "sfOwnerCount");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getDebtTotal(), "sfDebtTotal");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getDebtMaximum(), "sfDebtMaximum");
    }
    {
        auto const expected = canonical_NUMBER();
        expectEqualField(expected, setEntry.getCoverAvailable(), "sfCoverAvailable");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getCoverRateMinimum(), "sfCoverRateMinimum");
    }
    {
        auto const expected = canonical_UINT32();
        expectEqualField(expected, setEntry.getCoverRateLiquidation(), "sfCoverRateLiquidation");
    }
}
}
