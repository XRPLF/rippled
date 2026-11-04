// Auto-generated unit tests for ledger entry PerpMarket


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/PerpMarket.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(PerpMarketTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const exchangeIDValue = canonical_UINT256();
    auto const baseAssetValue = canonical_CURRENCY();
    auto const quoteAssetValue = canonical_CURRENCY();
    auto const indexOraclesValue = canonical_ARRAY();
    auto const indexMaxAgeValue = canonical_UINT32();
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const midSpreadMaxValue = canonical_UINT32();
    auto const markBandValue = canonical_UINT32();
    auto const markMoveMaxValue = canonical_UINT32();
    auto const basisWindowValue = canonical_UINT32();
    auto const fundingInterestRateValue = canonical_INT32();
    auto const fundingCapValue = canonical_UINT32();
    auto const liquidationPenaltyValue = canonical_UINT32();
    auto const liquidationRewardShareValue = canonical_UINT32();
    auto const liquidationRewardMinValue = canonical_NUMBER();
    auto const liquidationChunkMinValue = canonical_NUMBER();
    auto const settleStaleAfterValue = canonical_UINT32();
    auto const indexPriceValue = canonical_NUMBER();
    auto const markPriceValue = canonical_NUMBER();
    auto const basisEMAValue = canonical_NUMBER();
    auto const fundingRateValue = canonical_NUMBER();
    auto const cumulativeFundingValue = canonical_NUMBER();
    auto const lossIndexLongValue = canonical_NUMBER();
    auto const lossIndexShortValue = canonical_NUMBER();
    auto const openInterestLongValue = canonical_NUMBER();
    auto const openInterestShortValue = canonical_NUMBER();
    auto const orderCountValue = canonical_UINT32();
    auto const lastRefreshTimeValue = canonical_UINT32();
    auto const refreshLedgerValue = canonical_UINT32();
    auto const staleSinceValue = canonical_UINT32();
    auto const settledAtValue = canonical_UINT32();
    auto const marketStatusValue = canonical_UINT8();
    auto const settlementPriceValue = canonical_NUMBER();
    auto const pendingParametersValue = canonical_OBJECT();
    auto const parameterEffectiveTimeValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    PerpMarketBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        exchangeIDValue,
        baseAssetValue,
        quoteAssetValue,
        indexOraclesValue,
        indexMaxAgeValue,
        indexMaxLedgersValue,
        priceTickValue,
        lotSizeValue,
        marginTiersValue,
        openInterestMaxValue,
        orderPriceBandValue,
        midSpreadMaxValue,
        markBandValue,
        markMoveMaxValue,
        basisWindowValue,
        fundingCapValue,
        liquidationPenaltyValue,
        liquidationRewardShareValue,
        liquidationChunkMinValue,
        settleStaleAfterValue,
        ownerNodeValue
    };

    builder.setFundingInterestRate(fundingInterestRateValue);
    builder.setLiquidationRewardMin(liquidationRewardMinValue);
    builder.setIndexPrice(indexPriceValue);
    builder.setMarkPrice(markPriceValue);
    builder.setBasisEMA(basisEMAValue);
    builder.setFundingRate(fundingRateValue);
    builder.setCumulativeFunding(cumulativeFundingValue);
    builder.setLossIndexLong(lossIndexLongValue);
    builder.setLossIndexShort(lossIndexShortValue);
    builder.setOpenInterestLong(openInterestLongValue);
    builder.setOpenInterestShort(openInterestShortValue);
    builder.setOrderCount(orderCountValue);
    builder.setLastRefreshTime(lastRefreshTimeValue);
    builder.setRefreshLedger(refreshLedgerValue);
    builder.setStaleSince(staleSinceValue);
    builder.setSettledAt(settledAtValue);
    builder.setMarketStatus(marketStatusValue);
    builder.setSettlementPrice(settlementPriceValue);
    builder.setPendingParameters(pendingParametersValue);
    builder.setParameterEffectiveTime(parameterEffectiveTimeValue);

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
        auto const& expected = exchangeIDValue;
        auto const actual = entry.getExchangeID();
        expectEqualField(expected, actual, "sfExchangeID");
    }

    {
        auto const& expected = baseAssetValue;
        auto const actual = entry.getBaseAsset();
        expectEqualField(expected, actual, "sfBaseAsset");
    }

    {
        auto const& expected = quoteAssetValue;
        auto const actual = entry.getQuoteAsset();
        expectEqualField(expected, actual, "sfQuoteAsset");
    }

    {
        auto const& expected = indexOraclesValue;
        auto const actual = entry.getIndexOracles();
        expectEqualField(expected, actual, "sfIndexOracles");
    }

    {
        auto const& expected = indexMaxAgeValue;
        auto const actual = entry.getIndexMaxAge();
        expectEqualField(expected, actual, "sfIndexMaxAge");
    }

    {
        auto const& expected = indexMaxLedgersValue;
        auto const actual = entry.getIndexMaxLedgers();
        expectEqualField(expected, actual, "sfIndexMaxLedgers");
    }

    {
        auto const& expected = priceTickValue;
        auto const actual = entry.getPriceTick();
        expectEqualField(expected, actual, "sfPriceTick");
    }

    {
        auto const& expected = lotSizeValue;
        auto const actual = entry.getLotSize();
        expectEqualField(expected, actual, "sfLotSize");
    }

    {
        auto const& expected = marginTiersValue;
        auto const actual = entry.getMarginTiers();
        expectEqualField(expected, actual, "sfMarginTiers");
    }

    {
        auto const& expected = openInterestMaxValue;
        auto const actual = entry.getOpenInterestMax();
        expectEqualField(expected, actual, "sfOpenInterestMax");
    }

    {
        auto const& expected = orderPriceBandValue;
        auto const actual = entry.getOrderPriceBand();
        expectEqualField(expected, actual, "sfOrderPriceBand");
    }

    {
        auto const& expected = midSpreadMaxValue;
        auto const actual = entry.getMidSpreadMax();
        expectEqualField(expected, actual, "sfMidSpreadMax");
    }

    {
        auto const& expected = markBandValue;
        auto const actual = entry.getMarkBand();
        expectEqualField(expected, actual, "sfMarkBand");
    }

    {
        auto const& expected = markMoveMaxValue;
        auto const actual = entry.getMarkMoveMax();
        expectEqualField(expected, actual, "sfMarkMoveMax");
    }

    {
        auto const& expected = basisWindowValue;
        auto const actual = entry.getBasisWindow();
        expectEqualField(expected, actual, "sfBasisWindow");
    }

    {
        auto const& expected = fundingCapValue;
        auto const actual = entry.getFundingCap();
        expectEqualField(expected, actual, "sfFundingCap");
    }

    {
        auto const& expected = liquidationPenaltyValue;
        auto const actual = entry.getLiquidationPenalty();
        expectEqualField(expected, actual, "sfLiquidationPenalty");
    }

    {
        auto const& expected = liquidationRewardShareValue;
        auto const actual = entry.getLiquidationRewardShare();
        expectEqualField(expected, actual, "sfLiquidationRewardShare");
    }

    {
        auto const& expected = liquidationChunkMinValue;
        auto const actual = entry.getLiquidationChunkMin();
        expectEqualField(expected, actual, "sfLiquidationChunkMin");
    }

    {
        auto const& expected = settleStaleAfterValue;
        auto const actual = entry.getSettleStaleAfter();
        expectEqualField(expected, actual, "sfSettleStaleAfter");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = fundingInterestRateValue;
        auto const actualOpt = entry.getFundingInterestRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFundingInterestRate");
        EXPECT_TRUE(entry.hasFundingInterestRate());
    }

    {
        auto const& expected = liquidationRewardMinValue;
        auto const actualOpt = entry.getLiquidationRewardMin();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLiquidationRewardMin");
        EXPECT_TRUE(entry.hasLiquidationRewardMin());
    }

    {
        auto const& expected = indexPriceValue;
        auto const actualOpt = entry.getIndexPrice();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIndexPrice");
        EXPECT_TRUE(entry.hasIndexPrice());
    }

    {
        auto const& expected = markPriceValue;
        auto const actualOpt = entry.getMarkPrice();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarkPrice");
        EXPECT_TRUE(entry.hasMarkPrice());
    }

    {
        auto const& expected = basisEMAValue;
        auto const actualOpt = entry.getBasisEMA();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBasisEMA");
        EXPECT_TRUE(entry.hasBasisEMA());
    }

    {
        auto const& expected = fundingRateValue;
        auto const actualOpt = entry.getFundingRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFundingRate");
        EXPECT_TRUE(entry.hasFundingRate());
    }

    {
        auto const& expected = cumulativeFundingValue;
        auto const actualOpt = entry.getCumulativeFunding();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCumulativeFunding");
        EXPECT_TRUE(entry.hasCumulativeFunding());
    }

    {
        auto const& expected = lossIndexLongValue;
        auto const actualOpt = entry.getLossIndexLong();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLossIndexLong");
        EXPECT_TRUE(entry.hasLossIndexLong());
    }

    {
        auto const& expected = lossIndexShortValue;
        auto const actualOpt = entry.getLossIndexShort();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLossIndexShort");
        EXPECT_TRUE(entry.hasLossIndexShort());
    }

    {
        auto const& expected = openInterestLongValue;
        auto const actualOpt = entry.getOpenInterestLong();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOpenInterestLong");
        EXPECT_TRUE(entry.hasOpenInterestLong());
    }

    {
        auto const& expected = openInterestShortValue;
        auto const actualOpt = entry.getOpenInterestShort();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOpenInterestShort");
        EXPECT_TRUE(entry.hasOpenInterestShort());
    }

    {
        auto const& expected = orderCountValue;
        auto const actualOpt = entry.getOrderCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOrderCount");
        EXPECT_TRUE(entry.hasOrderCount());
    }

    {
        auto const& expected = lastRefreshTimeValue;
        auto const actualOpt = entry.getLastRefreshTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLastRefreshTime");
        EXPECT_TRUE(entry.hasLastRefreshTime());
    }

    {
        auto const& expected = refreshLedgerValue;
        auto const actualOpt = entry.getRefreshLedger();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfRefreshLedger");
        EXPECT_TRUE(entry.hasRefreshLedger());
    }

    {
        auto const& expected = staleSinceValue;
        auto const actualOpt = entry.getStaleSince();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfStaleSince");
        EXPECT_TRUE(entry.hasStaleSince());
    }

    {
        auto const& expected = settledAtValue;
        auto const actualOpt = entry.getSettledAt();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSettledAt");
        EXPECT_TRUE(entry.hasSettledAt());
    }

    {
        auto const& expected = marketStatusValue;
        auto const actualOpt = entry.getMarketStatus();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarketStatus");
        EXPECT_TRUE(entry.hasMarketStatus());
    }

    {
        auto const& expected = settlementPriceValue;
        auto const actualOpt = entry.getSettlementPrice();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSettlementPrice");
        EXPECT_TRUE(entry.hasSettlementPrice());
    }

    {
        auto const& expected = pendingParametersValue;
        auto const actualOpt = entry.getPendingParameters();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPendingParameters");
        EXPECT_TRUE(entry.hasPendingParameters());
    }

    {
        auto const& expected = parameterEffectiveTimeValue;
        auto const actualOpt = entry.getParameterEffectiveTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfParameterEffectiveTime");
        EXPECT_TRUE(entry.hasParameterEffectiveTime());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(PerpMarketTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const exchangeIDValue = canonical_UINT256();
    auto const baseAssetValue = canonical_CURRENCY();
    auto const quoteAssetValue = canonical_CURRENCY();
    auto const indexOraclesValue = canonical_ARRAY();
    auto const indexMaxAgeValue = canonical_UINT32();
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const midSpreadMaxValue = canonical_UINT32();
    auto const markBandValue = canonical_UINT32();
    auto const markMoveMaxValue = canonical_UINT32();
    auto const basisWindowValue = canonical_UINT32();
    auto const fundingInterestRateValue = canonical_INT32();
    auto const fundingCapValue = canonical_UINT32();
    auto const liquidationPenaltyValue = canonical_UINT32();
    auto const liquidationRewardShareValue = canonical_UINT32();
    auto const liquidationRewardMinValue = canonical_NUMBER();
    auto const liquidationChunkMinValue = canonical_NUMBER();
    auto const settleStaleAfterValue = canonical_UINT32();
    auto const indexPriceValue = canonical_NUMBER();
    auto const markPriceValue = canonical_NUMBER();
    auto const basisEMAValue = canonical_NUMBER();
    auto const fundingRateValue = canonical_NUMBER();
    auto const cumulativeFundingValue = canonical_NUMBER();
    auto const lossIndexLongValue = canonical_NUMBER();
    auto const lossIndexShortValue = canonical_NUMBER();
    auto const openInterestLongValue = canonical_NUMBER();
    auto const openInterestShortValue = canonical_NUMBER();
    auto const orderCountValue = canonical_UINT32();
    auto const lastRefreshTimeValue = canonical_UINT32();
    auto const refreshLedgerValue = canonical_UINT32();
    auto const staleSinceValue = canonical_UINT32();
    auto const settledAtValue = canonical_UINT32();
    auto const marketStatusValue = canonical_UINT8();
    auto const settlementPriceValue = canonical_NUMBER();
    auto const pendingParametersValue = canonical_OBJECT();
    auto const parameterEffectiveTimeValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(PerpMarket::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfExchangeID) = exchangeIDValue;
    sle->at(sfBaseAsset) = baseAssetValue;
    sle->at(sfQuoteAsset) = quoteAssetValue;
    sle->setFieldArray(sfIndexOracles, indexOraclesValue);
    sle->at(sfIndexMaxAge) = indexMaxAgeValue;
    sle->at(sfIndexMaxLedgers) = indexMaxLedgersValue;
    sle->at(sfPriceTick) = priceTickValue;
    sle->at(sfLotSize) = lotSizeValue;
    sle->setFieldArray(sfMarginTiers, marginTiersValue);
    sle->at(sfOpenInterestMax) = openInterestMaxValue;
    sle->at(sfOrderPriceBand) = orderPriceBandValue;
    sle->at(sfMidSpreadMax) = midSpreadMaxValue;
    sle->at(sfMarkBand) = markBandValue;
    sle->at(sfMarkMoveMax) = markMoveMaxValue;
    sle->at(sfBasisWindow) = basisWindowValue;
    sle->at(sfFundingInterestRate) = fundingInterestRateValue;
    sle->at(sfFundingCap) = fundingCapValue;
    sle->at(sfLiquidationPenalty) = liquidationPenaltyValue;
    sle->at(sfLiquidationRewardShare) = liquidationRewardShareValue;
    sle->at(sfLiquidationRewardMin) = liquidationRewardMinValue;
    sle->at(sfLiquidationChunkMin) = liquidationChunkMinValue;
    sle->at(sfSettleStaleAfter) = settleStaleAfterValue;
    sle->at(sfIndexPrice) = indexPriceValue;
    sle->at(sfMarkPrice) = markPriceValue;
    sle->at(sfBasisEMA) = basisEMAValue;
    sle->at(sfFundingRate) = fundingRateValue;
    sle->at(sfCumulativeFunding) = cumulativeFundingValue;
    sle->at(sfLossIndexLong) = lossIndexLongValue;
    sle->at(sfLossIndexShort) = lossIndexShortValue;
    sle->at(sfOpenInterestLong) = openInterestLongValue;
    sle->at(sfOpenInterestShort) = openInterestShortValue;
    sle->at(sfOrderCount) = orderCountValue;
    sle->at(sfLastRefreshTime) = lastRefreshTimeValue;
    sle->at(sfRefreshLedger) = refreshLedgerValue;
    sle->at(sfStaleSince) = staleSinceValue;
    sle->at(sfSettledAt) = settledAtValue;
    sle->at(sfMarketStatus) = marketStatusValue;
    sle->at(sfSettlementPrice) = settlementPriceValue;
    sle->setFieldObject(sfPendingParameters, pendingParametersValue);
    sle->at(sfParameterEffectiveTime) = parameterEffectiveTimeValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    PerpMarketBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    PerpMarket entryFromSle{sle};
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
        auto const& expected = exchangeIDValue;

        auto const fromSle = entryFromSle.getExchangeID();
        auto const fromBuilder = entryFromBuilder.getExchangeID();

        expectEqualField(expected, fromSle, "sfExchangeID");
        expectEqualField(expected, fromBuilder, "sfExchangeID");
    }

    {
        auto const& expected = baseAssetValue;

        auto const fromSle = entryFromSle.getBaseAsset();
        auto const fromBuilder = entryFromBuilder.getBaseAsset();

        expectEqualField(expected, fromSle, "sfBaseAsset");
        expectEqualField(expected, fromBuilder, "sfBaseAsset");
    }

    {
        auto const& expected = quoteAssetValue;

        auto const fromSle = entryFromSle.getQuoteAsset();
        auto const fromBuilder = entryFromBuilder.getQuoteAsset();

        expectEqualField(expected, fromSle, "sfQuoteAsset");
        expectEqualField(expected, fromBuilder, "sfQuoteAsset");
    }

    {
        auto const& expected = indexOraclesValue;

        auto const fromSle = entryFromSle.getIndexOracles();
        auto const fromBuilder = entryFromBuilder.getIndexOracles();

        expectEqualField(expected, fromSle, "sfIndexOracles");
        expectEqualField(expected, fromBuilder, "sfIndexOracles");
    }

    {
        auto const& expected = indexMaxAgeValue;

        auto const fromSle = entryFromSle.getIndexMaxAge();
        auto const fromBuilder = entryFromBuilder.getIndexMaxAge();

        expectEqualField(expected, fromSle, "sfIndexMaxAge");
        expectEqualField(expected, fromBuilder, "sfIndexMaxAge");
    }

    {
        auto const& expected = indexMaxLedgersValue;

        auto const fromSle = entryFromSle.getIndexMaxLedgers();
        auto const fromBuilder = entryFromBuilder.getIndexMaxLedgers();

        expectEqualField(expected, fromSle, "sfIndexMaxLedgers");
        expectEqualField(expected, fromBuilder, "sfIndexMaxLedgers");
    }

    {
        auto const& expected = priceTickValue;

        auto const fromSle = entryFromSle.getPriceTick();
        auto const fromBuilder = entryFromBuilder.getPriceTick();

        expectEqualField(expected, fromSle, "sfPriceTick");
        expectEqualField(expected, fromBuilder, "sfPriceTick");
    }

    {
        auto const& expected = lotSizeValue;

        auto const fromSle = entryFromSle.getLotSize();
        auto const fromBuilder = entryFromBuilder.getLotSize();

        expectEqualField(expected, fromSle, "sfLotSize");
        expectEqualField(expected, fromBuilder, "sfLotSize");
    }

    {
        auto const& expected = marginTiersValue;

        auto const fromSle = entryFromSle.getMarginTiers();
        auto const fromBuilder = entryFromBuilder.getMarginTiers();

        expectEqualField(expected, fromSle, "sfMarginTiers");
        expectEqualField(expected, fromBuilder, "sfMarginTiers");
    }

    {
        auto const& expected = openInterestMaxValue;

        auto const fromSle = entryFromSle.getOpenInterestMax();
        auto const fromBuilder = entryFromBuilder.getOpenInterestMax();

        expectEqualField(expected, fromSle, "sfOpenInterestMax");
        expectEqualField(expected, fromBuilder, "sfOpenInterestMax");
    }

    {
        auto const& expected = orderPriceBandValue;

        auto const fromSle = entryFromSle.getOrderPriceBand();
        auto const fromBuilder = entryFromBuilder.getOrderPriceBand();

        expectEqualField(expected, fromSle, "sfOrderPriceBand");
        expectEqualField(expected, fromBuilder, "sfOrderPriceBand");
    }

    {
        auto const& expected = midSpreadMaxValue;

        auto const fromSle = entryFromSle.getMidSpreadMax();
        auto const fromBuilder = entryFromBuilder.getMidSpreadMax();

        expectEqualField(expected, fromSle, "sfMidSpreadMax");
        expectEqualField(expected, fromBuilder, "sfMidSpreadMax");
    }

    {
        auto const& expected = markBandValue;

        auto const fromSle = entryFromSle.getMarkBand();
        auto const fromBuilder = entryFromBuilder.getMarkBand();

        expectEqualField(expected, fromSle, "sfMarkBand");
        expectEqualField(expected, fromBuilder, "sfMarkBand");
    }

    {
        auto const& expected = markMoveMaxValue;

        auto const fromSle = entryFromSle.getMarkMoveMax();
        auto const fromBuilder = entryFromBuilder.getMarkMoveMax();

        expectEqualField(expected, fromSle, "sfMarkMoveMax");
        expectEqualField(expected, fromBuilder, "sfMarkMoveMax");
    }

    {
        auto const& expected = basisWindowValue;

        auto const fromSle = entryFromSle.getBasisWindow();
        auto const fromBuilder = entryFromBuilder.getBasisWindow();

        expectEqualField(expected, fromSle, "sfBasisWindow");
        expectEqualField(expected, fromBuilder, "sfBasisWindow");
    }

    {
        auto const& expected = fundingCapValue;

        auto const fromSle = entryFromSle.getFundingCap();
        auto const fromBuilder = entryFromBuilder.getFundingCap();

        expectEqualField(expected, fromSle, "sfFundingCap");
        expectEqualField(expected, fromBuilder, "sfFundingCap");
    }

    {
        auto const& expected = liquidationPenaltyValue;

        auto const fromSle = entryFromSle.getLiquidationPenalty();
        auto const fromBuilder = entryFromBuilder.getLiquidationPenalty();

        expectEqualField(expected, fromSle, "sfLiquidationPenalty");
        expectEqualField(expected, fromBuilder, "sfLiquidationPenalty");
    }

    {
        auto const& expected = liquidationRewardShareValue;

        auto const fromSle = entryFromSle.getLiquidationRewardShare();
        auto const fromBuilder = entryFromBuilder.getLiquidationRewardShare();

        expectEqualField(expected, fromSle, "sfLiquidationRewardShare");
        expectEqualField(expected, fromBuilder, "sfLiquidationRewardShare");
    }

    {
        auto const& expected = liquidationChunkMinValue;

        auto const fromSle = entryFromSle.getLiquidationChunkMin();
        auto const fromBuilder = entryFromBuilder.getLiquidationChunkMin();

        expectEqualField(expected, fromSle, "sfLiquidationChunkMin");
        expectEqualField(expected, fromBuilder, "sfLiquidationChunkMin");
    }

    {
        auto const& expected = settleStaleAfterValue;

        auto const fromSle = entryFromSle.getSettleStaleAfter();
        auto const fromBuilder = entryFromBuilder.getSettleStaleAfter();

        expectEqualField(expected, fromSle, "sfSettleStaleAfter");
        expectEqualField(expected, fromBuilder, "sfSettleStaleAfter");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = fundingInterestRateValue;

        auto const fromSleOpt = entryFromSle.getFundingInterestRate();
        auto const fromBuilderOpt = entryFromBuilder.getFundingInterestRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFundingInterestRate");
        expectEqualField(expected, *fromBuilderOpt, "sfFundingInterestRate");
    }

    {
        auto const& expected = liquidationRewardMinValue;

        auto const fromSleOpt = entryFromSle.getLiquidationRewardMin();
        auto const fromBuilderOpt = entryFromBuilder.getLiquidationRewardMin();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLiquidationRewardMin");
        expectEqualField(expected, *fromBuilderOpt, "sfLiquidationRewardMin");
    }

    {
        auto const& expected = indexPriceValue;

        auto const fromSleOpt = entryFromSle.getIndexPrice();
        auto const fromBuilderOpt = entryFromBuilder.getIndexPrice();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfIndexPrice");
        expectEqualField(expected, *fromBuilderOpt, "sfIndexPrice");
    }

    {
        auto const& expected = markPriceValue;

        auto const fromSleOpt = entryFromSle.getMarkPrice();
        auto const fromBuilderOpt = entryFromBuilder.getMarkPrice();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMarkPrice");
        expectEqualField(expected, *fromBuilderOpt, "sfMarkPrice");
    }

    {
        auto const& expected = basisEMAValue;

        auto const fromSleOpt = entryFromSle.getBasisEMA();
        auto const fromBuilderOpt = entryFromBuilder.getBasisEMA();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBasisEMA");
        expectEqualField(expected, *fromBuilderOpt, "sfBasisEMA");
    }

    {
        auto const& expected = fundingRateValue;

        auto const fromSleOpt = entryFromSle.getFundingRate();
        auto const fromBuilderOpt = entryFromBuilder.getFundingRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFundingRate");
        expectEqualField(expected, *fromBuilderOpt, "sfFundingRate");
    }

    {
        auto const& expected = cumulativeFundingValue;

        auto const fromSleOpt = entryFromSle.getCumulativeFunding();
        auto const fromBuilderOpt = entryFromBuilder.getCumulativeFunding();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCumulativeFunding");
        expectEqualField(expected, *fromBuilderOpt, "sfCumulativeFunding");
    }

    {
        auto const& expected = lossIndexLongValue;

        auto const fromSleOpt = entryFromSle.getLossIndexLong();
        auto const fromBuilderOpt = entryFromBuilder.getLossIndexLong();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLossIndexLong");
        expectEqualField(expected, *fromBuilderOpt, "sfLossIndexLong");
    }

    {
        auto const& expected = lossIndexShortValue;

        auto const fromSleOpt = entryFromSle.getLossIndexShort();
        auto const fromBuilderOpt = entryFromBuilder.getLossIndexShort();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLossIndexShort");
        expectEqualField(expected, *fromBuilderOpt, "sfLossIndexShort");
    }

    {
        auto const& expected = openInterestLongValue;

        auto const fromSleOpt = entryFromSle.getOpenInterestLong();
        auto const fromBuilderOpt = entryFromBuilder.getOpenInterestLong();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOpenInterestLong");
        expectEqualField(expected, *fromBuilderOpt, "sfOpenInterestLong");
    }

    {
        auto const& expected = openInterestShortValue;

        auto const fromSleOpt = entryFromSle.getOpenInterestShort();
        auto const fromBuilderOpt = entryFromBuilder.getOpenInterestShort();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOpenInterestShort");
        expectEqualField(expected, *fromBuilderOpt, "sfOpenInterestShort");
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

    {
        auto const& expected = lastRefreshTimeValue;

        auto const fromSleOpt = entryFromSle.getLastRefreshTime();
        auto const fromBuilderOpt = entryFromBuilder.getLastRefreshTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLastRefreshTime");
        expectEqualField(expected, *fromBuilderOpt, "sfLastRefreshTime");
    }

    {
        auto const& expected = refreshLedgerValue;

        auto const fromSleOpt = entryFromSle.getRefreshLedger();
        auto const fromBuilderOpt = entryFromBuilder.getRefreshLedger();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfRefreshLedger");
        expectEqualField(expected, *fromBuilderOpt, "sfRefreshLedger");
    }

    {
        auto const& expected = staleSinceValue;

        auto const fromSleOpt = entryFromSle.getStaleSince();
        auto const fromBuilderOpt = entryFromBuilder.getStaleSince();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfStaleSince");
        expectEqualField(expected, *fromBuilderOpt, "sfStaleSince");
    }

    {
        auto const& expected = settledAtValue;

        auto const fromSleOpt = entryFromSle.getSettledAt();
        auto const fromBuilderOpt = entryFromBuilder.getSettledAt();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSettledAt");
        expectEqualField(expected, *fromBuilderOpt, "sfSettledAt");
    }

    {
        auto const& expected = marketStatusValue;

        auto const fromSleOpt = entryFromSle.getMarketStatus();
        auto const fromBuilderOpt = entryFromBuilder.getMarketStatus();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMarketStatus");
        expectEqualField(expected, *fromBuilderOpt, "sfMarketStatus");
    }

    {
        auto const& expected = settlementPriceValue;

        auto const fromSleOpt = entryFromSle.getSettlementPrice();
        auto const fromBuilderOpt = entryFromBuilder.getSettlementPrice();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSettlementPrice");
        expectEqualField(expected, *fromBuilderOpt, "sfSettlementPrice");
    }

    {
        auto const& expected = pendingParametersValue;

        auto const fromSleOpt = entryFromSle.getPendingParameters();
        auto const fromBuilderOpt = entryFromBuilder.getPendingParameters();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPendingParameters");
        expectEqualField(expected, *fromBuilderOpt, "sfPendingParameters");
    }

    {
        auto const& expected = parameterEffectiveTimeValue;

        auto const fromSleOpt = entryFromSle.getParameterEffectiveTime();
        auto const fromBuilderOpt = entryFromBuilder.getParameterEffectiveTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfParameterEffectiveTime");
        expectEqualField(expected, *fromBuilderOpt, "sfParameterEffectiveTime");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(PerpMarketTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpMarket{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(PerpMarketTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpMarketBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(PerpMarketTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const exchangeIDValue = canonical_UINT256();
    auto const baseAssetValue = canonical_CURRENCY();
    auto const quoteAssetValue = canonical_CURRENCY();
    auto const indexOraclesValue = canonical_ARRAY();
    auto const indexMaxAgeValue = canonical_UINT32();
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const midSpreadMaxValue = canonical_UINT32();
    auto const markBandValue = canonical_UINT32();
    auto const markMoveMaxValue = canonical_UINT32();
    auto const basisWindowValue = canonical_UINT32();
    auto const fundingCapValue = canonical_UINT32();
    auto const liquidationPenaltyValue = canonical_UINT32();
    auto const liquidationRewardShareValue = canonical_UINT32();
    auto const liquidationChunkMinValue = canonical_NUMBER();
    auto const settleStaleAfterValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    PerpMarketBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        exchangeIDValue,
        baseAssetValue,
        quoteAssetValue,
        indexOraclesValue,
        indexMaxAgeValue,
        indexMaxLedgersValue,
        priceTickValue,
        lotSizeValue,
        marginTiersValue,
        openInterestMaxValue,
        orderPriceBandValue,
        midSpreadMaxValue,
        markBandValue,
        markMoveMaxValue,
        basisWindowValue,
        fundingCapValue,
        liquidationPenaltyValue,
        liquidationRewardShareValue,
        liquidationChunkMinValue,
        settleStaleAfterValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasFundingInterestRate());
    EXPECT_FALSE(entry.getFundingInterestRate().has_value());
    EXPECT_FALSE(entry.hasLiquidationRewardMin());
    EXPECT_FALSE(entry.getLiquidationRewardMin().has_value());
    EXPECT_FALSE(entry.hasIndexPrice());
    EXPECT_FALSE(entry.getIndexPrice().has_value());
    EXPECT_FALSE(entry.hasMarkPrice());
    EXPECT_FALSE(entry.getMarkPrice().has_value());
    EXPECT_FALSE(entry.hasBasisEMA());
    EXPECT_FALSE(entry.getBasisEMA().has_value());
    EXPECT_FALSE(entry.hasFundingRate());
    EXPECT_FALSE(entry.getFundingRate().has_value());
    EXPECT_FALSE(entry.hasCumulativeFunding());
    EXPECT_FALSE(entry.getCumulativeFunding().has_value());
    EXPECT_FALSE(entry.hasLossIndexLong());
    EXPECT_FALSE(entry.getLossIndexLong().has_value());
    EXPECT_FALSE(entry.hasLossIndexShort());
    EXPECT_FALSE(entry.getLossIndexShort().has_value());
    EXPECT_FALSE(entry.hasOpenInterestLong());
    EXPECT_FALSE(entry.getOpenInterestLong().has_value());
    EXPECT_FALSE(entry.hasOpenInterestShort());
    EXPECT_FALSE(entry.getOpenInterestShort().has_value());
    EXPECT_FALSE(entry.hasOrderCount());
    EXPECT_FALSE(entry.getOrderCount().has_value());
    EXPECT_FALSE(entry.hasLastRefreshTime());
    EXPECT_FALSE(entry.getLastRefreshTime().has_value());
    EXPECT_FALSE(entry.hasRefreshLedger());
    EXPECT_FALSE(entry.getRefreshLedger().has_value());
    EXPECT_FALSE(entry.hasStaleSince());
    EXPECT_FALSE(entry.getStaleSince().has_value());
    EXPECT_FALSE(entry.hasSettledAt());
    EXPECT_FALSE(entry.getSettledAt().has_value());
    EXPECT_FALSE(entry.hasMarketStatus());
    EXPECT_FALSE(entry.getMarketStatus().has_value());
    EXPECT_FALSE(entry.hasSettlementPrice());
    EXPECT_FALSE(entry.getSettlementPrice().has_value());
    EXPECT_FALSE(entry.hasPendingParameters());
    EXPECT_FALSE(entry.getPendingParameters().has_value());
    EXPECT_FALSE(entry.hasParameterEffectiveTime());
    EXPECT_FALSE(entry.getParameterEffectiveTime().has_value());
}
}
