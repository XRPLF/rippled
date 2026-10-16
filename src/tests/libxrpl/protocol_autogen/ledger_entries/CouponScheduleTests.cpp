// Auto-generated unit tests for ledger entry CouponSchedule


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/CouponSchedule.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(CouponScheduleTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();
    auto const accruedPerUnitValue = canonical_AMOUNT();
    auto const poolAmountValue = canonical_AMOUNT();
    auto const claimantCountValue = canonical_UINT32();
    auto const couponCountValue = canonical_UINT32();
    auto const lastCouponTimeValue = canonical_UINT32();
    auto const couponAmountValue = canonical_AMOUNT();
    auto const couponIntervalValue = canonical_UINT32();
    auto const firstCouponTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const callNoticePeriodValue = canonical_UINT32();
    auto const earliestCallTimeValue = canonical_UINT32();

    CouponScheduleBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        ownerValue,
        accountValue,
        mPTokenIssuanceIDValue,
        couponAssetValue
    };

    builder.setAccruedPerUnit(accruedPerUnitValue);
    builder.setPoolAmount(poolAmountValue);
    builder.setClaimantCount(claimantCountValue);
    builder.setCouponCount(couponCountValue);
    builder.setLastCouponTime(lastCouponTimeValue);
    builder.setCouponAmount(couponAmountValue);
    builder.setCouponInterval(couponIntervalValue);
    builder.setFirstCouponTime(firstCouponTimeValue);
    builder.setExpiration(expirationValue);
    builder.setCallNoticePeriod(callNoticePeriodValue);
    builder.setEarliestCallTime(earliestCallTimeValue);

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
        auto const& expected = ownerValue;
        auto const actual = entry.getOwner();
        expectEqualField(expected, actual, "sfOwner");
    }

    {
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actual = entry.getMPTokenIssuanceID();
        expectEqualField(expected, actual, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = couponAssetValue;
        auto const actual = entry.getCouponAsset();
        expectEqualField(expected, actual, "sfCouponAsset");
    }

    {
        auto const& expected = accruedPerUnitValue;
        auto const actualOpt = entry.getAccruedPerUnit();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfAccruedPerUnit");
        EXPECT_TRUE(entry.hasAccruedPerUnit());
    }

    {
        auto const& expected = poolAmountValue;
        auto const actualOpt = entry.getPoolAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfPoolAmount");
        EXPECT_TRUE(entry.hasPoolAmount());
    }

    {
        auto const& expected = claimantCountValue;
        auto const actualOpt = entry.getClaimantCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfClaimantCount");
        EXPECT_TRUE(entry.hasClaimantCount());
    }

    {
        auto const& expected = couponCountValue;
        auto const actualOpt = entry.getCouponCount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCouponCount");
        EXPECT_TRUE(entry.hasCouponCount());
    }

    {
        auto const& expected = lastCouponTimeValue;
        auto const actualOpt = entry.getLastCouponTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfLastCouponTime");
        EXPECT_TRUE(entry.hasLastCouponTime());
    }

    {
        auto const& expected = couponAmountValue;
        auto const actualOpt = entry.getCouponAmount();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCouponAmount");
        EXPECT_TRUE(entry.hasCouponAmount());
    }

    {
        auto const& expected = couponIntervalValue;
        auto const actualOpt = entry.getCouponInterval();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCouponInterval");
        EXPECT_TRUE(entry.hasCouponInterval());
    }

    {
        auto const& expected = firstCouponTimeValue;
        auto const actualOpt = entry.getFirstCouponTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfFirstCouponTime");
        EXPECT_TRUE(entry.hasFirstCouponTime());
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    {
        auto const& expected = callNoticePeriodValue;
        auto const actualOpt = entry.getCallNoticePeriod();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfCallNoticePeriod");
        EXPECT_TRUE(entry.hasCallNoticePeriod());
    }

    {
        auto const& expected = earliestCallTimeValue;
        auto const actualOpt = entry.getEarliestCallTime();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfEarliestCallTime");
        EXPECT_TRUE(entry.hasEarliestCallTime());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(CouponScheduleTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();
    auto const accruedPerUnitValue = canonical_AMOUNT();
    auto const poolAmountValue = canonical_AMOUNT();
    auto const claimantCountValue = canonical_UINT32();
    auto const couponCountValue = canonical_UINT32();
    auto const lastCouponTimeValue = canonical_UINT32();
    auto const couponAmountValue = canonical_AMOUNT();
    auto const couponIntervalValue = canonical_UINT32();
    auto const firstCouponTimeValue = canonical_UINT32();
    auto const expirationValue = canonical_UINT32();
    auto const callNoticePeriodValue = canonical_UINT32();
    auto const earliestCallTimeValue = canonical_UINT32();

    auto sle = std::make_shared<SLE>(CouponSchedule::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfOwnerNode) = ownerNodeValue;
    sle->at(sfOwner) = ownerValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfMPTokenIssuanceID) = mPTokenIssuanceIDValue;
    sle->at(sfCouponAsset) = STIssue(sfCouponAsset, couponAssetValue);
    sle->at(sfAccruedPerUnit) = accruedPerUnitValue;
    sle->at(sfPoolAmount) = poolAmountValue;
    sle->at(sfClaimantCount) = claimantCountValue;
    sle->at(sfCouponCount) = couponCountValue;
    sle->at(sfLastCouponTime) = lastCouponTimeValue;
    sle->at(sfCouponAmount) = couponAmountValue;
    sle->at(sfCouponInterval) = couponIntervalValue;
    sle->at(sfFirstCouponTime) = firstCouponTimeValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfCallNoticePeriod) = callNoticePeriodValue;
    sle->at(sfEarliestCallTime) = earliestCallTimeValue;

    CouponScheduleBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    CouponSchedule entryFromSle{sle};
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
        auto const& expected = ownerValue;

        auto const fromSle = entryFromSle.getOwner();
        auto const fromBuilder = entryFromBuilder.getOwner();

        expectEqualField(expected, fromSle, "sfOwner");
        expectEqualField(expected, fromBuilder, "sfOwner");
    }

    {
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;

        auto const fromSle = entryFromSle.getMPTokenIssuanceID();
        auto const fromBuilder = entryFromBuilder.getMPTokenIssuanceID();

        expectEqualField(expected, fromSle, "sfMPTokenIssuanceID");
        expectEqualField(expected, fromBuilder, "sfMPTokenIssuanceID");
    }

    {
        auto const& expected = couponAssetValue;

        auto const fromSle = entryFromSle.getCouponAsset();
        auto const fromBuilder = entryFromBuilder.getCouponAsset();

        expectEqualField(expected, fromSle, "sfCouponAsset");
        expectEqualField(expected, fromBuilder, "sfCouponAsset");
    }

    {
        auto const& expected = accruedPerUnitValue;

        auto const fromSleOpt = entryFromSle.getAccruedPerUnit();
        auto const fromBuilderOpt = entryFromBuilder.getAccruedPerUnit();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfAccruedPerUnit");
        expectEqualField(expected, *fromBuilderOpt, "sfAccruedPerUnit");
    }

    {
        auto const& expected = poolAmountValue;

        auto const fromSleOpt = entryFromSle.getPoolAmount();
        auto const fromBuilderOpt = entryFromBuilder.getPoolAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfPoolAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfPoolAmount");
    }

    {
        auto const& expected = claimantCountValue;

        auto const fromSleOpt = entryFromSle.getClaimantCount();
        auto const fromBuilderOpt = entryFromBuilder.getClaimantCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfClaimantCount");
        expectEqualField(expected, *fromBuilderOpt, "sfClaimantCount");
    }

    {
        auto const& expected = couponCountValue;

        auto const fromSleOpt = entryFromSle.getCouponCount();
        auto const fromBuilderOpt = entryFromBuilder.getCouponCount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCouponCount");
        expectEqualField(expected, *fromBuilderOpt, "sfCouponCount");
    }

    {
        auto const& expected = lastCouponTimeValue;

        auto const fromSleOpt = entryFromSle.getLastCouponTime();
        auto const fromBuilderOpt = entryFromBuilder.getLastCouponTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfLastCouponTime");
        expectEqualField(expected, *fromBuilderOpt, "sfLastCouponTime");
    }

    {
        auto const& expected = couponAmountValue;

        auto const fromSleOpt = entryFromSle.getCouponAmount();
        auto const fromBuilderOpt = entryFromBuilder.getCouponAmount();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCouponAmount");
        expectEqualField(expected, *fromBuilderOpt, "sfCouponAmount");
    }

    {
        auto const& expected = couponIntervalValue;

        auto const fromSleOpt = entryFromSle.getCouponInterval();
        auto const fromBuilderOpt = entryFromBuilder.getCouponInterval();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCouponInterval");
        expectEqualField(expected, *fromBuilderOpt, "sfCouponInterval");
    }

    {
        auto const& expected = firstCouponTimeValue;

        auto const fromSleOpt = entryFromSle.getFirstCouponTime();
        auto const fromBuilderOpt = entryFromBuilder.getFirstCouponTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfFirstCouponTime");
        expectEqualField(expected, *fromBuilderOpt, "sfFirstCouponTime");
    }

    {
        auto const& expected = expirationValue;

        auto const fromSleOpt = entryFromSle.getExpiration();
        auto const fromBuilderOpt = entryFromBuilder.getExpiration();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfExpiration");
        expectEqualField(expected, *fromBuilderOpt, "sfExpiration");
    }

    {
        auto const& expected = callNoticePeriodValue;

        auto const fromSleOpt = entryFromSle.getCallNoticePeriod();
        auto const fromBuilderOpt = entryFromBuilder.getCallNoticePeriod();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfCallNoticePeriod");
        expectEqualField(expected, *fromBuilderOpt, "sfCallNoticePeriod");
    }

    {
        auto const& expected = earliestCallTimeValue;

        auto const fromSleOpt = entryFromSle.getEarliestCallTime();
        auto const fromBuilderOpt = entryFromBuilder.getEarliestCallTime();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfEarliestCallTime");
        expectEqualField(expected, *fromBuilderOpt, "sfEarliestCallTime");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(CouponScheduleTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(CouponSchedule{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(CouponScheduleTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(CouponScheduleBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(CouponScheduleTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const ownerNodeValue = canonical_UINT64();
    auto const ownerValue = canonical_ACCOUNT();
    auto const accountValue = canonical_ACCOUNT();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const couponAssetValue = canonical_ISSUE();

    CouponScheduleBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        ownerNodeValue,
        ownerValue,
        accountValue,
        mPTokenIssuanceIDValue,
        couponAssetValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasAccruedPerUnit());
    EXPECT_FALSE(entry.getAccruedPerUnit().has_value());
    EXPECT_FALSE(entry.hasPoolAmount());
    EXPECT_FALSE(entry.getPoolAmount().has_value());
    EXPECT_FALSE(entry.hasClaimantCount());
    EXPECT_FALSE(entry.getClaimantCount().has_value());
    EXPECT_FALSE(entry.hasCouponCount());
    EXPECT_FALSE(entry.getCouponCount().has_value());
    EXPECT_FALSE(entry.hasLastCouponTime());
    EXPECT_FALSE(entry.getLastCouponTime().has_value());
    EXPECT_FALSE(entry.hasCouponAmount());
    EXPECT_FALSE(entry.getCouponAmount().has_value());
    EXPECT_FALSE(entry.hasCouponInterval());
    EXPECT_FALSE(entry.getCouponInterval().has_value());
    EXPECT_FALSE(entry.hasFirstCouponTime());
    EXPECT_FALSE(entry.getFirstCouponTime().has_value());
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasCallNoticePeriod());
    EXPECT_FALSE(entry.getCallNoticePeriod().has_value());
    EXPECT_FALSE(entry.hasEarliestCallTime());
    EXPECT_FALSE(entry.getEarliestCallTime().has_value());
}
}
