// Auto-generated unit tests for ledger entry PerpOrder


#include <gtest/gtest.h>

#include <protocol_autogen/TestHelpers.h>

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol_autogen/ledger_entries/PerpOrder.h>
#include <xrpl/protocol_autogen/ledger_entries/Ticket.h>

#include <string>

namespace xrpl::ledger_entries {

// 1 & 4) Set fields via builder setters, build, then read them back via
// wrapper getters. After build(), validate() should succeed for both the
// builder's STObject and the wrapper's SLE.
TEST(PerpOrderTests, BuilderSettersRoundTrip)
{
    UInt256 const index{1u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();
    auto const expirationValue = canonical_UINT32();
    auto const builderValue = canonical_ACCOUNT();
    auto const builderFeeValue = canonical_UINT32();
    auto const bookDirectoryValue = canonical_UINT256();
    auto const bookNodeValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();

    PerpOrderBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        accountValue,
        sequenceValue,
        marginAccountIDValue,
        marketIDValue,
        limitPriceValue,
        sizeValue,
        bookDirectoryValue,
        bookNodeValue,
        ownerNodeValue
    };

    builder.setExpiration(expirationValue);
    builder.setBuilder(builderValue);
    builder.setBuilderFee(builderFeeValue);

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
        auto const& expected = accountValue;
        auto const actual = entry.getAccount();
        expectEqualField(expected, actual, "sfAccount");
    }

    {
        auto const& expected = sequenceValue;
        auto const actual = entry.getSequence();
        expectEqualField(expected, actual, "sfSequence");
    }

    {
        auto const& expected = marginAccountIDValue;
        auto const actual = entry.getMarginAccountID();
        expectEqualField(expected, actual, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;
        auto const actual = entry.getMarketID();
        expectEqualField(expected, actual, "sfMarketID");
    }

    {
        auto const& expected = limitPriceValue;
        auto const actual = entry.getLimitPrice();
        expectEqualField(expected, actual, "sfLimitPrice");
    }

    {
        auto const& expected = sizeValue;
        auto const actual = entry.getSize();
        expectEqualField(expected, actual, "sfSize");
    }

    {
        auto const& expected = bookDirectoryValue;
        auto const actual = entry.getBookDirectory();
        expectEqualField(expected, actual, "sfBookDirectory");
    }

    {
        auto const& expected = bookNodeValue;
        auto const actual = entry.getBookNode();
        expectEqualField(expected, actual, "sfBookNode");
    }

    {
        auto const& expected = ownerNodeValue;
        auto const actual = entry.getOwnerNode();
        expectEqualField(expected, actual, "sfOwnerNode");
    }

    {
        auto const& expected = expirationValue;
        auto const actualOpt = entry.getExpiration();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfExpiration");
        EXPECT_TRUE(entry.hasExpiration());
    }

    {
        auto const& expected = builderValue;
        auto const actualOpt = entry.getBuilder();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBuilder");
        EXPECT_TRUE(entry.hasBuilder());
    }

    {
        auto const& expected = builderFeeValue;
        auto const actualOpt = entry.getBuilderFee();
        ASSERT_TRUE(actualOpt.has_value());
        expectEqualField(expected, *actualOpt, "sfBuilderFee");
        EXPECT_TRUE(entry.hasBuilderFee());
    }

    EXPECT_TRUE(entry.hasLedgerIndex());
    auto const ledgerIndex = entry.getLedgerIndex();
    ASSERT_TRUE(ledgerIndex.has_value());
    EXPECT_EQ(*ledgerIndex, index);
    EXPECT_EQ(entry.getKey(), index);
}

// 2 & 4) Start from an SLE, set fields directly on it, construct a builder
// from that SLE, build a new wrapper, and verify all fields (and validate()).
TEST(PerpOrderTests, BuilderFromSleRoundTrip)
{
    UInt256 const index{2u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();
    auto const expirationValue = canonical_UINT32();
    auto const builderValue = canonical_ACCOUNT();
    auto const builderFeeValue = canonical_UINT32();
    auto const bookDirectoryValue = canonical_UINT256();
    auto const bookNodeValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();

    auto sle = std::make_shared<SLE>(PerpOrder::entryType, index);

    sle->at(sfPreviousTxnID) = previousTxnIDValue;
    sle->at(sfPreviousTxnLgrSeq) = previousTxnLgrSeqValue;
    sle->at(sfAccount) = accountValue;
    sle->at(sfSequence) = sequenceValue;
    sle->at(sfMarginAccountID) = marginAccountIDValue;
    sle->at(sfMarketID) = marketIDValue;
    sle->at(sfLimitPrice) = limitPriceValue;
    sle->at(sfSize) = sizeValue;
    sle->at(sfExpiration) = expirationValue;
    sle->at(sfBuilder) = builderValue;
    sle->at(sfBuilderFee) = builderFeeValue;
    sle->at(sfBookDirectory) = bookDirectoryValue;
    sle->at(sfBookNode) = bookNodeValue;
    sle->at(sfOwnerNode) = ownerNodeValue;

    PerpOrderBuilder builderFromSle{sle};
    EXPECT_TRUE(builderFromSle.validate());

    auto const entryFromBuilder = builderFromSle.build(index);

    PerpOrder entryFromSle{sle};
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
        auto const& expected = accountValue;

        auto const fromSle = entryFromSle.getAccount();
        auto const fromBuilder = entryFromBuilder.getAccount();

        expectEqualField(expected, fromSle, "sfAccount");
        expectEqualField(expected, fromBuilder, "sfAccount");
    }

    {
        auto const& expected = sequenceValue;

        auto const fromSle = entryFromSle.getSequence();
        auto const fromBuilder = entryFromBuilder.getSequence();

        expectEqualField(expected, fromSle, "sfSequence");
        expectEqualField(expected, fromBuilder, "sfSequence");
    }

    {
        auto const& expected = marginAccountIDValue;

        auto const fromSle = entryFromSle.getMarginAccountID();
        auto const fromBuilder = entryFromBuilder.getMarginAccountID();

        expectEqualField(expected, fromSle, "sfMarginAccountID");
        expectEqualField(expected, fromBuilder, "sfMarginAccountID");
    }

    {
        auto const& expected = marketIDValue;

        auto const fromSle = entryFromSle.getMarketID();
        auto const fromBuilder = entryFromBuilder.getMarketID();

        expectEqualField(expected, fromSle, "sfMarketID");
        expectEqualField(expected, fromBuilder, "sfMarketID");
    }

    {
        auto const& expected = limitPriceValue;

        auto const fromSle = entryFromSle.getLimitPrice();
        auto const fromBuilder = entryFromBuilder.getLimitPrice();

        expectEqualField(expected, fromSle, "sfLimitPrice");
        expectEqualField(expected, fromBuilder, "sfLimitPrice");
    }

    {
        auto const& expected = sizeValue;

        auto const fromSle = entryFromSle.getSize();
        auto const fromBuilder = entryFromBuilder.getSize();

        expectEqualField(expected, fromSle, "sfSize");
        expectEqualField(expected, fromBuilder, "sfSize");
    }

    {
        auto const& expected = bookDirectoryValue;

        auto const fromSle = entryFromSle.getBookDirectory();
        auto const fromBuilder = entryFromBuilder.getBookDirectory();

        expectEqualField(expected, fromSle, "sfBookDirectory");
        expectEqualField(expected, fromBuilder, "sfBookDirectory");
    }

    {
        auto const& expected = bookNodeValue;

        auto const fromSle = entryFromSle.getBookNode();
        auto const fromBuilder = entryFromBuilder.getBookNode();

        expectEqualField(expected, fromSle, "sfBookNode");
        expectEqualField(expected, fromBuilder, "sfBookNode");
    }

    {
        auto const& expected = ownerNodeValue;

        auto const fromSle = entryFromSle.getOwnerNode();
        auto const fromBuilder = entryFromBuilder.getOwnerNode();

        expectEqualField(expected, fromSle, "sfOwnerNode");
        expectEqualField(expected, fromBuilder, "sfOwnerNode");
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
        auto const& expected = builderValue;

        auto const fromSleOpt = entryFromSle.getBuilder();
        auto const fromBuilderOpt = entryFromBuilder.getBuilder();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBuilder");
        expectEqualField(expected, *fromBuilderOpt, "sfBuilder");
    }

    {
        auto const& expected = builderFeeValue;

        auto const fromSleOpt = entryFromSle.getBuilderFee();
        auto const fromBuilderOpt = entryFromBuilder.getBuilderFee();

        ASSERT_TRUE(fromSleOpt.has_value());
        ASSERT_TRUE(fromBuilderOpt.has_value());

        expectEqualField(expected, *fromSleOpt, "sfBuilderFee");
        expectEqualField(expected, *fromBuilderOpt, "sfBuilderFee");
    }

    EXPECT_EQ(entryFromSle.getKey(), index);
    EXPECT_EQ(entryFromBuilder.getKey(), index);
}

// 3) Verify wrapper throws when constructed from wrong ledger entry type.
TEST(PerpOrderTests, WrapperThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpOrder{wrongEntry.getSle()}, std::runtime_error);
}

// 4) Verify builder throws when constructed from wrong ledger entry type.
TEST(PerpOrderTests, BuilderThrowsOnWrongEntryType)
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

    EXPECT_THROW(PerpOrderBuilder{wrongEntry.getSle()}, std::runtime_error);
}

// 5) Build with only required fields and verify optional fields return nullopt.
TEST(PerpOrderTests, OptionalFieldsReturnNullopt)
{
    UInt256 const index{3u};

    auto const previousTxnIDValue = canonical_UINT256();
    auto const previousTxnLgrSeqValue = canonical_UINT32();
    auto const accountValue = canonical_ACCOUNT();
    auto const sequenceValue = canonical_UINT32();
    auto const marginAccountIDValue = canonical_UINT256();
    auto const marketIDValue = canonical_UINT256();
    auto const limitPriceValue = canonical_NUMBER();
    auto const sizeValue = canonical_NUMBER();
    auto const bookDirectoryValue = canonical_UINT256();
    auto const bookNodeValue = canonical_UINT64();
    auto const ownerNodeValue = canonical_UINT64();

    PerpOrderBuilder builder{
        previousTxnIDValue,
        previousTxnLgrSeqValue,
        accountValue,
        sequenceValue,
        marginAccountIDValue,
        marketIDValue,
        limitPriceValue,
        sizeValue,
        bookDirectoryValue,
        bookNodeValue,
        ownerNodeValue
    };

    auto const entry = builder.build(index);

    // Verify optional fields are not present
    EXPECT_FALSE(entry.hasExpiration());
    EXPECT_FALSE(entry.getExpiration().has_value());
    EXPECT_FALSE(entry.hasBuilder());
    EXPECT_FALSE(entry.getBuilder().has_value());
    EXPECT_FALSE(entry.hasBuilderFee());
    EXPECT_FALSE(entry.getBuilderFee().has_value());
}
}
