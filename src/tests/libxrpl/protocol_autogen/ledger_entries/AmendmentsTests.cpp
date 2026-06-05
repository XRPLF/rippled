// Auto-generated unit tests for ledger entry Amendments


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/Amendments.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AmendmentsTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const amendmentsValue = canonical_VECTOR256();
    auto const majoritiesValue = canonical_ARRAY();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    AmendmentsBuilder builder{
    };

    builder.setAmendments(amendmentsValue);
    builder.setMajorities(majoritiesValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = amendmentsValue;
        auto const actualOpt = entry.getAmendments();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmendments");
        EXPECT_TRUE(entry.hasAmendments());
    }

    {
        auto const& expected = majoritiesValue;
        auto const actualOpt = entry.getMajorities();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMajorities");
        EXPECT_TRUE(entry.hasMajorities());
    }

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

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AmendmentsTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const amendmentsValue = canonical_VECTOR256();
    auto const majoritiesValue = canonical_ARRAY();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(Amendments::entryType, index);

    sle->at(sfAmendments) = amendmentsValue;
    sle->setFieldArray(sfMajorities, majoritiesValue);
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    AmendmentsBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    Amendments entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = amendmentsValue;

        auto const fromSleOpt = entryFromSle.getAmendments();
        auto const fromBuilderOpt = entryFromBuilder.getAmendments();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAmendments");
        expectEqualField(expected, *fromBuilderOpt, "sfAmendments");
    }

    {
        auto const& expected = majoritiesValue;

        auto const fromSleOpt = entryFromSle.getMajorities();
        auto const fromBuilderOpt = entryFromBuilder.getMajorities();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMajorities");
        expectEqualField(expected, *fromBuilderOpt, "sfMajorities");
    }

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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AmendmentsTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(Amendments{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AmendmentsTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AmendmentsBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(AmendmentsTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};


    AmendmentsBuilder builder{
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAmendments());
    EXPECT_FALSE(entry.getAmendments().has_value());
    EXPECT_FALSE(entry.hasMajorities());
    EXPECT_FALSE(entry.getMajorities().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
