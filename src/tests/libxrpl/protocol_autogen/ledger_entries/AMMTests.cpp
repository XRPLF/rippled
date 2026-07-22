// Auto-generated unit tests for ledger entry AMM


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMM.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const tradingFeeValue = canonical_UINT16();
    auto const voteSlotsValue = canonical_ARRAY();
    auto const auctionSlotValue = canonical_OBJECT();
    auto const lPTokenBalanceValue = canonical_AMOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    AMMBuilder builder{
        accountValue,
        lPTokenBalanceValue,
        assetValue,
        asset2Value,
        ownerNodeValue
    };

    builder.setTradingFee(tradingFeeValue);
    builder.setVoteSlots(voteSlotsValue);
    builder.setAuctionSlot(auctionSlotValue);
    builder.setPreviousTxnID(previousTxnIDValue);
    builder.setPreviousTxnLgrSeq(previousTxnLgrSeqValue);

    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = lPTokenBalanceValue;
        auto const actual = entry.getLPTokenBalance();
        expectEqualField(expected, actual, "sfLPTokenBalance");
    }

    {
        auto const& expected = assetValue;
        auto const actual = entry.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = asset2Value;
        auto const actual = entry.getAsset2();
        expectEqualField(expected, actual, "sfAsset2");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = tradingFeeValue;
        auto const actualOpt = entry.getTradingFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTradingFee");
        EXPECT_TRUE(entry.hasTradingFee());
    }

    {
        auto const& expected = voteSlotsValue;
        auto const actualOpt = entry.getVoteSlots();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVoteSlots");
        EXPECT_TRUE(entry.hasVoteSlots());
    }

    {
        auto const& expected = auctionSlotValue;
        auto const actualOpt = entry.getAuctionSlot();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAuctionSlot");
        EXPECT_TRUE(entry.hasAuctionSlot());
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
TEST(AMMTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const tradingFeeValue = canonical_UINT16();
    auto const voteSlotsValue = canonical_ARRAY();
    auto const auctionSlotValue = canonical_OBJECT();
    auto const lPTokenBalanceValue = canonical_AMOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const ownerNodeValue = canonical_UINT64();
    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(AMM::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfTradingFee) = tradingFeeValue;
    sle->setFieldArray(sfVoteSlots, voteSlotsValue);
    sle->setFieldObject(sfAuctionSlot, auctionSlotValue);
    sle->at(sfLPTokenBalance) = lPTokenBalanceValue;
    sle->at(sfAsset) = STIssue(sfAsset, assetValue);
    sle->at(sfAsset2) = STIssue(sfAsset2, asset2Value);
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;

    AMMBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMM entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = lPTokenBalanceValue;

        auto const fromSle = entryFromSle.getLPTokenBalance();
        auto const fromBuilder = entryFromBuilder.getLPTokenBalance();

        expectEqualField(expected, fromSle, "sfLPTokenBalance");
        expectEqualField(expected, fromBuilder, "sfLPTokenBalance");
    }

    {
        auto const& expected = assetValue;

        auto const fromSle = entryFromSle.getAsset();
        auto const fromBuilder = entryFromBuilder.getAsset();

        expectEqualField(expected, fromSle, "sfAsset");
        expectEqualField(expected, fromBuilder, "sfAsset");
    }

    {
        auto const& expected = asset2Value;

        auto const fromSle = entryFromSle.getAsset2();
        auto const fromBuilder = entryFromBuilder.getAsset2();

        expectEqualField(expected, fromSle, "sfAsset2");
        expectEqualField(expected, fromBuilder, "sfAsset2");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = tradingFeeValue;

        auto const fromSleOpt = entryFromSle.getTradingFee();
        auto const fromBuilderOpt = entryFromBuilder.getTradingFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTradingFee");
        expectEqualField(expected, *fromBuilderOpt, "sfTradingFee");
    }

    {
        auto const& expected = voteSlotsValue;

        auto const fromSleOpt = entryFromSle.getVoteSlots();
        auto const fromBuilderOpt = entryFromBuilder.getVoteSlots();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVoteSlots");
        expectEqualField(expected, *fromBuilderOpt, "sfVoteSlots");
    }

    {
        auto const& expected = auctionSlotValue;

        auto const fromSleOpt = entryFromSle.getAuctionSlot();
        auto const fromBuilderOpt = entryFromBuilder.getAuctionSlot();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAuctionSlot");
        expectEqualField(expected, *fromBuilderOpt, "sfAuctionSlot");
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
TEST(AMMTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMM{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(AMMTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const lPTokenBalanceValue = canonical_AMOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const asset2Value = canonical_ISSUE();
    auto const ownerNodeValue = canonical_UINT64();

    AMMBuilder builder{
        accountValue,
        lPTokenBalanceValue,
        assetValue,
        asset2Value,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasTradingFee());
    EXPECT_FALSE(entry.getTradingFee().has_value());
    EXPECT_FALSE(entry.hasVoteSlots());
    EXPECT_FALSE(entry.getVoteSlots().has_value());
    EXPECT_FALSE(entry.hasAuctionSlot());
    EXPECT_FALSE(entry.getAuctionSlot().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnID());
    EXPECT_FALSE(entry.getPreviousTxnID().has_value());
    EXPECT_FALSE(entry.hasPreviousTxnLgrSeq());
    EXPECT_FALSE(entry.getPreviousTxnLgrSeq().has_value());
}
}
