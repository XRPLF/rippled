// Auto-generated unit tests for ledger entry AMMTick


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMMTick.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMTickTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const aMMIDValue = canonical_UINT256();
    auto const tickIndexValue = canonical_INT32();
    auto const liquidityNetValue = canonical_UINT64();
    auto const liquidityGrossValue = canonical_UINT64();
    auto const feeGrowthOutside0Value = canonical_NUMBER();
    auto const feeGrowthOutside1Value = canonical_NUMBER();
    auto const ownerNodeValue = canonical_UINT64();

    AMMTickBuilder builder{
        aMMIDValue,
        tickIndexValue,
        liquidityNetValue,
        liquidityGrossValue,
        feeGrowthOutside0Value,
        feeGrowthOutside1Value,
        ownerNodeValue
    };


    builder.setLedgerIndex(index);
    builder.setFlags(0x1u);

    EXPECT_TRUE(builder.validate());

    auto const entry = builder.build(index);

    EXPECT_TRUE(entry.validate());

    {
        auto const& expected = aMMIDValue;
        auto const actual = entry.getAMMID();
        expectEqualField(expected, actual, "sfAMMID");
    }

    {
        auto const& expected = tickIndexValue;
        auto const actual = entry.getTickIndex();
        expectEqualField(expected, actual, "sfTickIndex");
    }

    {
        auto const& expected = liquidityNetValue;
        auto const actual = entry.getLiquidityNet();
        expectEqualField(expected, actual, "sfLiquidityNet");
    }

    {
        auto const& expected = liquidityGrossValue;
        auto const actual = entry.getLiquidityGross();
        expectEqualField(expected, actual, "sfLiquidityGross");
    }

    {
        auto const& expected = feeGrowthOutside0Value;
        auto const actual = entry.getFeeGrowthOutside0();
        expectEqualField(expected, actual, "sfFeeGrowthOutside0");
    }

    {
        auto const& expected = feeGrowthOutside1Value;
        auto const actual = entry.getFeeGrowthOutside1();
        expectEqualField(expected, actual, "sfFeeGrowthOutside1");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AMMTickTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const aMMIDValue = canonical_UINT256();
    auto const tickIndexValue = canonical_INT32();
    auto const liquidityNetValue = canonical_UINT64();
    auto const liquidityGrossValue = canonical_UINT64();
    auto const feeGrowthOutside0Value = canonical_NUMBER();
    auto const feeGrowthOutside1Value = canonical_NUMBER();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(AMMTick::entryType, index);

    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfTickIndex) = tickIndexValue;
    sle->at(sfLiquidityNet) = liquidityNetValue;
    sle->at(sfLiquidityGross) = liquidityGrossValue;
    sle->at(sfFeeGrowthOutside0) = feeGrowthOutside0Value;
    sle->at(sfFeeGrowthOutside1) = feeGrowthOutside1Value;
    sle->at(sfOwnerNode) = ownerNodeValue;

    AMMTickBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMMTick entryFromSle{sle};
    EXPECT_TRUE(entryFromBuilder.validate());
    EXPECT_TRUE(entryFromSle.validate());

    {
        auto const& expected = aMMIDValue;

        auto const fromSle = entryFromSle.getAMMID();
        auto const fromBuilder = entryFromBuilder.getAMMID();

        expectEqualField(expected, fromSle, "sfAMMID");
        expectEqualField(expected, fromBuilder, "sfAMMID");
    }

    {
        auto const& expected = tickIndexValue;

        auto const fromSle = entryFromSle.getTickIndex();
        auto const fromBuilder = entryFromBuilder.getTickIndex();

        expectEqualField(expected, fromSle, "sfTickIndex");
        expectEqualField(expected, fromBuilder, "sfTickIndex");
    }

    {
        auto const& expected = liquidityNetValue;

        auto const fromSle = entryFromSle.getLiquidityNet();
        auto const fromBuilder = entryFromBuilder.getLiquidityNet();

        expectEqualField(expected, fromSle, "sfLiquidityNet");
        expectEqualField(expected, fromBuilder, "sfLiquidityNet");
    }

    {
        auto const& expected = liquidityGrossValue;

        auto const fromSle = entryFromSle.getLiquidityGross();
        auto const fromBuilder = entryFromBuilder.getLiquidityGross();

        expectEqualField(expected, fromSle, "sfLiquidityGross");
        expectEqualField(expected, fromBuilder, "sfLiquidityGross");
    }

    {
        auto const& expected = feeGrowthOutside0Value;

        auto const fromSle = entryFromSle.getFeeGrowthOutside0();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthOutside0();

        expectEqualField(expected, fromSle, "sfFeeGrowthOutside0");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthOutside0");
    }

    {
        auto const& expected = feeGrowthOutside1Value;

        auto const fromSle = entryFromSle.getFeeGrowthOutside1();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthOutside1();

        expectEqualField(expected, fromSle, "sfFeeGrowthOutside1");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthOutside1");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AMMTickTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMTick{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMTickTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMTickBuilder{wrongEntry.getSle()}, std::runtime_error);
}

}
