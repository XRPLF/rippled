// Auto-generated unit tests for ledger entry ContractSource


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/ContractSource.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(ContractSourceTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const contractHashValue = canonical_UINT256();
    auto const contractCodeValue = canonical_VL();
    auto const functionsValue = canonical_ARRAY();
    auto const instanceParametersValue = canonical_ARRAY();
    auto const referenceCountValue = canonical_UINT64();

    ContractSourceBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        contractHashValue,
        contractCodeValue,
        functionsValue,
        referenceCountValue
    };

    builder.setInstanceParameters(instanceParametersValue);

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
        auto const& expected = contractHashValue;
        auto const actual = entry.getContractHash();
        expectEqualField(expected, actual, "sfContractHash");
    }

    {
        auto const& expected = contractCodeValue;
        auto const actual = entry.getContractCode();
        expectEqualField(expected, actual, "sfContractCode");
    }

    {
        auto const& expected = functionsValue;
        auto const actual = entry.getFunctions();
        expectEqualField(expected, actual, "sfFunctions");
    }

    {
        auto const& expected = referenceCountValue;
        auto const actual = entry.getReferenceCount();
        expectEqualField(expected, actual, "sfReferenceCount");
    }

    {
        auto const& expected = instanceParametersValue;
        auto const actualOpt = entry.getInstanceParameters();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfInstanceParameters");
        EXPECT_TRUE(entry.hasInstanceParameters());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(ContractSourceTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const contractHashValue = canonical_UINT256();
    auto const contractCodeValue = canonical_VL();
    auto const functionsValue = canonical_ARRAY();
    auto const instanceParametersValue = canonical_ARRAY();
    auto const referenceCountValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(ContractSource::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfContractHash) = contractHashValue;
    sle->at(sfContractCode) = contractCodeValue;
    sle->setFieldArray(sfFunctions, functionsValue);
    sle->setFieldArray(sfInstanceParameters, instanceParametersValue);
    sle->at(sfReferenceCount) = referenceCountValue;

    ContractSourceBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    ContractSource entryFromSle{sle};
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
        auto const& expected = contractHashValue;

        auto const fromSle = entryFromSle.getContractHash();
        auto const fromBuilder = entryFromBuilder.getContractHash();

        expectEqualField(expected, fromSle, "sfContractHash");
        expectEqualField(expected, fromBuilder, "sfContractHash");
    }

    {
        auto const& expected = contractCodeValue;

        auto const fromSle = entryFromSle.getContractCode();
        auto const fromBuilder = entryFromBuilder.getContractCode();

        expectEqualField(expected, fromSle, "sfContractCode");
        expectEqualField(expected, fromBuilder, "sfContractCode");
    }

    {
        auto const& expected = functionsValue;

        auto const fromSle = entryFromSle.getFunctions();
        auto const fromBuilder = entryFromBuilder.getFunctions();

        expectEqualField(expected, fromSle, "sfFunctions");
        expectEqualField(expected, fromBuilder, "sfFunctions");
    }

    {
        auto const& expected = referenceCountValue;

        auto const fromSle = entryFromSle.getReferenceCount();
        auto const fromBuilder = entryFromBuilder.getReferenceCount();

        expectEqualField(expected, fromSle, "sfReferenceCount");
        expectEqualField(expected, fromBuilder, "sfReferenceCount");
    }

    {
        auto const& expected = instanceParametersValue;

        auto const fromSleOpt = entryFromSle.getInstanceParameters();
        auto const fromBuilderOpt = entryFromBuilder.getInstanceParameters();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfInstanceParameters");
        expectEqualField(expected, *fromBuilderOpt, "sfInstanceParameters");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(ContractSourceTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(ContractSource{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(ContractSourceTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(ContractSourceBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(ContractSourceTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const contractHashValue = canonical_UINT256();
    auto const contractCodeValue = canonical_VL();
    auto const functionsValue = canonical_ARRAY();
    auto const referenceCountValue = canonical_UINT64();

    ContractSourceBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        contractHashValue,
        contractCodeValue,
        functionsValue,
        referenceCountValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasInstanceParameters());
    EXPECT_FALSE(entry.getInstanceParameters().has_value());
}
}
