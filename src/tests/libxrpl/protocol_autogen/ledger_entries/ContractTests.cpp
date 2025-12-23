// Auto-generated unit tests for ledger entry Contract


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Contract.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(ContractTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const contractHashValue = canonical_UINT256();
    auto const instanceParameterValuesValue = canonical_ARRAY();
    auto const uRIValue = canonical_VL();

    ContractBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        ownerValue,
        contractAccountValue,
        contractHashValue
    };

    builder.setInstanceParameterValues(instanceParameterValuesValue);
    builder.setURI(uRIValue);

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
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = contractAccountValue;
        auto const actual = entry.getContractAccount();
        expectEqualField(expected, actual, "sfContractAccount");
    }

    {
        auto const& expected = contractHashValue;
        auto const actual = entry.getContractHash();
        expectEqualField(expected, actual, "sfContractHash");
    }

    {
        auto const& expected = instanceParameterValuesValue;
        auto const actualOpt = entry.getInstanceParameterValues();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfInstanceParameterValues");
        EXPECT_TRUE(entry.hasInstanceParameterValues());
    }

    {
        auto const& expected = uRIValue;
        auto const actualOpt = entry.getURI();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfURI");
        EXPECT_TRUE(entry.hasURI());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(ContractTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const contractHashValue = canonical_UINT256();
    auto const instanceParameterValuesValue = canonical_ARRAY();
    auto const uRIValue = canonical_VL();

    auto sle = std::make_shared<SLE>(Contract::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfContractAccount) = contractAccountValue;
    sle->at(sfContractHash) = contractHashValue;
    sle->setFieldArray(sfInstanceParameterValues, instanceParameterValuesValue);
    sle->at(sfURI) = uRIValue;

    ContractBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Contract entryFromSle{sle};
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
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = contractAccountValue;

        auto const fromSle = entryFromSle.getContractAccount();
        auto const fromBuilder = entryFromBuilder.getContractAccount();

        expectEqualField(expected, fromSle, "sfContractAccount");
        expectEqualField(expected, fromBuilder, "sfContractAccount");
    }

    {
        auto const& expected = contractHashValue;

        auto const fromSle = entryFromSle.getContractHash();
        auto const fromBuilder = entryFromBuilder.getContractHash();

        expectEqualField(expected, fromSle, "sfContractHash");
        expectEqualField(expected, fromBuilder, "sfContractHash");
    }

    {
        auto const& expected = instanceParameterValuesValue;

        auto const fromSleOpt = entryFromSle.getInstanceParameterValues();
        auto const fromBuilderOpt = entryFromBuilder.getInstanceParameterValues();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfInstanceParameterValues");
        expectEqualField(expected, *fromBuilderOpt, "sfInstanceParameterValues");
    }

    {
        auto const& expected = uRIValue;

        auto const fromSleOpt = entryFromSle.getURI();
        auto const fromBuilderOpt = entryFromBuilder.getURI();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfURI");
        expectEqualField(expected, *fromBuilderOpt, "sfURI");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(ContractTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Contract{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(ContractTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(ContractBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(ContractTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const sequenceValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const contractAccountValue = canonical_ACCOUNT();
    auto const contractHashValue = canonical_UINT256();

    ContractBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        sequenceValue,
        ownerNodeValue,
        ownerValue,
        contractAccountValue,
        contractHashValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasInstanceParameterValues());
    EXPECT_FALSE(entry.getInstanceParameterValues().has_value());
    EXPECT_FALSE(entry.hasURI());
    EXPECT_FALSE(entry.getURI().has_value());
}
}
