// Auto-generated unit tests for ledger entry AMMTickBitmap


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/AMMTickBitmap.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(AMMTickBitmapTests, BuilderSettersRoundTrip)
{
    uint256 const index{1u};

    auto const aMMIDValue = canonical_UINT256();
    auto const bitmapWordIndexValue = canonical_UINT16();
    auto const bitmapBitsValue = canonical_UINT256();
    auto const ownerNodeValue = canonical_UINT64();

    AMMTickBitmapBuilder builder{
        aMMIDValue,
        bitmapWordIndexValue,
        bitmapBitsValue,
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
        auto const& expected = bitmapWordIndexValue;
        auto const actual = entry.getBitmapWordIndex();
        expectEqualField(expected, actual, "sfBitmapWordIndex");
    }

    {
        auto const& expected = bitmapBitsValue;
        auto const actual = entry.getBitmapBits();
        expectEqualField(expected, actual, "sfBitmapBits");
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
TEST(AMMTickBitmapTests, BuilderFromSleRoundTrip)
{
    uint256 const index{2u};

    auto const aMMIDValue = canonical_UINT256();
    auto const bitmapWordIndexValue = canonical_UINT16();
    auto const bitmapBitsValue = canonical_UINT256();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(AMMTickBitmap::entryType, index);

    sle->at(sfAMMID) = aMMIDValue;
    sle->at(sfBitmapWordIndex) = bitmapWordIndexValue;
    sle->at(sfBitmapBits) = bitmapBitsValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    AMMTickBitmapBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    AMMTickBitmap entryFromSle{sle};
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
        auto const& expected = bitmapWordIndexValue;

        auto const fromSle = entryFromSle.getBitmapWordIndex();
        auto const fromBuilder = entryFromBuilder.getBitmapWordIndex();

        expectEqualField(expected, fromSle, "sfBitmapWordIndex");
        expectEqualField(expected, fromBuilder, "sfBitmapWordIndex");
    }

    {
        auto const& expected = bitmapBitsValue;

        auto const fromSle = entryFromSle.getBitmapBits();
        auto const fromBuilder = entryFromBuilder.getBitmapBits();

        expectEqualField(expected, fromSle, "sfBitmapBits");
        expectEqualField(expected, fromBuilder, "sfBitmapBits");
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
TEST(AMMTickBitmapTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMTickBitmap{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(AMMTickBitmapTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(AMMTickBitmapBuilder{wrongEntry.getSle()}, std::runtime_error);
}

}
