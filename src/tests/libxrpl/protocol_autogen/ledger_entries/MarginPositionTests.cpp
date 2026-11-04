// Auto-generated unit tests for ledger entry MarginPosition


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/MarginPosition.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(MarginPositionTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const sizeValue = canonical_NUMBER();
    auto const entryNotionalValue = canonical_NUMBER();
    auto const fundingIndexValue = canonical_NUMBER();
    auto const lossIndexValue = canonical_NUMBER();
    auto const openBuyValue = canonical_NUMBER();
    auto const openSellValue = canonical_NUMBER();
    auto const orderCountValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    MarginPositionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        marginAccountIDValue,
        marketIDValue,
        ownerNodeValue
    };

    builder.setSize(sizeValue);
    builder.setEntryNotional(entryNotionalValue);
    builder.setFundingIndex(fundingIndexValue);
    builder.setLossIndex(lossIndexValue);
    builder.setOpenBuy(openBuyValue);
    builder.setOpenSell(openSellValue);
    builder.setOrderCount(orderCountValue);

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
        auto const& expected = marginAccountIDValue;
        auto const actual = entry.getMarginAccountID();
        expectEqualField(expected, actual, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;
        auto const actual = entry.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = sizeValue;
        auto const actualOpt = entry.getSize();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSize");
        EXPECT_TRUE(entry.hasSize());
    }

    {
        auto const& expected = entryNotionalValue;
        auto const actualOpt = entry.getEntryNotional();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfEntryNotional");
        EXPECT_TRUE(entry.hasEntryNotional());
    }

    {
        auto const& expected = fundingIndexValue;
        auto const actualOpt = entry.getFundingIndex();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFundingIndex");
        EXPECT_TRUE(entry.hasFundingIndex());
    }

    {
        auto const& expected = lossIndexValue;
        auto const actualOpt = entry.getLossIndex();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLossIndex");
        EXPECT_TRUE(entry.hasLossIndex());
    }

    {
        auto const& expected = openBuyValue;
        auto const actualOpt = entry.getOpenBuy();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOpenBuy");
        EXPECT_TRUE(entry.hasOpenBuy());
    }

    {
        auto const& expected = openSellValue;
        auto const actualOpt = entry.getOpenSell();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOpenSell");
        EXPECT_TRUE(entry.hasOpenSell());
    }

    {
        auto const& expected = orderCountValue;
        auto const actualOpt = entry.getOrderCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOrderCount");
        EXPECT_TRUE(entry.hasOrderCount());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(MarginPositionTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const sizeValue = canonical_NUMBER();
    auto const entryNotionalValue = canonical_NUMBER();
    auto const fundingIndexValue = canonical_NUMBER();
    auto const lossIndexValue = canonical_NUMBER();
    auto const openBuyValue = canonical_NUMBER();
    auto const openSellValue = canonical_NUMBER();
    auto const orderCountValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(MarginPosition::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfMarginAccountID) = marginAccountIDValue;
    sle->at(sfMarketID) = marketIDValue;
    sle->at(sfSize) = sizeValue;
    sle->at(sfEntryNotional) = entryNotionalValue;
    sle->at(sfFundingIndex) = fundingIndexValue;
    sle->at(sfLossIndex) = lossIndexValue;
    sle->at(sfOpenBuy) = openBuyValue;
    sle->at(sfOpenSell) = openSellValue;
    sle->at(sfOrderCount) = orderCountValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    MarginPositionBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    MarginPosition entryFromSle{sle};
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
        auto const& expected = marginAccountIDValue;

        auto const fromSle = entryFromSle.getMarginAccountID();
        auto const fromBuilder = entryFromBuilder.getMarginAccountID();

        expectEqualField(expected, fromSle, "sfMarginAccountID");
        expectEqualField(expected, fromBuilder, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;

        auto const fromSle = entryFromSle.getMarketID();
        auto const fromBuilder = entryFromBuilder.getMarketID();

        expectEqualField(expected, fromSle, "sfMarketID");
        expectEqualField(expected, fromBuilder, "sfMarketID");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = sizeValue;

        auto const fromSleOpt = entryFromSle.getSize();
        auto const fromBuilderOpt = entryFromBuilder.getSize();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSize");
        expectEqualField(expected, *fromBuilderOpt, "sfSize");
    }

    {
        auto const& expected = entryNotionalValue;

        auto const fromSleOpt = entryFromSle.getEntryNotional();
        auto const fromBuilderOpt = entryFromBuilder.getEntryNotional();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfEntryNotional");
        expectEqualField(expected, *fromBuilderOpt, "sfEntryNotional");
    }

    {
        auto const& expected = fundingIndexValue;

        auto const fromSleOpt = entryFromSle.getFundingIndex();
        auto const fromBuilderOpt = entryFromBuilder.getFundingIndex();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFundingIndex");
        expectEqualField(expected, *fromBuilderOpt, "sfFundingIndex");
    }

    {
        auto const& expected = lossIndexValue;

        auto const fromSleOpt = entryFromSle.getLossIndex();
        auto const fromBuilderOpt = entryFromBuilder.getLossIndex();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLossIndex");
        expectEqualField(expected, *fromBuilderOpt, "sfLossIndex");
    }

    {
        auto const& expected = openBuyValue;

        auto const fromSleOpt = entryFromSle.getOpenBuy();
        auto const fromBuilderOpt = entryFromBuilder.getOpenBuy();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOpenBuy");
        expectEqualField(expected, *fromBuilderOpt, "sfOpenBuy");
    }

    {
        auto const& expected = openSellValue;

        auto const fromSleOpt = entryFromSle.getOpenSell();
        auto const fromBuilderOpt = entryFromBuilder.getOpenSell();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOpenSell");
        expectEqualField(expected, *fromBuilderOpt, "sfOpenSell");
    }

    {
        auto const& expected = orderCountValue;

        auto const fromSleOpt = entryFromSle.getOrderCount();
        auto const fromBuilderOpt = entryFromBuilder.getOrderCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOrderCount");
        expectEqualField(expected, *fromBuilderOpt, "sfOrderCount");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(MarginPositionTests, WrapperThrowsOnWrongEntryType)
{
    UInt256 const index{3u};

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

    EXPECT_THROW(MarginPosition{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(MarginPositionTests, BuilderThrowsOnWrongEntryType)
{
    UInt256 const index{4u};

    // Build a valid ledger entry of a different type
    TicketBuilder wrongBuilder{
        canonical_ACCOUNT(),
        canonical_UINT64(),
        canonical_UINT32(),
        canonical_UINT256(),
        canonical_UINT32()};
    auto wrongEntry = wrongBuilder.build(index);

    EXPECT_THROW(MarginPositionBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(MarginPositionTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const ownerNodeValue = canonical_UINT64();

    MarginPositionBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        marginAccountIDValue,
        marketIDValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasSize());
    EXPECT_FALSE(entry.getSize().has_value());
    EXPECT_FALSE(entry.hasEntryNotional());
    EXPECT_FALSE(entry.getEntryNotional().has_value());
    EXPECT_FALSE(entry.hasFundingIndex());
    EXPECT_FALSE(entry.getFundingIndex().has_value());
    EXPECT_FALSE(entry.hasLossIndex());
    EXPECT_FALSE(entry.getLossIndex().has_value());
    EXPECT_FALSE(entry.hasOpenBuy());
    EXPECT_FALSE(entry.getOpenBuy().has_value());
    EXPECT_FALSE(entry.hasOpenSell());
    EXPECT_FALSE(entry.getOpenSell().has_value());
    EXPECT_FALSE(entry.hasOrderCount());
    EXPECT_FALSE(entry.getOrderCount().has_value());
}
}
