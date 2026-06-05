// Auto-generated unit tests for ledger entry RippleState


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/RippleState.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(RippleStateTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const balanceValue = canonical_AMOUNT();
    auto const lowLimitValue = canonical_AMOUNT();
    auto const highLimitValue = canonical_AMOUNT();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const lowNodeValue = canonical_UINT64();
    auto const lowQualityInValue = canonical_UINT32();
    auto const lowQualityOutValue = canonical_UINT32();
    auto const highNodeValue = canonical_UINT64();
    auto const highQualityInValue = canonical_UINT32();
    auto const highQualityOutValue = canonical_UINT32();

    RippleStateBuilder builder{
        balanceValue,
        lowLimitValue,
        highLimitValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    builder.setLowNode(lowNodeValue);
    builder.setLowQualityIn(lowQualityInValue);
    builder.setLowQualityOut(lowQualityOutValue);
    builder.setHighNode(highNodeValue);
    builder.setHighQualityIn(highQualityInValue);
    builder.setHighQualityOut(highQualityOutValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = balanceValue;
        auto const actual = entry.getBalance();
        expectEqualField(expected, actual, "sfBalance");
    }

    {
        auto const& expected = lowLimitValue;
        auto const actual = entry.getLowLimit();
        expectEqualField(expected, actual, "sfLowLimit");
    }

    {
        auto const& expected = highLimitValue;
        auto const actual = entry.getHighLimit();
        expectEqualField(expected, actual, "sfHighLimit");
    }

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
        auto const& expected = lowNodeValue;
        auto const actualOpt = entry.getLowNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLowNode");
        EXPECT_TRUE(entry.hasLowNode());
    }

    {
        auto const& expected = lowQualityInValue;
        auto const actualOpt = entry.getLowQualityIn();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLowQualityIn");
        EXPECT_TRUE(entry.hasLowQualityIn());
    }

    {
        auto const& expected = lowQualityOutValue;
        auto const actualOpt = entry.getLowQualityOut();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLowQualityOut");
        EXPECT_TRUE(entry.hasLowQualityOut());
    }

    {
        auto const& expected = highNodeValue;
        auto const actualOpt = entry.getHighNode();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHighNode");
        EXPECT_TRUE(entry.hasHighNode());
    }

    {
        auto const& expected = highQualityInValue;
        auto const actualOpt = entry.getHighQualityIn();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHighQualityIn");
        EXPECT_TRUE(entry.hasHighQualityIn());
    }

    {
        auto const& expected = highQualityOutValue;
        auto const actualOpt = entry.getHighQualityOut();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHighQualityOut");
        EXPECT_TRUE(entry.hasHighQualityOut());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(RippleStateTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const balanceValue = canonical_AMOUNT();
    auto const lowLimitValue = canonical_AMOUNT();
    auto const highLimitValue = canonical_AMOUNT();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const lowNodeValue = canonical_UINT64();
    auto const lowQualityInValue = canonical_UINT32();
    auto const lowQualityOutValue = canonical_UINT32();
    auto const highNodeValue = canonical_UINT64();
    auto const highQualityInValue = canonical_UINT32();
    auto const highQualityOutValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(RippleState::entryType, index);

    sle->at(sfBalance) = balanceValue;
    sle->at(sfLowLimit) = lowLimitValue;
    sle->at(sfHighLimit) = highLimitValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfLowNode) = lowNodeValue;
    sle->at(sfLowQualityIn) = lowQualityInValue;
    sle->at(sfLowQualityOut) = lowQualityOutValue;
    sle->at(sfHighNode) = highNodeValue;
    sle->at(sfHighQualityIn) = highQualityInValue;
    sle->at(sfHighQualityOut) = highQualityOutValue;

    RippleStateBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    RippleState entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = balanceValue;

        auto const fromSle = entryFromSle.getBalance();
        auto const fromBuilder = entryFromBuilder.getBalance();

        expectEqualField(expected, fromSle, "sfBalance");
        expectEqualField(expected, fromBuilder, "sfBalance");
    }

    {
        auto const& expected = lowLimitValue;

        auto const fromSle = entryFromSle.getLowLimit();
        auto const fromBuilder = entryFromBuilder.getLowLimit();

        expectEqualField(expected, fromSle, "sfLowLimit");
        expectEqualField(expected, fromBuilder, "sfLowLimit");
    }

    {
        auto const& expected = highLimitValue;

        auto const fromSle = entryFromSle.getHighLimit();
        auto const fromBuilder = entryFromBuilder.getHighLimit();

        expectEqualField(expected, fromSle, "sfHighLimit");
        expectEqualField(expected, fromBuilder, "sfHighLimit");
    }

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
        auto const& expected = lowNodeValue;

        auto const fromSleOpt = entryFromSle.getLowNode();
        auto const fromBuilderOpt = entryFromBuilder.getLowNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLowNode");
        expectEqualField(expected, *fromBuilderOpt, "sfLowNode");
    }

    {
        auto const& expected = lowQualityInValue;

        auto const fromSleOpt = entryFromSle.getLowQualityIn();
        auto const fromBuilderOpt = entryFromBuilder.getLowQualityIn();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLowQualityIn");
        expectEqualField(expected, *fromBuilderOpt, "sfLowQualityIn");
    }

    {
        auto const& expected = lowQualityOutValue;

        auto const fromSleOpt = entryFromSle.getLowQualityOut();
        auto const fromBuilderOpt = entryFromBuilder.getLowQualityOut();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLowQualityOut");
        expectEqualField(expected, *fromBuilderOpt, "sfLowQualityOut");
    }

    {
        auto const& expected = highNodeValue;

        auto const fromSleOpt = entryFromSle.getHighNode();
        auto const fromBuilderOpt = entryFromBuilder.getHighNode();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHighNode");
        expectEqualField(expected, *fromBuilderOpt, "sfHighNode");
    }

    {
        auto const& expected = highQualityInValue;

        auto const fromSleOpt = entryFromSle.getHighQualityIn();
        auto const fromBuilderOpt = entryFromBuilder.getHighQualityIn();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHighQualityIn");
        expectEqualField(expected, *fromBuilderOpt, "sfHighQualityIn");
    }

    {
        auto const& expected = highQualityOutValue;

        auto const fromSleOpt = entryFromSle.getHighQualityOut();
        auto const fromBuilderOpt = entryFromBuilder.getHighQualityOut();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHighQualityOut");
        expectEqualField(expected, *fromBuilderOpt, "sfHighQualityOut");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(RippleStateTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(RippleState{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(RippleStateTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(RippleStateBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(RippleStateTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const balanceValue = canonical_AMOUNT();
    auto const lowLimitValue = canonical_AMOUNT();
    auto const highLimitValue = canonical_AMOUNT();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    RippleStateBuilder builder{
        balanceValue,
        lowLimitValue,
        highLimitValue,
        previousTxnIDValue,
        previousTxnLgrSeqValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasLowNode());
    EXPECT_FALSE(entry.getLowNode().has_value());
    EXPECT_FALSE(entry.hasLowQualityIn());
    EXPECT_FALSE(entry.getLowQualityIn().has_value());
    EXPECT_FALSE(entry.hasLowQualityOut());
    EXPECT_FALSE(entry.getLowQualityOut().has_value());
    EXPECT_FALSE(entry.hasHighNode());
    EXPECT_FALSE(entry.getHighNode().has_value());
    EXPECT_FALSE(entry.hasHighQualityIn());
    EXPECT_FALSE(entry.getHighQualityIn().has_value());
    EXPECT_FALSE(entry.hasHighQualityOut());
    EXPECT_FALSE(entry.getHighQualityOut().has_value());
}
}
