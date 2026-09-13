// Auto-generated unit tests for ledger entry AMMBin


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMMBin.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMBinTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const aMMIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();
    auto const reserve0Value = canonical_AMOUNT();
    auto const reserve1Value = canonical_AMOUNT();
    auto const feeGrowthBin0Value = canonical_NUMBER();
    auto const feeGrowthBin1Value = canonical_NUMBER();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const ownerNodeValue = canonical_UINT64();

    AMMBinBuilder builder{
        aMMIDValue,
        binIDValue,
        reserve0Value,
        reserve1Value,
        feeGrowthBin0Value,
        feeGrowthBin1Value,
        outstandingAmountValue,
        ownerNodeValue
    };

    builder.setMPTokenIssuanceID(mPTokenIssuanceIDValue);

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
        auto const& expected = binIDValue;
        auto const actual = entry.getBinID();
        expectEqualField(expected, actual, "sfBinID");
    }

    {
        auto const& expected = reserve0Value;
        auto const actual = entry.getReserve0();
        expectEqualField(expected, actual, "sfReserve0");
    }

    {
        auto const& expected = reserve1Value;
        auto const actual = entry.getReserve1();
        expectEqualField(expected, actual, "sfReserve1");
    }

    {
        auto const& expected = feeGrowthBin0Value;
        auto const actual = entry.getFeeGrowthBin0();
        expectEqualField(expected, actual, "sfFeeGrowthBin0");
    }

    {
        auto const& expected = feeGrowthBin1Value;
        auto const actual = entry.getFeeGrowthBin1();
        expectEqualField(expected, actual, "sfFeeGrowthBin1");
    }

    {
        auto const& expected = outstandingAmountValue;
        auto const actual = entry.getOutstandingAmount();
        expectEqualField(expected, actual, "sfOutstandingAmount");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;
        auto const actualOpt = entry.getMPTokenIssuanceID();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfMPTokenIssuanceID");
        EXPECT_TRUE(entry.hasMPTokenIssuanceID());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(AMMBinTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const aMMIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();
    auto const reserve0Value = canonical_AMOUNT();
    auto const reserve1Value = canonical_AMOUNT();
    auto const feeGrowthBin0Value = canonical_NUMBER();
    auto const feeGrowthBin1Value = canonical_NUMBER();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const mPTokenIssuanceIDValue = canonical_UINT192();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(AMMBin::entryType, index);

    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfBinID) = binIDValue;
    sle->at(sfReserve0) = reserve0Value;
    sle->at(sfReserve1) = reserve1Value;
    sle->at(sfFeeGrowthBin0) = feeGrowthBin0Value;
    sle->at(sfFeeGrowthBin1) = feeGrowthBin1Value;
    sle->at(sfOutstandingAmount) = outstandingAmountValue;
    sle->at(sfMPTokenIssuanceID) = mPTokenIssuanceIDValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    AMMBinBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMMBin entryFromSle{sle};
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
        auto const& expected = binIDValue;

        auto const fromSle = entryFromSle.getBinID();
        auto const fromBuilder = entryFromBuilder.getBinID();

        expectEqualField(expected, fromSle, "sfBinID");
        expectEqualField(expected, fromBuilder, "sfBinID");
    }

    {
        auto const& expected = reserve0Value;

        auto const fromSle = entryFromSle.getReserve0();
        auto const fromBuilder = entryFromBuilder.getReserve0();

        expectEqualField(expected, fromSle, "sfReserve0");
        expectEqualField(expected, fromBuilder, "sfReserve0");
    }

    {
        auto const& expected = reserve1Value;

        auto const fromSle = entryFromSle.getReserve1();
        auto const fromBuilder = entryFromBuilder.getReserve1();

        expectEqualField(expected, fromSle, "sfReserve1");
        expectEqualField(expected, fromBuilder, "sfReserve1");
    }

    {
        auto const& expected = feeGrowthBin0Value;

        auto const fromSle = entryFromSle.getFeeGrowthBin0();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthBin0();

        expectEqualField(expected, fromSle, "sfFeeGrowthBin0");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthBin0");
    }

    {
        auto const& expected = feeGrowthBin1Value;

        auto const fromSle = entryFromSle.getFeeGrowthBin1();
        auto const fromBuilder = entryFromBuilder.getFeeGrowthBin1();

        expectEqualField(expected, fromSle, "sfFeeGrowthBin1");
        expectEqualField(expected, fromBuilder, "sfFeeGrowthBin1");
    }

    {
        auto const& expected = outstandingAmountValue;

        auto const fromSle = entryFromSle.getOutstandingAmount();
        auto const fromBuilder = entryFromBuilder.getOutstandingAmount();

        expectEqualField(expected, fromSle, "sfOutstandingAmount");
        expectEqualField(expected, fromBuilder, "sfOutstandingAmount");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
    }

    {
        auto const& expected = mPTokenIssuanceIDValue;

        auto const fromSleOpt = entryFromSle.getMPTokenIssuanceID();
        auto const fromBuilderOpt = entryFromBuilder.getMPTokenIssuanceID();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfMPTokenIssuanceID");
        expectEqualField(expected, *fromBuilderOpt, "sfMPTokenIssuanceID");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(AMMBinTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMBin{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMBinTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMBinBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(AMMBinTests, OptionalFieldsReturnNullopt)
{
    uint256 const index{3u};

    auto const aMMIDValue = canonical_UINT256();
    auto const binIDValue = canonical_INT32();
    auto const reserve0Value = canonical_AMOUNT();
    auto const reserve1Value = canonical_AMOUNT();
    auto const feeGrowthBin0Value = canonical_NUMBER();
    auto const feeGrowthBin1Value = canonical_NUMBER();
    auto const outstandingAmountValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();

    AMMBinBuilder builder{
        aMMIDValue,
        binIDValue,
        reserve0Value,
        reserve1Value,
        feeGrowthBin0Value,
        feeGrowthBin1Value,
        outstandingAmountValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasMPTokenIssuanceID());
    EXPECT_FALSE(entry.getMPTokenIssuanceID().has_value());
}
}
