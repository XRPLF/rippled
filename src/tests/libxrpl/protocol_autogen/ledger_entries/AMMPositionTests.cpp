// Auto-generated unit tests for ledger entry AMMPosition


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMMPosition.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMPositionTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const aMMIDValue = canonical_UINT256();
    auto const tickLowerValue = canonical_INT32();
    auto const tickUpperValue = canonical_INT32();
    auto const positionLiquidityValue = canonical_UINT64();
    auto const feeGrowthInsideLast0Value = canonical_NUMBER();
    auto const feeGrowthInsideLast1Value = canonical_NUMBER();
    auto const tokensOwed0Value = canonical_AMOUNT();
    auto const tokensOwed1Value = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();

    AMMPositionBuilder builder{
        accountValue,
        aMMIDValue,
        tickLowerValue,
        tickUpperValue,
        positionLiquidityValue,
        feeGrowthInsideLast0Value,
        feeGrowthInsideLast1Value,
        ownerNodeValue
    };

    builder.setTokensOwed0(tokensOwed0Value);
    builder.setTokensOwed1(tokensOwed1Value);

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
        auto const& expected = aMMIDValue;
        auto const actual = entry.getAMMID();
        expectEqualField(expected, actual, "sfAMMID");
    }

    {
        auto const& expected = tickLowerValue;
        auto const actual = entry.getTickLower();
        expectEqualField(expected, actual, "sfTickLower");
    }

    {
        auto const& expected = tickUpperValue;
        auto const actual = entry.getTickUpper();
        expectEqualField(expected, actual, "sfTickUpper");
    }

    {
        auto const& expected = positionLiquidityValue;
        auto const actual = entry.getPositionLiquidity();
        expectEqualField(expected, actual, "sfPositionLiquidity");
    }

    {
        auto const& expected = feeGrowthInsideLast0Value;
        auto const actual = entry.getFeeGrowthInsideLast0();
        expectEqualField(expected, actual, "sfFeeGrowthInsideLast0");
    }

    {
        auto const& expected = feeGrowthInsideLast1Value;
        auto const actual = entry.getFeeGrowthInsideLast1();
        expectEqualField(expected, actual, "sfFeeGrowthInsideLast1");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = tokensOwed0Value;
        auto const actualOpt = entry.getTokensOwed0();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTokensOwed0");
        EXPECT_TRUE(entry.hasTokensOwed0());
    }

    {
        auto const& expected = tokensOwed1Value;
        auto const actualOpt = entry.getTokensOwed1();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfTokensOwed1");
        EXPECT_TRUE(entry.hasTokensOwed1());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AMMPositionTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const aMMIDValue = canonical_UINT256();
    auto const tickLowerValue = canonical_INT32();
    auto const tickUpperValue = canonical_INT32();
    auto const positionLiquidityValue = canonical_UINT64();
    auto const feeGrowthInsideLast0Value = canonical_NUMBER();
    auto const feeGrowthInsideLast1Value = canonical_NUMBER();
    auto const tokensOwed0Value = canonical_AMOUNT();
    auto const tokensOwed1Value = canonical_AMOUNT();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(AMMPosition::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfTickLower) = tickLowerValue;
    sle->at(sfTickUpper) = tickUpperValue;
    sle->at(sfPositionLiquidity) = positionLiquidityValue;
    sle->at(sfFeeGrowthInsideLast0) = feeGrowthInsideLast0Value;
    sle->at(sfFeeGrowthInsideLast1) = feeGrowthInsideLast1Value;
    sle->at(sfTokensOwed0) = tokensOwed0Value;
    sle->at(sfTokensOwed1) = tokensOwed1Value;
    sle->at(sfOwnerNode) = ownerNodeValue;

    AMMPositionBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMMPosition entryFromSle{sle};
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
        auto const& expected = aMMIDValue;

        auto const fromSle = entryFromSle.getAMMID();
        auto const fromBuilder = entryFromBuilder.getAMMID();

        expectEqualField(expected, fromSle, "sfAMMID");
        expectEqualField(expected, fromBuilder, "sfAMMID");
    }

    {
        auto const& expected = tickLowerValue;

        auto const fromSle = entryFromSle.getTickLower();
        auto const fromBuilder = entryFromBuilder.getTickLower();

        expectEqualField(expected, fromSle, "sfTickLower");
        expectEqualField(expected, fromBuilder, "sfTickLower");
    }

    {
        auto const& expected = tickUpperValue;

        auto const fromSle = entryFromSle.getTickUpper();
        auto const fromBuilder = entryFromBuilder.getTickUpper();

        expectEqualField(expected, fromSle, "sfTickUpper");
        expectEqualField(expected, fromBuilder, "sfTickUpper");
    }

    {
        auto const& expected = positionLiquidityValue;

        auto const fromSle = entryFromSle.getPositionLiquidity();
        auto const fromBuilder = entryFromBuilder.getPositionLiquidity();

        expectEqualField(expected, fromSle, "sfPositionLiquidity");
        expectEqualField(expected, fromBuilder, "sfPositionLiquidity");
    }

    {
        auto const& expected = feeGrowthInsideLast0Value;

        auto const fromSle = entryFromSle.getFeeGrowthInsideLast0();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthInsideLast0();

        expectEqualField(expected, fromSle, "sfFeeGrowthInsideLast0");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthInsideLast0");
    }

    {
        auto const& expected = feeGrowthInsideLast1Value;

        auto const fromSle = entryFromSle.getFeeGrowthInsideLast1();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthInsideLast1();

        expectEqualField(expected, fromSle, "sfFeeGrowthInsideLast1");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthInsideLast1");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = tokensOwed0Value;

        auto const fromSleOpt = entryFromSle.getTokensOwed0();
        auto const fromBuilderOpt = entryFromBuilder.getTokensOwed0();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTokensOwed0");
        expectEqualField(expected, *fromBuilderOpt, "sfTokensOwed0");
    }

    {
        auto const& expected = tokensOwed1Value;

        auto const fromSleOpt = entryFromSle.getTokensOwed1();
        auto const fromBuilderOpt = entryFromBuilder.getTokensOwed1();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfTokensOwed1");
        expectEqualField(expected, *fromBuilderOpt, "sfTokensOwed1");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AMMPositionTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMPosition{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMPositionTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMPositionBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(AMMPositionTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const accountValue = canonical_ACCOUNT();
    auto const aMMIDValue = canonical_UINT256();
    auto const tickLowerValue = canonical_INT32();
    auto const tickUpperValue = canonical_INT32();
    auto const positionLiquidityValue = canonical_UINT64();
    auto const feeGrowthInsideLast0Value = canonical_NUMBER();
    auto const feeGrowthInsideLast1Value = canonical_NUMBER();
    auto const ownerNodeValue = canonical_UINT64();

    AMMPositionBuilder builder{
        accountValue,
        aMMIDValue,
        tickLowerValue,
        tickUpperValue,
        positionLiquidityValue,
        feeGrowthInsideLast0Value,
        feeGrowthInsideLast1Value,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasTokensOwed0());
    EXPECT_FALSE(entry.getTokensOwed0().has_value());
    EXPECT_FALSE(entry.hasTokensOwed1());
    EXPECT_FALSE(entry.getTokensOwed1().has_value());
}
}
