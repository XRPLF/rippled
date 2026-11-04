// Auto-generated unit tests for ledger entry PerpExchange


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/PerpExchange.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(PerpExchangeTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const scaleValue = canonical_UINT8();
    auto const domainIDValue = canonical_UINT256();
    auto const vaultIDValue = canonical_UINT256();
    auto const makerFeeValue = canonical_UINT32();
    auto const takerFeeValue = canonical_UINT32();
    auto const ownerFeeShareValue = canonical_UINT32();
    auto const vaultFeeShareValue = canonical_UINT32();
    auto const builderFeeMaxValue = canonical_UINT32();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const vaultLossLimitValue = canonical_UINT32();
    auto const vaultLossWindowValue = canonical_UINT32();
    auto const parameterDelayValue = canonical_UINT32();
    auto const assetsTotalValue = canonical_NUMBER();
    auto const cashTotalValue = canonical_NUMBER();
    auto const insuranceFundValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const ownerFeesValue = canonical_NUMBER();
    auto const settlementBalanceValue = canonical_NUMBER();
    auto const badDebtValue = canonical_NUMBER();
    auto const openInterestNotionalValue = canonical_NUMBER();
    auto const vaultLossWindowStartValue = canonical_UINT32();
    auto const vaultLossInWindowValue = canonical_NUMBER();
    auto const marketCountValue = canonical_UINT32();
    auto const marginAccountCountValue = canonical_UINT32();
    auto const pendingParametersValue = canonical_OBJECT();
    auto const parameterEffectiveTimeValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();

    PerpExchangeBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerValue,
        sequenceValue,
        accountValue,
        assetValue,
        parameterDelayValue,
        ownerNodeValue
    };

    builder.setScale(scaleValue);
    builder.setDomainID(domainIDValue);
    builder.setVaultID(vaultIDValue);
    builder.setMakerFee(makerFeeValue);
    builder.setTakerFee(takerFeeValue);
    builder.setOwnerFeeShare(ownerFeeShareValue);
    builder.setVaultFeeShare(vaultFeeShareValue);
    builder.setBuilderFeeMax(builderFeeMaxValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setVaultLossLimit(vaultLossLimitValue);
    builder.setVaultLossWindow(vaultLossWindowValue);
    builder.setAssetsTotal(assetsTotalValue);
    builder.setCashTotal(cashTotalValue);
    builder.setInsuranceFund(insuranceFundValue);
    builder.setCoverAvailable(coverAvailableValue);
    builder.setOwnerFees(ownerFeesValue);
    builder.setSettlementBalance(settlementBalanceValue);
    builder.setBadDebt(badDebtValue);
    builder.setOpenInterestNotional(openInterestNotionalValue);
    builder.setVaultLossWindowStart(vaultLossWindowStartValue);
    builder.setVaultLossInWindow(vaultLossInWindowValue);
    builder.setMarketCount(marketCountValue);
    builder.setMarginAccountCount(marginAccountCountValue);
    builder.setPendingParameters(pendingParametersValue);
    builder.setParameterEffectiveTime(parameterEffectiveTimeValue);
    builder.setData(dataValue);

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
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = assetValue;
        auto const actual = entry.getAsset();
        expectEqualField(expected, actual, "sfAsset");
    }

    {
        auto const& expected = parameterDelayValue;
        auto const actual = entry.getParameterDelay();
        expectEqualField(expected, actual, "sfParameterDelay");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = scaleValue;
        auto const actualOpt = entry.getScale();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfScale");
        EXPECT_TRUE(entry.hasScale());
    }

    {
        auto const& expected = domainIDValue;
        auto const actualOpt = entry.getDomainID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfDomainID");
        EXPECT_TRUE(entry.hasDomainID());
    }

    {
        auto const& expected = vaultIDValue;
        auto const actualOpt = entry.getVaultID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultID");
        EXPECT_TRUE(entry.hasVaultID());
    }

    {
        auto const& expected = makerFeeValue;
        auto const actualOpt = entry.getMakerFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMakerFee");
        EXPECT_TRUE(entry.hasMakerFee());
    }

    {
        auto const& expected = takerFeeValue;
        auto const actualOpt = entry.getTakerFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTakerFee");
        EXPECT_TRUE(entry.hasTakerFee());
    }

    {
        auto const& expected = ownerFeeShareValue;
        auto const actualOpt = entry.getOwnerFeeShare();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerFeeShare");
        EXPECT_TRUE(entry.hasOwnerFeeShare());
    }

    {
        auto const& expected = vaultFeeShareValue;
        auto const actualOpt = entry.getVaultFeeShare();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultFeeShare");
        EXPECT_TRUE(entry.hasVaultFeeShare());
    }

    {
        auto const& expected = builderFeeMaxValue;
        auto const actualOpt = entry.getBuilderFeeMax();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBuilderFeeMax");
        EXPECT_TRUE(entry.hasBuilderFeeMax());
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = entry.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
        EXPECT_TRUE(entry.hasCoverRateMinimum());
    }

    {
        auto const& expected = vaultLossLimitValue;
        auto const actualOpt = entry.getVaultLossLimit();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultLossLimit");
        EXPECT_TRUE(entry.hasVaultLossLimit());
    }

    {
        auto const& expected = vaultLossWindowValue;
        auto const actualOpt = entry.getVaultLossWindow();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultLossWindow");
        EXPECT_TRUE(entry.hasVaultLossWindow());
    }

    {
        auto const& expected = assetsTotalValue;
        auto const actualOpt = entry.getAssetsTotal();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAssetsTotal");
        EXPECT_TRUE(entry.hasAssetsTotal());
    }

    {
        auto const& expected = cashTotalValue;
        auto const actualOpt = entry.getCashTotal();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCashTotal");
        EXPECT_TRUE(entry.hasCashTotal());
    }

    {
        auto const& expected = insuranceFundValue;
        auto const actualOpt = entry.getInsuranceFund();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfInsuranceFund");
        EXPECT_TRUE(entry.hasInsuranceFund());
    }

    {
        auto const& expected = coverAvailableValue;
        auto const actualOpt = entry.getCoverAvailable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverAvailable");
        EXPECT_TRUE(entry.hasCoverAvailable());
    }

    {
        auto const& expected = ownerFeesValue;
        auto const actualOpt = entry.getOwnerFees();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOwnerFees");
        EXPECT_TRUE(entry.hasOwnerFees());
    }

    {
        auto const& expected = settlementBalanceValue;
        auto const actualOpt = entry.getSettlementBalance();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfSettlementBalance");
        EXPECT_TRUE(entry.hasSettlementBalance());
    }

    {
        auto const& expected = badDebtValue;
        auto const actualOpt = entry.getBadDebt();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBadDebt");
        EXPECT_TRUE(entry.hasBadDebt());
    }

    {
        auto const& expected = openInterestNotionalValue;
        auto const actualOpt = entry.getOpenInterestNotional();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfOpenInterestNotional");
        EXPECT_TRUE(entry.hasOpenInterestNotional());
    }

    {
        auto const& expected = vaultLossWindowStartValue;
        auto const actualOpt = entry.getVaultLossWindowStart();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultLossWindowStart");
        EXPECT_TRUE(entry.hasVaultLossWindowStart());
    }

    {
        auto const& expected = vaultLossInWindowValue;
        auto const actualOpt = entry.getVaultLossInWindow();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfVaultLossInWindow");
        EXPECT_TRUE(entry.hasVaultLossInWindow());
    }

    {
        auto const& expected = marketCountValue;
        auto const actualOpt = entry.getMarketCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarketCount");
        EXPECT_TRUE(entry.hasMarketCount());
    }

    {
        auto const& expected = marginAccountCountValue;
        auto const actualOpt = entry.getMarginAccountCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMarginAccountCount");
        EXPECT_TRUE(entry.hasMarginAccountCount());
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

    {
        auto const& expected = dataValue;
        auto const actualOpt = entry.getData();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfData");
        EXPECT_TRUE(entry.hasData());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(PerpExchangeTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const scaleValue = canonical_UINT8();
    auto const domainIDValue = canonical_UINT256();
    auto const vaultIDValue = canonical_UINT256();
    auto const makerFeeValue = canonical_UINT32();
    auto const takerFeeValue = canonical_UINT32();
    auto const ownerFeeShareValue = canonical_UINT32();
    auto const vaultFeeShareValue = canonical_UINT32();
    auto const builderFeeMaxValue = canonical_UINT32();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const vaultLossLimitValue = canonical_UINT32();
    auto const vaultLossWindowValue = canonical_UINT32();
    auto const parameterDelayValue = canonical_UINT32();
    auto const assetsTotalValue = canonical_NUMBER();
    auto const cashTotalValue = canonical_NUMBER();
    auto const insuranceFundValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const ownerFeesValue = canonical_NUMBER();
    auto const settlementBalanceValue = canonical_NUMBER();
    auto const badDebtValue = canonical_NUMBER();
    auto const openInterestNotionalValue = canonical_NUMBER();
    auto const vaultLossWindowStartValue = canonical_UINT32();
    auto const vaultLossInWindowValue = canonical_NUMBER();
    auto const marketCountValue = canonical_UINT32();
    auto const marginAccountCountValue = canonical_UINT32();
    auto const pendingParametersValue = canonical_OBJECT();
    auto const parameterEffectiveTimeValue = canonical_UINT32();
    auto const dataValue = canonical_VL();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(PerpExchange::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfAsset) = STIssue(sfAsset, assetValue);
    sle->at(sfScale) = scaleValue;
    sle->at(sfDomainID) = domainIDValue;
    sle->at(sfVaultID) = vaultIDValue;
    sle->at(sfMakerFee) = makerFeeValue;
    sle->at(sfTakerFee) = takerFeeValue;
    sle->at(sfOwnerFeeShare) = ownerFeeShareValue;
    sle->at(sfVaultFeeShare) = vaultFeeShareValue;
    sle->at(sfBuilderFeeMax) = builderFeeMaxValue;
    sle->at(sfCoverRateMinimum) = coverRateMinimumValue;
    sle->at(sfVaultLossLimit) = vaultLossLimitValue;
    sle->at(sfVaultLossWindow) = vaultLossWindowValue;
    sle->at(sfParameterDelay) = parameterDelayValue;
    sle->at(sfAssetsTotal) = assetsTotalValue;
    sle->at(sfCashTotal) = cashTotalValue;
    sle->at(sfInsuranceFund) = insuranceFundValue;
    sle->at(sfCoverAvailable) = coverAvailableValue;
    sle->at(sfOwnerFees) = ownerFeesValue;
    sle->at(sfSettlementBalance) = settlementBalanceValue;
    sle->at(sfBadDebt) = badDebtValue;
    sle->at(sfOpenInterestNotional) = openInterestNotionalValue;
    sle->at(sfVaultLossWindowStart) = vaultLossWindowStartValue;
    sle->at(sfVaultLossInWindow) = vaultLossInWindowValue;
    sle->at(sfMarketCount) = marketCountValue;
    sle->at(sfMarginAccountCount) = marginAccountCountValue;
    sle->setFieldObject(sfPendingParameters, pendingParametersValue);
    sle->at(sfParameterEffectiveTime) = parameterEffectiveTimeValue;
    sle->at(sfData) = dataValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    PerpExchangeBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    PerpExchange entryFromSle{sle};
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
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = assetValue;

        auto const fromSle = entryFromSle.getAsset();
        auto const fromBuilder = entryFromBuilder.getAsset();

        expectEqualField(expected, fromSle, "sfAsset");
        expectEqualField(expected, fromBuilder, "sfAsset");
    }

    {
        auto const& expected = parameterDelayValue;

        auto const fromSle = entryFromSle.getParameterDelay();
        auto const fromBuilder = entryFromBuilder.getParameterDelay();

        expectEqualField(expected, fromSle, "sfParameterDelay");
        expectEqualField(expected, fromBuilder, "sfParameterDelay");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = scaleValue;

        auto const fromSleOpt = entryFromSle.getScale();
        auto const fromBuilderOpt = entryFromBuilder.getScale();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfScale");
        expectEqualField(expected, *fromBuilderOpt, "sfScale");
    }

    {
        auto const& expected = domainIDValue;

        auto const fromSleOpt = entryFromSle.getDomainID();
        auto const fromBuilderOpt = entryFromBuilder.getDomainID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfDomainID");
        expectEqualField(expected, *fromBuilderOpt, "sfDomainID");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSleOpt = entryFromSle.getVaultID();
        auto const fromBuilderOpt = entryFromBuilder.getVaultID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultID");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultID");
    }

    {
        auto const& expected = makerFeeValue;

        auto const fromSleOpt = entryFromSle.getMakerFee();
        auto const fromBuilderOpt = entryFromBuilder.getMakerFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMakerFee");
        expectEqualField(expected, *fromBuilderOpt, "sfMakerFee");
    }

    {
        auto const& expected = takerFeeValue;

        auto const fromSleOpt = entryFromSle.getTakerFee();
        auto const fromBuilderOpt = entryFromBuilder.getTakerFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTakerFee");
        expectEqualField(expected, *fromBuilderOpt, "sfTakerFee");
    }

    {
        auto const& expected = ownerFeeShareValue;

        auto const fromSleOpt = entryFromSle.getOwnerFeeShare();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerFeeShare();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerFeeShare");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerFeeShare");
    }

    {
        auto const& expected = vaultFeeShareValue;

        auto const fromSleOpt = entryFromSle.getVaultFeeShare();
        auto const fromBuilderOpt = entryFromBuilder.getVaultFeeShare();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultFeeShare");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultFeeShare");
    }

    {
        auto const& expected = builderFeeMaxValue;

        auto const fromSleOpt = entryFromSle.getBuilderFeeMax();
        auto const fromBuilderOpt = entryFromBuilder.getBuilderFeeMax();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBuilderFeeMax");
        expectEqualField(expected, *fromBuilderOpt, "sfBuilderFeeMax");
    }

    {
        auto const& expected = coverRateMinimumValue;

        auto const fromSleOpt = entryFromSle.getCoverRateMinimum();
        auto const fromBuilderOpt = entryFromBuilder.getCoverRateMinimum();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverRateMinimum");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverRateMinimum");
    }

    {
        auto const& expected = vaultLossLimitValue;

        auto const fromSleOpt = entryFromSle.getVaultLossLimit();
        auto const fromBuilderOpt = entryFromBuilder.getVaultLossLimit();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultLossLimit");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultLossLimit");
    }

    {
        auto const& expected = vaultLossWindowValue;

        auto const fromSleOpt = entryFromSle.getVaultLossWindow();
        auto const fromBuilderOpt = entryFromBuilder.getVaultLossWindow();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultLossWindow");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultLossWindow");
    }

    {
        auto const& expected = assetsTotalValue;

        auto const fromSleOpt = entryFromSle.getAssetsTotal();
        auto const fromBuilderOpt = entryFromBuilder.getAssetsTotal();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAssetsTotal");
        expectEqualField(expected, *fromBuilderOpt, "sfAssetsTotal");
    }

    {
        auto const& expected = cashTotalValue;

        auto const fromSleOpt = entryFromSle.getCashTotal();
        auto const fromBuilderOpt = entryFromBuilder.getCashTotal();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCashTotal");
        expectEqualField(expected, *fromBuilderOpt, "sfCashTotal");
    }

    {
        auto const& expected = insuranceFundValue;

        auto const fromSleOpt = entryFromSle.getInsuranceFund();
        auto const fromBuilderOpt = entryFromBuilder.getInsuranceFund();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfInsuranceFund");
        expectEqualField(expected, *fromBuilderOpt, "sfInsuranceFund");
    }

    {
        auto const& expected = coverAvailableValue;

        auto const fromSleOpt = entryFromSle.getCoverAvailable();
        auto const fromBuilderOpt = entryFromBuilder.getCoverAvailable();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverAvailable");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverAvailable");
    }

    {
        auto const& expected = ownerFeesValue;

        auto const fromSleOpt = entryFromSle.getOwnerFees();
        auto const fromBuilderOpt = entryFromBuilder.getOwnerFees();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOwnerFees");
        expectEqualField(expected, *fromBuilderOpt, "sfOwnerFees");
    }

    {
        auto const& expected = settlementBalanceValue;

        auto const fromSleOpt = entryFromSle.getSettlementBalance();
        auto const fromBuilderOpt = entryFromBuilder.getSettlementBalance();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfSettlementBalance");
        expectEqualField(expected, *fromBuilderOpt, "sfSettlementBalance");
    }

    {
        auto const& expected = badDebtValue;

        auto const fromSleOpt = entryFromSle.getBadDebt();
        auto const fromBuilderOpt = entryFromBuilder.getBadDebt();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBadDebt");
        expectEqualField(expected, *fromBuilderOpt, "sfBadDebt");
    }

    {
        auto const& expected = openInterestNotionalValue;

        auto const fromSleOpt = entryFromSle.getOpenInterestNotional();
        auto const fromBuilderOpt = entryFromBuilder.getOpenInterestNotional();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfOpenInterestNotional");
        expectEqualField(expected, *fromBuilderOpt, "sfOpenInterestNotional");
    }

    {
        auto const& expected = vaultLossWindowStartValue;

        auto const fromSleOpt = entryFromSle.getVaultLossWindowStart();
        auto const fromBuilderOpt = entryFromBuilder.getVaultLossWindowStart();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultLossWindowStart");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultLossWindowStart");
    }

    {
        auto const& expected = vaultLossInWindowValue;

        auto const fromSleOpt = entryFromSle.getVaultLossInWindow();
        auto const fromBuilderOpt = entryFromBuilder.getVaultLossInWindow();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfVaultLossInWindow");
        expectEqualField(expected, *fromBuilderOpt, "sfVaultLossInWindow");
    }

    {
        auto const& expected = marketCountValue;

        auto const fromSleOpt = entryFromSle.getMarketCount();
        auto const fromBuilderOpt = entryFromBuilder.getMarketCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMarketCount");
        expectEqualField(expected, *fromBuilderOpt, "sfMarketCount");
    }

    {
        auto const& expected = marginAccountCountValue;

        auto const fromSleOpt = entryFromSle.getMarginAccountCount();
        auto const fromBuilderOpt = entryFromBuilder.getMarginAccountCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMarginAccountCount");
        expectEqualField(expected, *fromBuilderOpt, "sfMarginAccountCount");
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

    {
        auto const& expected = dataValue;

        auto const fromSleOpt = entryFromSle.getData();
        auto const fromBuilderOpt = entryFromBuilder.getData();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfData");
        expectEqualField(expected, *fromBuilderOpt, "sfData");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(PerpExchangeTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpExchange{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(PerpExchangeTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpExchangeBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(PerpExchangeTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const assetValue = canonical_ISSUE();
    auto const parameterDelayValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();

    PerpExchangeBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerValue,
        sequenceValue,
        accountValue,
        assetValue,
        parameterDelayValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasScale());
    EXPECT_FALSE(entry.getScale().has_value());
    EXPECT_FALSE(entry.hasDomainID());
    EXPECT_FALSE(entry.getDomainID().has_value());
    EXPECT_FALSE(entry.hasVaultID());
    EXPECT_FALSE(entry.getVaultID().has_value());
    EXPECT_FALSE(entry.hasMakerFee());
    EXPECT_FALSE(entry.getMakerFee().has_value());
    EXPECT_FALSE(entry.hasTakerFee());
    EXPECT_FALSE(entry.getTakerFee().has_value());
    EXPECT_FALSE(entry.hasOwnerFeeShare());
    EXPECT_FALSE(entry.getOwnerFeeShare().has_value());
    EXPECT_FALSE(entry.hasVaultFeeShare());
    EXPECT_FALSE(entry.getVaultFeeShare().has_value());
    EXPECT_FALSE(entry.hasBuilderFeeMax());
    EXPECT_FALSE(entry.getBuilderFeeMax().has_value());
    EXPECT_FALSE(entry.hasCoverRateMinimum());
    EXPECT_FALSE(entry.getCoverRateMinimum().has_value());
    EXPECT_FALSE(entry.hasVaultLossLimit());
    EXPECT_FALSE(entry.getVaultLossLimit().has_value());
    EXPECT_FALSE(entry.hasVaultLossWindow());
    EXPECT_FALSE(entry.getVaultLossWindow().has_value());
    EXPECT_FALSE(entry.hasAssetsTotal());
    EXPECT_FALSE(entry.getAssetsTotal().has_value());
    EXPECT_FALSE(entry.hasCashTotal());
    EXPECT_FALSE(entry.getCashTotal().has_value());
    EXPECT_FALSE(entry.hasInsuranceFund());
    EXPECT_FALSE(entry.getInsuranceFund().has_value());
    EXPECT_FALSE(entry.hasCoverAvailable());
    EXPECT_FALSE(entry.getCoverAvailable().has_value());
    EXPECT_FALSE(entry.hasOwnerFees());
    EXPECT_FALSE(entry.getOwnerFees().has_value());
    EXPECT_FALSE(entry.hasSettlementBalance());
    EXPECT_FALSE(entry.getSettlementBalance().has_value());
    EXPECT_FALSE(entry.hasBadDebt());
    EXPECT_FALSE(entry.getBadDebt().has_value());
    EXPECT_FALSE(entry.hasOpenInterestNotional());
    EXPECT_FALSE(entry.getOpenInterestNotional().has_value());
    EXPECT_FALSE(entry.hasVaultLossWindowStart());
    EXPECT_FALSE(entry.getVaultLossWindowStart().has_value());
    EXPECT_FALSE(entry.hasVaultLossInWindow());
    EXPECT_FALSE(entry.getVaultLossInWindow().has_value());
    EXPECT_FALSE(entry.hasMarketCount());
    EXPECT_FALSE(entry.getMarketCount().has_value());
    EXPECT_FALSE(entry.hasMarginAccountCount());
    EXPECT_FALSE(entry.getMarginAccountCount().has_value());
    EXPECT_FALSE(entry.hasPendingParameters());
    EXPECT_FALSE(entry.getPendingParameters().has_value());
    EXPECT_FALSE(entry.hasParameterEffectiveTime());
    EXPECT_FALSE(entry.getParameterEffectiveTime().has_value());
    EXPECT_FALSE(entry.hasData());
    EXPECT_FALSE(entry.getData().has_value());
}
}
