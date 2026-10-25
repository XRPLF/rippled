// Auto-generated unit tests for ledger entry VaultManager


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/VaultManager.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(VaultManagerTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const managerValue = canonical_ACCOUNT();
    auto const valuerValue = canonical_ACCOUNT();
    auto const guardianValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mandateValue = canonical_ARRAY();
    auto const deployedMaximumValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const performanceFeeRateValue = canonical_UINT32();
    auto const highWaterMarkValue = canonical_NUMBER();
    auto const lastFeeTimeValue = canonical_UINT32();
    auto const markMoveLimitValue = canonical_UINT32();
    auto const markHaltPeriodValue = canonical_UINT32();
    auto const markStaleAfterValue = canonical_UINT32();
    auto const timelockValue = canonical_UINT32();
    auto const pendingUpdateValue = canonical_OBJECT();
    auto const pendingUntilValue = canonical_UINT32();

    VaultManagerBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        vaultIDValue,
        managerValue,
        valuerValue,
        guardianValue,
        accountValue,
        mandateValue,
        deployedMaximumValue,
        markMoveLimitValue,
        markHaltPeriodValue,
        markStaleAfterValue,
        timelockValue
    };

    builder.setCoverAvailable(coverAvailableValue);
    builder.setCoverRateMinimum(coverRateMinimumValue);
    builder.setCoverRateLiquidation(coverRateLiquidationValue);
    builder.setManagementFeeRate(managementFeeRateValue);
    builder.setPerformanceFeeRate(performanceFeeRateValue);
    builder.setHighWaterMark(highWaterMarkValue);
    builder.setLastFeeTime(lastFeeTimeValue);
    builder.setPendingUpdate(pendingUpdateValue);
    builder.setPendingUntil(pendingUntilValue);

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
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = vaultIDValue;
        auto const actual = entry.getVaultID();
        expectEqualField(expected, actual, "sfVaultID");
    }

    {
        auto const& expected = managerValue;
        auto const actual = entry.getManager();
        expectEqualField(expected, actual, "sfManager");
    }

    {
        auto const& expected = valuerValue;
        auto const actual = entry.getValuer();
        expectEqualField(expected, actual, "sfValuer");
    }

    {
        auto const& expected = guardianValue;
        auto const actual = entry.getGuardian();
        expectEqualField(expected, actual, "sfGuardian");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = mandateValue;
        auto const actual = entry.getMandate();
        expectEqualField(expected, actual, "sfMandate");
    }

    {
        auto const& expected = deployedMaximumValue;
        auto const actual = entry.getDeployedMaximum();
        expectEqualField(expected, actual, "sfDeployedMaximum");
    }

    {
        auto const& expected = markMoveLimitValue;
        auto const actual = entry.getMarkMoveLimit();
        expectEqualField(expected, actual, "sfMarkMoveLimit");
    }

    {
        auto const& expected = markHaltPeriodValue;
        auto const actual = entry.getMarkHaltPeriod();
        expectEqualField(expected, actual, "sfMarkHaltPeriod");
    }

    {
        auto const& expected = markStaleAfterValue;
        auto const actual = entry.getMarkStaleAfter();
        expectEqualField(expected, actual, "sfMarkStaleAfter");
    }

    {
        auto const& expected = timelockValue;
        auto const actual = entry.getTimelock();
        expectEqualField(expected, actual, "sfTimelock");
    }

    {
        auto const& expected = coverAvailableValue;
        auto const actualOpt = entry.getCoverAvailable();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverAvailable");
        EXPECT_TRUE(entry.hasCoverAvailable());
    }

    {
        auto const& expected = coverRateMinimumValue;
        auto const actualOpt = entry.getCoverRateMinimum();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverRateMinimum");
        EXPECT_TRUE(entry.hasCoverRateMinimum());
    }

    {
        auto const& expected = coverRateLiquidationValue;
        auto const actualOpt = entry.getCoverRateLiquidation();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCoverRateLiquidation");
        EXPECT_TRUE(entry.hasCoverRateLiquidation());
    }

    {
        auto const& expected = managementFeeRateValue;
        auto const actualOpt = entry.getManagementFeeRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfManagementFeeRate");
        EXPECT_TRUE(entry.hasManagementFeeRate());
    }

    {
        auto const& expected = performanceFeeRateValue;
        auto const actualOpt = entry.getPerformanceFeeRate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPerformanceFeeRate");
        EXPECT_TRUE(entry.hasPerformanceFeeRate());
    }

    {
        auto const& expected = highWaterMarkValue;
        auto const actualOpt = entry.getHighWaterMark();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfHighWaterMark");
        EXPECT_TRUE(entry.hasHighWaterMark());
    }

    {
        auto const& expected = lastFeeTimeValue;
        auto const actualOpt = entry.getLastFeeTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLastFeeTime");
        EXPECT_TRUE(entry.hasLastFeeTime());
    }

    {
        auto const& expected = pendingUpdateValue;
        auto const actualOpt = entry.getPendingUpdate();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPendingUpdate");
        EXPECT_TRUE(entry.hasPendingUpdate());
    }

    {
        auto const& expected = pendingUntilValue;
        auto const actualOpt = entry.getPendingUntil();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPendingUntil");
        EXPECT_TRUE(entry.hasPendingUntil());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(VaultManagerTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const managerValue = canonical_ACCOUNT();
    auto const valuerValue = canonical_ACCOUNT();
    auto const guardianValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mandateValue = canonical_ARRAY();
    auto const deployedMaximumValue = canonical_NUMBER();
    auto const coverAvailableValue = canonical_NUMBER();
    auto const coverRateMinimumValue = canonical_UINT32();
    auto const coverRateLiquidationValue = canonical_UINT32();
    auto const managementFeeRateValue = canonical_UINT16();
    auto const performanceFeeRateValue = canonical_UINT32();
    auto const highWaterMarkValue = canonical_NUMBER();
    auto const lastFeeTimeValue = canonical_UINT32();
    auto const markMoveLimitValue = canonical_UINT32();
    auto const markHaltPeriodValue = canonical_UINT32();
    auto const markStaleAfterValue = canonical_UINT32();
    auto const timelockValue = canonical_UINT32();
    auto const pendingUpdateValue = canonical_OBJECT();
    auto const pendingUntilValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(VaultManager::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfVaultID) = vaultIDValue;
    sle->at(sfManager) = managerValue;
    sle->at(sfValuer) = valuerValue;
    sle->at(sfGuardian) = guardianValue;
    sle->at(sfAccount) = accountValue;
    sle->setFieldArray(sfMandate, mandateValue);
    sle->at(sfDeployedMaximum) = deployedMaximumValue;
    sle->at(sfCoverAvailable) = coverAvailableValue;
    sle->at(sfCoverRateMinimum) = coverRateMinimumValue;
    sle->at(sfCoverRateLiquidation) = coverRateLiquidationValue;
    sle->at(sfManagementFeeRate) = managementFeeRateValue;
    sle->at(sfPerformanceFeeRate) = performanceFeeRateValue;
    sle->at(sfHighWaterMark) = highWaterMarkValue;
    sle->at(sfLastFeeTime) = lastFeeTimeValue;
    sle->at(sfMarkMoveLimit) = markMoveLimitValue;
    sle->at(sfMarkHaltPeriod) = markHaltPeriodValue;
    sle->at(sfMarkStaleAfter) = markStaleAfterValue;
    sle->at(sfTimelock) = timelockValue;
    sle->setFieldObject(sfPendingUpdate, pendingUpdateValue);
    sle->at(sfPendingUntil) = pendingUntilValue;

    VaultManagerBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    VaultManager entryFromSle{sle};
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
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = vaultIDValue;

        auto const fromSle = entryFromSle.getVaultID();
        auto const fromBuilder = entryFromBuilder.getVaultID();

        expectEqualField(expected, fromSle, "sfVaultID");
        expectEqualField(expected, fromBuilder, "sfVaultID");
    }

    {
        auto const& expected = managerValue;

        auto const fromSle = entryFromSle.getManager();
        auto const fromBuilder = entryFromBuilder.getManager();

        expectEqualField(expected, fromSle, "sfManager");
        expectEqualField(expected, fromBuilder, "sfManager");
    }

    {
        auto const& expected = valuerValue;

        auto const fromSle = entryFromSle.getValuer();
        auto const fromBuilder = entryFromBuilder.getValuer();

        expectEqualField(expected, fromSle, "sfValuer");
        expectEqualField(expected, fromBuilder, "sfValuer");
    }

    {
        auto const& expected = guardianValue;

        auto const fromSle = entryFromSle.getGuardian();
        auto const fromBuilder = entryFromBuilder.getGuardian();

        expectEqualField(expected, fromSle, "sfGuardian");
        expectEqualField(expected, fromBuilder, "sfGuardian");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = mandateValue;

        auto const fromSle = entryFromSle.getMandate();
        auto const fromBuilder = entryFromBuilder.getMandate();

        expectEqualField(expected, fromSle, "sfMandate");
        expectEqualField(expected, fromBuilder, "sfMandate");
    }

    {
        auto const& expected = deployedMaximumValue;

        auto const fromSle = entryFromSle.getDeployedMaximum();
        auto const fromBuilder = entryFromBuilder.getDeployedMaximum();

        expectEqualField(expected, fromSle, "sfDeployedMaximum");
        expectEqualField(expected, fromBuilder, "sfDeployedMaximum");
    }

    {
        auto const& expected = markMoveLimitValue;

        auto const fromSle = entryFromSle.getMarkMoveLimit();
        auto const fromBuilder = entryFromBuilder.getMarkMoveLimit();

        expectEqualField(expected, fromSle, "sfMarkMoveLimit");
        expectEqualField(expected, fromBuilder, "sfMarkMoveLimit");
    }

    {
        auto const& expected = markHaltPeriodValue;

        auto const fromSle = entryFromSle.getMarkHaltPeriod();
        auto const fromBuilder = entryFromBuilder.getMarkHaltPeriod();

        expectEqualField(expected, fromSle, "sfMarkHaltPeriod");
        expectEqualField(expected, fromBuilder, "sfMarkHaltPeriod");
    }

    {
        auto const& expected = markStaleAfterValue;

        auto const fromSle = entryFromSle.getMarkStaleAfter();
        auto const fromBuilder = entryFromBuilder.getMarkStaleAfter();

        expectEqualField(expected, fromSle, "sfMarkStaleAfter");
        expectEqualField(expected, fromBuilder, "sfMarkStaleAfter");
    }

    {
        auto const& expected = timelockValue;

        auto const fromSle = entryFromSle.getTimelock();
        auto const fromBuilder = entryFromBuilder.getTimelock();

        expectEqualField(expected, fromSle, "sfTimelock");
        expectEqualField(expected, fromBuilder, "sfTimelock");
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
        auto const& expected = coverRateMinimumValue;

        auto const fromSleOpt = entryFromSle.getCoverRateMinimum();
        auto const fromBuilderOpt = entryFromBuilder.getCoverRateMinimum();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverRateMinimum");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverRateMinimum");
    }

    {
        auto const& expected = coverRateLiquidationValue;

        auto const fromSleOpt = entryFromSle.getCoverRateLiquidation();
        auto const fromBuilderOpt = entryFromBuilder.getCoverRateLiquidation();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCoverRateLiquidation");
        expectEqualField(expected, *fromBuilderOpt, "sfCoverRateLiquidation");
    }

    {
        auto const& expected = managementFeeRateValue;

        auto const fromSleOpt = entryFromSle.getManagementFeeRate();
        auto const fromBuilderOpt = entryFromBuilder.getManagementFeeRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfManagementFeeRate");
        expectEqualField(expected, *fromBuilderOpt, "sfManagementFeeRate");
    }

    {
        auto const& expected = performanceFeeRateValue;

        auto const fromSleOpt = entryFromSle.getPerformanceFeeRate();
        auto const fromBuilderOpt = entryFromBuilder.getPerformanceFeeRate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPerformanceFeeRate");
        expectEqualField(expected, *fromBuilderOpt, "sfPerformanceFeeRate");
    }

    {
        auto const& expected = highWaterMarkValue;

        auto const fromSleOpt = entryFromSle.getHighWaterMark();
        auto const fromBuilderOpt = entryFromBuilder.getHighWaterMark();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfHighWaterMark");
        expectEqualField(expected, *fromBuilderOpt, "sfHighWaterMark");
    }

    {
        auto const& expected = lastFeeTimeValue;

        auto const fromSleOpt = entryFromSle.getLastFeeTime();
        auto const fromBuilderOpt = entryFromBuilder.getLastFeeTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLastFeeTime");
        expectEqualField(expected, *fromBuilderOpt, "sfLastFeeTime");
    }

    {
        auto const& expected = pendingUpdateValue;

        auto const fromSleOpt = entryFromSle.getPendingUpdate();
        auto const fromBuilderOpt = entryFromBuilder.getPendingUpdate();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPendingUpdate");
        expectEqualField(expected, *fromBuilderOpt, "sfPendingUpdate");
    }

    {
        auto const& expected = pendingUntilValue;

        auto const fromSleOpt = entryFromSle.getPendingUntil();
        auto const fromBuilderOpt = entryFromBuilder.getPendingUntil();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPendingUntil");
        expectEqualField(expected, *fromBuilderOpt, "sfPendingUntil");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(VaultManagerTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(VaultManager{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(VaultManagerTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(VaultManagerBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(VaultManagerTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const vaultIDValue = canonical_UINT256();
    auto const managerValue = canonical_ACCOUNT();
    auto const valuerValue = canonical_ACCOUNT();
    auto const guardianValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mandateValue = canonical_ARRAY();
    auto const deployedMaximumValue = canonical_NUMBER();
    auto const markMoveLimitValue = canonical_UINT32();
    auto const markHaltPeriodValue = canonical_UINT32();
    auto const markStaleAfterValue = canonical_UINT32();
    auto const timelockValue = canonical_UINT32();

    VaultManagerBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        vaultIDValue,
        managerValue,
        valuerValue,
        guardianValue,
        accountValue,
        mandateValue,
        deployedMaximumValue,
        markMoveLimitValue,
        markHaltPeriodValue,
        markStaleAfterValue,
        timelockValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasCoverAvailable());
    EXPECT_FALSE(entry.getCoverAvailable().has_value());
    EXPECT_FALSE(entry.hasCoverRateMinimum());
    EXPECT_FALSE(entry.getCoverRateMinimum().has_value());
    EXPECT_FALSE(entry.hasCoverRateLiquidation());
    EXPECT_FALSE(entry.getCoverRateLiquidation().has_value());
    EXPECT_FALSE(entry.hasManagementFeeRate());
    EXPECT_FALSE(entry.getManagementFeeRate().has_value());
    EXPECT_FALSE(entry.hasPerformanceFeeRate());
    EXPECT_FALSE(entry.getPerformanceFeeRate().has_value());
    EXPECT_FALSE(entry.hasHighWaterMark());
    EXPECT_FALSE(entry.getHighWaterMark().has_value());
    EXPECT_FALSE(entry.hasLastFeeTime());
    EXPECT_FALSE(entry.getLastFeeTime().has_value());
    EXPECT_FALSE(entry.hasPendingUpdate());
    EXPECT_FALSE(entry.getPendingUpdate().has_value());
    EXPECT_FALSE(entry.hasPendingUntil());
    EXPECT_FALSE(entry.getPendingUntil().has_value());
}
}
