// Auto-generated unit tests for ledger entry AMMBinHolding


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMMBinHolding.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMBinHoldingTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const accountValue = canonical_ACCOUNT();
    auto const aMMIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();
    auto const feeGrowthInsideLast0Value = canonical_NUMBER();
    auto const feeGrowthInsideLast1Value = canonical_NUMBER();
    auto const ownerNodeValue = canonical_UINT64();

    AMMBinHoldingBuilder builder{
        accountValue,
        aMMIDValue,
        binIDValue,
        feeGrowthInsideLast0Value,
        feeGrowthInsideLast1Value,
        ownerNodeValue
    };


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
        auto const& expected = binIDValue;
        auto const actual = entry.getBinID();
        expectEqualField(expected, actual, "sfBinID");
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

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AMMBinHoldingTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const accountValue = canonical_ACCOUNT();
    auto const aMMIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();
    auto const feeGrowthInsideLast0Value = canonical_NUMBER();
    auto const feeGrowthInsideLast1Value = canonical_NUMBER();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(AMMBinHolding::entryType, index);

    sle->at(sfAccount) = accountValue;
    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfBinID) = binIDValue;
    sle->at(sfFeeGrowthInsideLast0) = feeGrowthInsideLast0Value;
    sle->at(sfFeeGrowthInsideLast1) = feeGrowthInsideLast1Value;
    sle->at(sfOwnerNode) = ownerNodeValue;

    AMMBinHoldingBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMMBinHolding entryFromSle{sle};
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
        auto const& expected = binIDValue;

        auto const fromSle = entryFromSle.getBinID();
        auto const fromBuilder = entryFromBuilder.getBinID();

        expectEqualField(expected, fromSle, "sfBinID");
        expectEqualField(expected, fromBuilder, "sfBinID");
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

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AMMBinHoldingTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMBinHolding{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMBinHoldingTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMBinHoldingBuilder{wrongEntry.getSle()}, std::runtime_error);
}

}
