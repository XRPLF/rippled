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
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const indexPriceValue = canonical_NUMBER();
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
    auto const statusTimeValue = canonical_UINT32();
    auto const marketStatusValue = canonical_UINT8();
    auto const ownerNodeValue = canonical_UINT64();

    PerpMarketBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        exchangeIDValue,
        baseAssetValue,
        quoteAssetValue,
        indexOraclesValue,
        indexMaxLedgersValue,
        priceTickValue,
        lotSizeValue,
        marginTiersValue,
        openInterestMaxValue,
        orderPriceBandValue,
        ownerNodeValue
    };

    builder.setIndexPrice(indexPriceValue);
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
    builder.setStatusTime(statusTimeValue);
    builder.setMarketStatus(marketStatusValue);

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
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = indexPriceValue;
        auto const actualOpt = entry.getIndexPrice();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfIndexPrice");
        EXPECT_TRUE(entry.hasIndexPrice());
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
        auto const& expected = statusTimeValue;
        auto const actualOpt = entry.getStatusTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfStatusTime");
        EXPECT_TRUE(entry.hasStatusTime());
    }

    {
        auto const& expected = marketStatusValue;
        auto const actualOpt = entry.getMarketStatus();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarketStatus");
        EXPECT_TRUE(entry.hasMarketStatus());
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
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const indexPriceValue = canonical_NUMBER();
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
    auto const statusTimeValue = canonical_UINT32();
    auto const marketStatusValue = canonical_UINT8();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(PerpMarket::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfExchangeID) = exchangeIDValue;
    sle->at(sfBaseAsset) = baseAssetValue;
    sle->at(sfQuoteAsset) = quoteAssetValue;
    sle->setFieldArray(sfIndexOracles, indexOraclesValue);
    sle->at(sfIndexMaxLedgers) = indexMaxLedgersValue;
    sle->at(sfPriceTick) = priceTickValue;
    sle->at(sfLotSize) = lotSizeValue;
    sle->setFieldArray(sfMarginTiers, marginTiersValue);
    sle->at(sfOpenInterestMax) = openInterestMaxValue;
    sle->at(sfOrderPriceBand) = orderPriceBandValue;
    sle->at(sfIndexPrice) = indexPriceValue;
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
    sle->at(sfStatusTime) = statusTimeValue;
    sle->at(sfMarketStatus) = marketStatusValue;
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
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
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
        auto const& expected = statusTimeValue;

        auto const fromSleOpt = entryFromSle.getStatusTime();
        auto const fromBuilderOpt = entryFromBuilder.getStatusTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfStatusTime");
        expectEqualField(expected, *fromBuilderOpt, "sfStatusTime");
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
    auto const indexMaxLedgersValue = canonical_UINT32();
    auto const priceTickValue = canonical_NUMBER();
    auto const lotSizeValue = canonical_NUMBER();
    auto const marginTiersValue = canonical_ARRAY();
    auto const openInterestMaxValue = canonical_NUMBER();
    auto const orderPriceBandValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    PerpMarketBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        exchangeIDValue,
        baseAssetValue,
        quoteAssetValue,
        indexOraclesValue,
        indexMaxLedgersValue,
        priceTickValue,
        lotSizeValue,
        marginTiersValue,
        openInterestMaxValue,
        orderPriceBandValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasIndexPrice());
    EXPECT_FALSE(entry.getIndexPrice().has_value());
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
    EXPECT_FALSE(entry.hasStatusTime());
    EXPECT_FALSE(entry.getStatusTime().has_value());
    EXPECT_FALSE(entry.hasMarketStatus());
    EXPECT_FALSE(entry.getMarketStatus().has_value());
}
}
