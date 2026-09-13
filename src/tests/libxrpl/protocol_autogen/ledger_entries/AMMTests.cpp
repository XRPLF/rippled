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
    auto const curveTypeValue = canonical_UINT8();
    auto const feeTierValue = canonical_UINT8();
    auto const tickSpacingValue = canonical_UINT16();
    auto const currentTickValue = canonical_INT32();
    auto const activeLiquidityValue = canonical_UINT64();
    auto const sqrtPriceX96Value = canonical_UINT256();
    auto const feeGrowthGlobal0Value = canonical_NUMBER();
    auto const feeGrowthGlobal1Value = canonical_NUMBER();
    auto const amplificationValue = canonical_UINT32();
    auto const amplificationTimeValue = canonical_UINT32();
    auto const positionCountValue = canonical_UINT32();
    auto const binStepValue = canonical_UINT16();
    auto const activeBinIDValue = canonical_INT32();

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
    builder.setCurveType(curveTypeValue);
    builder.setFeeTier(feeTierValue);
    builder.setTickSpacing(tickSpacingValue);
    builder.setCurrentTick(currentTickValue);
    builder.setActiveLiquidity(activeLiquidityValue);
    builder.setSqrtPriceX96(sqrtPriceX96Value);
    builder.setFeeGrowthGlobal0(feeGrowthGlobal0Value);
    builder.setFeeGrowthGlobal1(feeGrowthGlobal1Value);
    builder.setAmplification(amplificationValue);
    builder.setAmplificationTime(amplificationTimeValue);
    builder.setPositionCount(positionCountValue);
    builder.setBinStep(binStepValue);
    builder.setActiveBinID(activeBinIDValue);

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

    {
        auto const& expected = curveTypeValue;
        auto const actualOpt = entry.getCurveType();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCurveType");
        EXPECT_TRUE(entry.hasCurveType());
    }

    {
        auto const& expected = feeTierValue;
        auto const actualOpt = entry.getFeeTier();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFeeTier");
        EXPECT_TRUE(entry.hasFeeTier());
    }

    {
        auto const& expected = tickSpacingValue;
        auto const actualOpt = entry.getTickSpacing();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTickSpacing");
        EXPECT_TRUE(entry.hasTickSpacing());
    }

    {
        auto const& expected = currentTickValue;
        auto const actualOpt = entry.getCurrentTick();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCurrentTick");
        EXPECT_TRUE(entry.hasCurrentTick());
    }

    {
        auto const& expected = activeLiquidityValue;
        auto const actualOpt = entry.getActiveLiquidity();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfActiveLiquidity");
        EXPECT_TRUE(entry.hasActiveLiquidity());
    }

    {
        auto const& expected = sqrtPriceX96Value;
        auto const actualOpt = entry.getSqrtPriceX96();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSqrtPriceX96");
        EXPECT_TRUE(entry.hasSqrtPriceX96());
    }

    {
        auto const& expected = feeGrowthGlobal0Value;
        auto const actualOpt = entry.getFeeGrowthGlobal0();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFeeGrowthGlobal0");
        EXPECT_TRUE(entry.hasFeeGrowthGlobal0());
    }

    {
        auto const& expected = feeGrowthGlobal1Value;
        auto const actualOpt = entry.getFeeGrowthGlobal1();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFeeGrowthGlobal1");
        EXPECT_TRUE(entry.hasFeeGrowthGlobal1());
    }

    {
        auto const& expected = amplificationValue;
        auto const actualOpt = entry.getAmplification();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmplification");
        EXPECT_TRUE(entry.hasAmplification());
    }

    {
        auto const& expected = amplificationTimeValue;
        auto const actualOpt = entry.getAmplificationTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAmplificationTime");
        EXPECT_TRUE(entry.hasAmplificationTime());
    }

    {
        auto const& expected = positionCountValue;
        auto const actualOpt = entry.getPositionCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPositionCount");
        EXPECT_TRUE(entry.hasPositionCount());
    }

    {
        auto const& expected = binStepValue;
        auto const actualOpt = entry.getBinStep();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBinStep");
        EXPECT_TRUE(entry.hasBinStep());
    }

    {
        auto const& expected = activeBinIDValue;
        auto const actualOpt = entry.getActiveBinID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfActiveBinID");
        EXPECT_TRUE(entry.hasActiveBinID());
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
    auto const curveTypeValue = canonical_UINT8();
    auto const feeTierValue = canonical_UINT8();
    auto const tickSpacingValue = canonical_UINT16();
    auto const currentTickValue = canonical_INT32();
    auto const activeLiquidityValue = canonical_UINT64();
    auto const sqrtPriceX96Value = canonical_UINT256();
    auto const feeGrowthGlobal0Value = canonical_NUMBER();
    auto const feeGrowthGlobal1Value = canonical_NUMBER();
    auto const amplificationValue = canonical_UINT32();
    auto const amplificationTimeValue = canonical_UINT32();
    auto const positionCountValue = canonical_UINT32();
    auto const binStepValue = canonical_UINT16();
    auto const activeBinIDValue = canonical_INT32();

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
    sle->at(sfCurveType) = curveTypeValue;
    sle->at(sfFeeTier) = feeTierValue;
    sle->at(sfTickSpacing) = tickSpacingValue;
    sle->at(sfCurrentTick) = currentTickValue;
    sle->at(sfActiveLiquidity) = activeLiquidityValue;
    sle->at(sfSqrtPriceX96) = sqrtPriceX96Value;
    sle->at(sfFeeGrowthGlobal0) = feeGrowthGlobal0Value;
    sle->at(sfFeeGrowthGlobal1) = feeGrowthGlobal1Value;
    sle->at(sfAmplification) = amplificationValue;
    sle->at(sfAmplificationTime) = amplificationTimeValue;
    sle->at(sfPositionCount) = positionCountValue;
    sle->at(sfBinStep) = binStepValue;
    sle->at(sfActiveBinID) = activeBinIDValue;

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

    {
        auto const& expected = curveTypeValue;

        auto const fromSleOpt = entryFromSle.getCurveType();
        auto const fromBuilderOpt = entryFromBuilder.getCurveType();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCurveType");
        expectEqualField(expected, *fromBuilderOpt, "sfCurveType");
    }

    {
        auto const& expected = feeTierValue;

        auto const fromSleOpt = entryFromSle.getFeeTier();
        auto const fromBuilderOpt = entryFromBuilder.getFeeTier();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFeeTier");
        expectEqualField(expected, *fromBuilderOpt, "sfFeeTier");
    }

    {
        auto const& expected = tickSpacingValue;

        auto const fromSleOpt = entryFromSle.getTickSpacing();
        auto const fromBuilderOpt = entryFromBuilder.getTickSpacing();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTickSpacing");
        expectEqualField(expected, *fromBuilderOpt, "sfTickSpacing");
    }

    {
        auto const& expected = currentTickValue;

        auto const fromSleOpt = entryFromSle.getCurrentTick();
        auto const fromBuilderOpt = entryFromBuilder.getCurrentTick();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCurrentTick");
        expectEqualField(expected, *fromBuilderOpt, "sfCurrentTick");
    }

    {
        auto const& expected = activeLiquidityValue;

        auto const fromSleOpt = entryFromSle.getActiveLiquidity();
        auto const fromBuilderOpt = entryFromBuilder.getActiveLiquidity();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfActiveLiquidity");
        expectEqualField(expected, *fromBuilderOpt, "sfActiveLiquidity");
    }

    {
        auto const& expected = sqrtPriceX96Value;

        auto const fromSleOpt = entryFromSle.getSqrtPriceX96();
        auto const fromBuilderOpt = entryFromBuilder.getSqrtPriceX96();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSqrtPriceX96");
        expectEqualField(expected, *fromBuilderOpt, "sfSqrtPriceX96");
    }

    {
        auto const& expected = feeGrowthGlobal0Value;

        auto const fromSleOpt = entryFromSle.getFeeGrowthGlobal0();
        auto const fromBuilderOpt = entryFromBuilder.getFeeGrowthGlobal0();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFeeGrowthGlobal0");
        expectEqualField(expected, *fromBuilderOpt, "sfFeeGrowthGlobal0");
    }

    {
        auto const& expected = feeGrowthGlobal1Value;

        auto const fromSleOpt = entryFromSle.getFeeGrowthGlobal1();
        auto const fromBuilderOpt = entryFromBuilder.getFeeGrowthGlobal1();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFeeGrowthGlobal1");
        expectEqualField(expected, *fromBuilderOpt, "sfFeeGrowthGlobal1");
    }

    {
        auto const& expected = amplificationValue;

        auto const fromSleOpt = entryFromSle.getAmplification();
        auto const fromBuilderOpt = entryFromBuilder.getAmplification();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAmplification");
        expectEqualField(expected, *fromBuilderOpt, "sfAmplification");
    }

    {
        auto const& expected = amplificationTimeValue;

        auto const fromSleOpt = entryFromSle.getAmplificationTime();
        auto const fromBuilderOpt = entryFromBuilder.getAmplificationTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAmplificationTime");
        expectEqualField(expected, *fromBuilderOpt, "sfAmplificationTime");
    }

    {
        auto const& expected = positionCountValue;

        auto const fromSleOpt = entryFromSle.getPositionCount();
        auto const fromBuilderOpt = entryFromBuilder.getPositionCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPositionCount");
        expectEqualField(expected, *fromBuilderOpt, "sfPositionCount");
    }

    {
        auto const& expected = binStepValue;

        auto const fromSleOpt = entryFromSle.getBinStep();
        auto const fromBuilderOpt = entryFromBuilder.getBinStep();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBinStep");
        expectEqualField(expected, *fromBuilderOpt, "sfBinStep");
    }

    {
        auto const& expected = activeBinIDValue;

        auto const fromSleOpt = entryFromSle.getActiveBinID();
        auto const fromBuilderOpt = entryFromBuilder.getActiveBinID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfActiveBinID");
        expectEqualField(expected, *fromBuilderOpt, "sfActiveBinID");
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
    EXPECT_FALSE(entry.hasCurveType());
    EXPECT_FALSE(entry.getCurveType().has_value());
    EXPECT_FALSE(entry.hasFeeTier());
    EXPECT_FALSE(entry.getFeeTier().has_value());
    EXPECT_FALSE(entry.hasTickSpacing());
    EXPECT_FALSE(entry.getTickSpacing().has_value());
    EXPECT_FALSE(entry.hasCurrentTick());
    EXPECT_FALSE(entry.getCurrentTick().has_value());
    EXPECT_FALSE(entry.hasActiveLiquidity());
    EXPECT_FALSE(entry.getActiveLiquidity().has_value());
    EXPECT_FALSE(entry.hasSqrtPriceX96());
    EXPECT_FALSE(entry.getSqrtPriceX96().has_value());
    EXPECT_FALSE(entry.hasFeeGrowthGlobal0());
    EXPECT_FALSE(entry.getFeeGrowthGlobal0().has_value());
    EXPECT_FALSE(entry.hasFeeGrowthGlobal1());
    EXPECT_FALSE(entry.getFeeGrowthGlobal1().has_value());
    EXPECT_FALSE(entry.hasAmplification());
    EXPECT_FALSE(entry.getAmplification().has_value());
    EXPECT_FALSE(entry.hasAmplificationTime());
    EXPECT_FALSE(entry.getAmplificationTime().has_value());
    EXPECT_FALSE(entry.hasPositionCount());
    EXPECT_FALSE(entry.getPositionCount().has_value());
    EXPECT_FALSE(entry.hasBinStep());
    EXPECT_FALSE(entry.getBinStep().has_value());
    EXPECT_FALSE(entry.hasActiveBinID());
    EXPECT_FALSE(entry.getActiveBinID().has_value());
}
}
